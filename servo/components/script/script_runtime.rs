/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! The script runtime contains common traits and structs commonly used by the
//! script thread, the dom, and the worker threads.

use dom::bindings::js::{RootCollection, RootCollectionPtr, trace_roots};
use dom::bindings::refcounted::{LiveDOMReferences, TrustedReference, trace_refcounted_objects};
use dom::bindings::trace::trace_traceables;
use dom::bindings::utils::DOM_CALLBACKS;
use js::glue::CollectServoSizes;
use js::jsapi::{DisableIncrementalGC, GCDescription, GCProgress};
use js::jsapi::{JSContext, JS_GetRuntime, JSRuntime, JSTracer, SetDOMCallbacks, SetGCSliceCallback};
use js::jsapi::{JSGCInvocationKind, JSGCStatus, JS_AddExtraGCRootsTracer, JS_SetGCCallback};
use js::jsapi::{JSObject, SetPreserveWrapperCallback};
use js::rust::Runtime;
use libc;
use profile_traits::mem::{Report, ReportKind, ReportsChan};
use script_thread::{Runnable, STACK_ROOTS, trace_thread};
use std::cell::Cell;
use std::io::{Write, stdout};
use std::marker::PhantomData;
use std::ptr;
use time::{Tm, now};
use util::opts;
use util::thread_state;

/// Common messages used to control the event loops in both the script and the worker
pub enum CommonScriptMsg {
    /// Requests that the script thread measure its memory usage. The results are sent back via the
    /// supplied channel.
    CollectReports(ReportsChan),
    /// A DOM object's last pinned reference was removed (dispatched to all threads).
    RefcountCleanup(TrustedReference),
    /// Generic message that encapsulates event handling.
    RunnableMsg(ScriptThreadEventCategory, Box<Runnable + Send>),
}

/// A cloneable interface for communicating with an event loop.
pub trait ScriptChan {
    /// Send a message to the associated event loop.
    fn send(&self, msg: CommonScriptMsg) -> Result<(), ()>;
    /// Clone this handle.
    fn clone(&self) -> Box<ScriptChan + Send>;
}

#[derive(Clone, Copy, Debug, Eq, Hash, JSTraceable, PartialEq)]
pub enum ScriptThreadEventCategory {
    AttachLayout,
    ConstellationMsg,
    DevtoolsMsg,
    DocumentEvent,
    DomEvent,
    FileRead,
    FormPlannedNavigation,
    ImageCacheMsg,
    InputEvent,
    NetworkEvent,
    Resize,
    ScriptEvent,
    SetViewport,
    StylesheetLoad,
    TimerEvent,
    UpdateReplacedElement,
    WebSocketEvent,
    WorkerEvent,
}

/// An interface for receiving ScriptMsg values in an event loop. Used for synchronous DOM
/// APIs that need to abstract over multiple kinds of event loops (worker/main thread) with
/// different Receiver interfaces.
pub trait ScriptPort {
    fn recv(&self) -> Result<CommonScriptMsg, ()>;
}

pub struct StackRootTLS<'a>(PhantomData<&'a u32>);

impl<'a> StackRootTLS<'a> {
    pub fn new(roots: &'a RootCollection) -> StackRootTLS<'a> {
        STACK_ROOTS.with(|ref r| {
            r.set(Some(RootCollectionPtr(roots as *const _)))
        });
        StackRootTLS(PhantomData)
    }
}

impl<'a> Drop for StackRootTLS<'a> {
    fn drop(&mut self) {
        STACK_ROOTS.with(|ref r| r.set(None));
    }
}

#[allow(unsafe_code)]
pub fn new_rt_and_cx() -> Runtime {
    LiveDOMReferences::initialize();
    let runtime = Runtime::new();

    unsafe {
        JS_AddExtraGCRootsTracer(runtime.rt(), Some(trace_rust_roots), ptr::null_mut());
        JS_AddExtraGCRootsTracer(runtime.rt(), Some(trace_refcounted_objects), ptr::null_mut());
    }

    // Needed for debug assertions about whether GC is running.
    if cfg!(debug_assertions) {
        unsafe {
            JS_SetGCCallback(runtime.rt(), Some(debug_gc_callback), ptr::null_mut());
        }
    }
    if opts::get().gc_profile {
        unsafe {
            SetGCSliceCallback(runtime.rt(), Some(gc_slice_callback));
        }
    }

    unsafe {
        unsafe extern "C" fn empty_wrapper_callback(_: *mut JSContext, _: *mut JSObject) -> bool { true }
        SetDOMCallbacks(runtime.rt(), &DOM_CALLBACKS);
        SetPreserveWrapperCallback(runtime.rt(), Some(empty_wrapper_callback));
        // Pre barriers aren't working correctly at the moment
        DisableIncrementalGC(runtime.rt());
    }

    runtime
}

#[allow(unsafe_code)]
pub fn get_reports(cx: *mut JSContext, path_seg: String) -> Vec<Report> {
    let mut reports = vec![];

    unsafe {
        let rt = JS_GetRuntime(cx);
        let mut stats = ::std::mem::zeroed();
        if CollectServoSizes(rt, &mut stats) {
            let mut report = |mut path_suffix, kind, size| {
                let mut path = path![path_seg, "js"];
                path.append(&mut path_suffix);
                reports.push(Report {
                    path: path,
                    kind: kind,
                    size: size as usize,
                })
            };

            // A note about possibly confusing terminology: the JS GC "heap" is allocated via
            // mmap/VirtualAlloc, which means it's not on the malloc "heap", so we use
            // `ExplicitNonHeapSize` as its kind.

            report(path!["gc-heap", "used"],
                   ReportKind::ExplicitNonHeapSize,
                   stats.gcHeapUsed);

            report(path!["gc-heap", "unused"],
                   ReportKind::ExplicitNonHeapSize,
                   stats.gcHeapUnused);

            report(path!["gc-heap", "admin"],
                   ReportKind::ExplicitNonHeapSize,
                   stats.gcHeapAdmin);

            report(path!["gc-heap", "decommitted"],
                   ReportKind::ExplicitNonHeapSize,
                   stats.gcHeapDecommitted);

            // SpiderMonkey uses the system heap, not jemalloc.
            report(path!["malloc-heap"],
                   ReportKind::ExplicitSystemHeapSize,
                   stats.mallocHeap);

            report(path!["non-heap"],
                   ReportKind::ExplicitNonHeapSize,
                   stats.nonHeap);
        }
    }
    reports
}

thread_local!(static GC_CYCLE_START: Cell<Option<Tm>> = Cell::new(None));
thread_local!(static GC_SLICE_START: Cell<Option<Tm>> = Cell::new(None));

#[allow(unsafe_code)]
unsafe extern "C" fn gc_slice_callback(_rt: *mut JSRuntime, progress: GCProgress, desc: *const GCDescription) {
    match progress {
        GCProgress::GC_CYCLE_BEGIN => {
            GC_CYCLE_START.with(|start| {
                start.set(Some(now()));
                println!("GC cycle began");
            })
        },
        GCProgress::GC_SLICE_BEGIN => {
            GC_SLICE_START.with(|start| {
                start.set(Some(now()));
                println!("GC slice began");
            })
        },
        GCProgress::GC_SLICE_END => {
            GC_SLICE_START.with(|start| {
                let dur = now() - start.get().unwrap();
                start.set(None);
                println!("GC slice ended: duration={}", dur);
            })
        },
        GCProgress::GC_CYCLE_END => {
            GC_CYCLE_START.with(|start| {
                let dur = now() - start.get().unwrap();
                start.set(None);
                println!("GC cycle ended: duration={}", dur);
            })
        },
    };
    if !desc.is_null() {
        let desc: &GCDescription = &*desc;
        let invocationKind = match desc.invocationKind_ {
            JSGCInvocationKind::GC_NORMAL => "GC_NORMAL",
            JSGCInvocationKind::GC_SHRINK => "GC_SHRINK",
        };
        println!("  isCompartment={}, invocationKind={}", desc.isCompartment_, invocationKind);
    }
    let _ = stdout().flush();
}

#[allow(unsafe_code)]
unsafe extern "C" fn debug_gc_callback(_rt: *mut JSRuntime, status: JSGCStatus, _data: *mut libc::c_void) {
    match status {
        JSGCStatus::JSGC_BEGIN => thread_state::enter(thread_state::IN_GC),
        JSGCStatus::JSGC_END   => thread_state::exit(thread_state::IN_GC),
    }
}

#[allow(unsafe_code)]
unsafe extern fn trace_rust_roots(tr: *mut JSTracer, _data: *mut libc::c_void) {
    trace_thread(tr);
    trace_traceables(tr);
    trace_roots(tr);
}
