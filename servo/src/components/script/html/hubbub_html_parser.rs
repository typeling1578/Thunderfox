/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use dom::attr::AttrMethods;
use dom::bindings::codegen::InheritTypes::{NodeBase, NodeCast, TextCast, ElementCast};
use dom::bindings::codegen::InheritTypes::HTMLIFrameElementCast;
use dom::bindings::js::{JS, JSRef, Temporary, OptionalRootable, Root};
use dom::bindings::utils::Reflectable;
use dom::document::{Document, DocumentHelpers};
use dom::element::{AttributeHandlers, HTMLLinkElementTypeId, HTMLIFrameElementTypeId};
use dom::htmlelement::HTMLElement;
use dom::htmlheadingelement::{Heading1, Heading2, Heading3, Heading4, Heading5, Heading6};
use dom::htmliframeelement::{IFrameSize, HTMLIFrameElementHelpers};
use dom::htmlformelement::HTMLFormElement;
use dom::node::{ElementNodeTypeId, NodeHelpers, NodeMethods};
use dom::types::*;
use html::cssparse::{StylesheetProvenance, UrlProvenance, spawn_css_parser};
use script_task::Page;

use hubbub::hubbub;
use servo_msg::constellation_msg::SubpageId;
use servo_net::resource_task::{Load, Payload, Done, ResourceTask, load_whole_resource};
use servo_util::namespace::Null;
use servo_util::str::{DOMString, HTML_SPACE_CHARACTERS};
use servo_util::task::spawn_named;
use servo_util::url::parse_url;
use std::ascii::StrAsciiExt;
use std::cast;
use std::cell::RefCell;
use std::comm::{channel, Sender, Receiver};
use std::str;
use style::Stylesheet;
use url::Url;

macro_rules! handle_element(
    ($document: expr,
     $localName: expr,
     $string: expr,
     $ctor: ident
     $(, $arg:expr )*) => (
        if $string == $localName {
            return ElementCast::from_temporary($ctor::new($localName, $document $(, $arg)*));
        }
    )
)


pub struct JSFile {
    pub data: ~str,
    pub url: Url
}

pub type JSResult = Vec<JSFile>;

enum CSSMessage {
    CSSTaskNewFile(StylesheetProvenance),
    CSSTaskExit
}

enum JSMessage {
    JSTaskNewFile(Url),
    JSTaskNewInlineScript(~str, Url),
    JSTaskExit
}

/// Messages generated by the HTML parser upon discovery of additional resources
pub enum HtmlDiscoveryMessage {
    HtmlDiscoveredStyle(Stylesheet),
    HtmlDiscoveredIFrame((Url, SubpageId, bool)),
    HtmlDiscoveredScript(JSResult)
}

pub struct HtmlParserResult {
    pub discovery_port: Receiver<HtmlDiscoveryMessage>,
}

trait NodeWrapping<T> {
    unsafe fn to_hubbub_node(&self) -> hubbub::NodeDataPtr;
}

impl<'a, T: NodeBase+Reflectable> NodeWrapping<T> for JSRef<'a, T> {
    unsafe fn to_hubbub_node(&self) -> hubbub::NodeDataPtr {
        cast::transmute(self.deref())
    }
}

unsafe fn from_hubbub_node<T: Reflectable>(n: hubbub::NodeDataPtr) -> Temporary<T> {
    Temporary::new(JS::from_raw(cast::transmute(n)))
}

/**
Runs a task that coordinates parsing links to css stylesheets.

This function should be spawned in a separate task and spins waiting
for the html builder to find links to css stylesheets and sends off
tasks to parse each link.  When the html process finishes, it notifies
the listener, who then collects the css rules from each task it
spawned, collates them, and sends them to the given result channel.

# Arguments

* `to_parent` - A channel on which to send back the full set of rules.
* `from_parent` - A port on which to receive new links.

*/
fn css_link_listener(to_parent: Sender<HtmlDiscoveryMessage>,
                     from_parent: Receiver<CSSMessage>) {
    let mut result_vec = vec!();

    loop {
        match from_parent.recv_opt() {
            Some(CSSTaskNewFile(provenance)) => {
                result_vec.push(spawn_css_parser(provenance));
            }
            Some(CSSTaskExit) | None => {
                break;
            }
        }
    }

    // Send the sheets back in order
    // FIXME: Shouldn't wait until after we've recieved CSSTaskExit to start sending these
    for port in result_vec.iter() {
        to_parent.try_send(HtmlDiscoveredStyle(port.recv()));
    }
}

fn js_script_listener(to_parent: Sender<HtmlDiscoveryMessage>,
                      from_parent: Receiver<JSMessage>,
                      resource_task: ResourceTask) {
    let mut result_vec = vec!();

    loop {
        match from_parent.recv_opt() {
            Some(JSTaskNewFile(url)) => {
                match load_whole_resource(&resource_task, url.clone()) {
                    Err(_) => {
                        error!("error loading script {:s}", url.to_str());
                    }
                    Ok((metadata, bytes)) => {
                        result_vec.push(JSFile {
                            data: str::from_utf8(bytes.as_slice()).unwrap().to_owned(),
                            url: metadata.final_url,
                        });
                    }
                }
            }
            Some(JSTaskNewInlineScript(data, url)) => {
                result_vec.push(JSFile { data: data, url: url });
            }
            Some(JSTaskExit) | None => {
                break;
            }
        }
    }

    to_parent.try_send(HtmlDiscoveredScript(result_vec));
}

// Silly macros to handle constructing      DOM nodes. This produces bad code and should be optimized
// via atomization (issue #85).

pub fn build_element_from_tag(tag: DOMString, document: &JSRef<Document>) -> Temporary<Element> {
    // TODO (Issue #85): use atoms
    handle_element!(document, tag, "a",         HTMLAnchorElement);
    handle_element!(document, tag, "applet",    HTMLAppletElement);
    handle_element!(document, tag, "area",      HTMLAreaElement);
    handle_element!(document, tag, "aside",     HTMLElement);
    handle_element!(document, tag, "audio",     HTMLAudioElement);
    handle_element!(document, tag, "b",         HTMLElement);
    handle_element!(document, tag, "base",      HTMLBaseElement);
    handle_element!(document, tag, "body",      HTMLBodyElement);
    handle_element!(document, tag, "br",        HTMLBRElement);
    handle_element!(document, tag, "button",    HTMLButtonElement);
    handle_element!(document, tag, "canvas",    HTMLCanvasElement);
    handle_element!(document, tag, "caption",   HTMLTableCaptionElement);
    handle_element!(document, tag, "col",       HTMLTableColElement);
    handle_element!(document, tag, "colgroup",  HTMLTableColElement);
    handle_element!(document, tag, "data",      HTMLDataElement);
    handle_element!(document, tag, "datalist",  HTMLDataListElement);
    handle_element!(document, tag, "del",       HTMLModElement);
    handle_element!(document, tag, "dir",       HTMLDirectoryElement);
    handle_element!(document, tag, "div",       HTMLDivElement);
    handle_element!(document, tag, "dl",        HTMLDListElement);
    handle_element!(document, tag, "em",        HTMLElement);
    handle_element!(document, tag, "embed",     HTMLEmbedElement);
    handle_element!(document, tag, "fieldset",  HTMLFieldSetElement);
    handle_element!(document, tag, "font",      HTMLFontElement);
    handle_element!(document, tag, "form",      HTMLFormElement);
    handle_element!(document, tag, "frame",     HTMLFrameElement);
    handle_element!(document, tag, "frameset",  HTMLFrameSetElement);
    handle_element!(document, tag, "h1",        HTMLHeadingElement, Heading1);
    handle_element!(document, tag, "h2",        HTMLHeadingElement, Heading2);
    handle_element!(document, tag, "h3",        HTMLHeadingElement, Heading3);
    handle_element!(document, tag, "h4",        HTMLHeadingElement, Heading4);
    handle_element!(document, tag, "h5",        HTMLHeadingElement, Heading5);
    handle_element!(document, tag, "h6",        HTMLHeadingElement, Heading6);
    handle_element!(document, tag, "head",      HTMLHeadElement);
    handle_element!(document, tag, "hr",        HTMLHRElement);
    handle_element!(document, tag, "html",      HTMLHtmlElement);
    handle_element!(document, tag, "i",         HTMLElement);
    handle_element!(document, tag, "iframe",    HTMLIFrameElement);
    handle_element!(document, tag, "img",       HTMLImageElement);
    handle_element!(document, tag, "input",     HTMLInputElement);
    handle_element!(document, tag, "ins",       HTMLModElement);
    handle_element!(document, tag, "label",     HTMLLabelElement);
    handle_element!(document, tag, "legend",    HTMLLegendElement);
    handle_element!(document, tag, "li",        HTMLLIElement);
    handle_element!(document, tag, "link",      HTMLLinkElement);
    handle_element!(document, tag, "main",      HTMLMainElement);
    handle_element!(document, tag, "map",       HTMLMapElement);
    handle_element!(document, tag, "meta",      HTMLMetaElement);
    handle_element!(document, tag, "meter",     HTMLMeterElement);
    handle_element!(document, tag, "object",    HTMLObjectElement);
    handle_element!(document, tag, "ol",        HTMLOListElement);
    handle_element!(document, tag, "optgroup",  HTMLOptGroupElement);
    handle_element!(document, tag, "option",    HTMLOptionElement);
    handle_element!(document, tag, "output",    HTMLOutputElement);
    handle_element!(document, tag, "p",         HTMLParagraphElement);
    handle_element!(document, tag, "param",     HTMLParamElement);
    handle_element!(document, tag, "pre",       HTMLPreElement);
    handle_element!(document, tag, "progress",  HTMLProgressElement);
    handle_element!(document, tag, "q",         HTMLQuoteElement);
    handle_element!(document, tag, "script",    HTMLScriptElement);
    handle_element!(document, tag, "section",   HTMLElement);
    handle_element!(document, tag, "select",    HTMLSelectElement);
    handle_element!(document, tag, "small",     HTMLElement);
    handle_element!(document, tag, "source",    HTMLSourceElement);
    handle_element!(document, tag, "span",      HTMLSpanElement);
    handle_element!(document, tag, "strong",    HTMLElement);
    handle_element!(document, tag, "style",     HTMLStyleElement);
    handle_element!(document, tag, "table",     HTMLTableElement);
    handle_element!(document, tag, "tbody",     HTMLTableSectionElement);
    handle_element!(document, tag, "td",        HTMLTableDataCellElement);
    handle_element!(document, tag, "template",  HTMLTemplateElement);
    handle_element!(document, tag, "textarea",  HTMLTextAreaElement);
    handle_element!(document, tag, "th",        HTMLTableHeaderCellElement);
    handle_element!(document, tag, "time",      HTMLTimeElement);
    handle_element!(document, tag, "title",     HTMLTitleElement);
    handle_element!(document, tag, "tr",        HTMLTableRowElement);
    handle_element!(document, tag, "track",     HTMLTrackElement);
    handle_element!(document, tag, "ul",        HTMLUListElement);
    handle_element!(document, tag, "video",     HTMLVideoElement);

    return ElementCast::from_temporary(HTMLUnknownElement::new(tag, document));
}

pub fn parse_html(page: &Page,
                  document: &mut JSRef<Document>,
                  url: Url,
                  resource_task: ResourceTask)
                  -> HtmlParserResult {
    debug!("Hubbub: parsing {:?}", url);
    let next_subpage_id: SubpageId = *page.next_subpage_id.deref().borrow();
    // Spawn a CSS parser to receive links to CSS style sheets.

    let (discovery_chan, discovery_port) = channel();
    let stylesheet_chan = discovery_chan.clone();
    let (css_chan, css_msg_port) = channel();
    spawn_named("parse_html:css", proc() {
        css_link_listener(stylesheet_chan, css_msg_port);
    });

    // Spawn a JS parser to receive JavaScript.
    let resource_task2 = resource_task.clone();
    let js_result_chan = discovery_chan.clone();
    let (js_chan, js_msg_port) = channel();
    spawn_named("parse_html:js", proc() {
        js_script_listener(js_result_chan, js_msg_port, resource_task2.clone());
    });

    // Wait for the LoadResponse so that the parser knows the final URL.
    let (input_chan, input_port) = channel();
    resource_task.send(Load(url.clone(), input_chan));
    let load_response = input_port.recv();

    debug!("Fetched page; metadata is {:?}", load_response.metadata);

    let base_url = load_response.metadata.final_url.clone();
    let url2 = base_url.clone();
    let url3 = url2.clone();

    {
        // Store the final URL before we start parsing, so that DOM routines
        // (e.g. HTMLImageElement::update_image) can resolve relative URLs
        // correctly.
        *page.mut_url() = Some((url2.clone(), true));
    }

    let pipeline_id = page.id;

    let mut parser = hubbub::Parser("UTF-8", false);
    debug!("created parser");

    parser.set_document_node(unsafe { document.to_hubbub_node() });
    parser.enable_scripting(true);
    parser.enable_styling(true);

    let (css_chan2, js_chan2) = (css_chan.clone(), js_chan.clone());

    let next_subpage_id = RefCell::new(next_subpage_id);

    let doc_cell = RefCell::new(document);

    let tree_handler = hubbub::TreeHandler {
        create_comment: |data: ~str| {
            debug!("create comment");
            // NOTE: tmp vars are workaround for lifetime issues. Both required.
            let tmp_borrow = doc_cell.borrow();
            let tmp = &*tmp_borrow;
            let comment = Comment::new(data, *tmp).root();
            let comment: &JSRef<Node> = NodeCast::from_ref(&*comment);
            unsafe { comment.to_hubbub_node() }
        },
        create_doctype: |doctype: ~hubbub::Doctype| {
            debug!("create doctype");
            let ~hubbub::Doctype {name: name,
                                public_id: public_id,
                                system_id: system_id,
                                force_quirks: _ } = doctype;
            // NOTE: tmp vars are workaround for lifetime issues. Both required.
            let tmp_borrow = doc_cell.borrow();
            let tmp = &*tmp_borrow;
            let doctype_node = DocumentType::new(name, public_id, system_id, *tmp).root();
            unsafe {
                doctype_node.deref().to_hubbub_node()
            }
        },
        create_element: |tag: ~hubbub::Tag| {
            debug!("create element {:?}", tag.name.clone());
            // NOTE: tmp vars are workaround for lifetime issues. Both required.
            let tmp_borrow = doc_cell.borrow();
            let tmp = &*tmp_borrow;
            let mut element = build_element_from_tag(tag.name.clone(), *tmp).root();

            debug!("-- attach attrs");
            for attr in tag.attributes.iter() {
                //FIXME: this should have proper error handling or explicitly drop
                //       exceptions on the ground
                assert!(element.set_attr(attr.name.clone(),
                                         attr.value.clone()).is_ok());
            }

            //FIXME: workaround for https://github.com/mozilla/rust/issues/13246;
            //       we get unrooting order failures if these are inside the match.
            let rel = {
                let rel = element.get_attribute(Null, "rel").root();
                rel.map(|a| a.deref().Value())
            };
            let href = {
                let href= element.get_attribute(Null, "href").root();
                href.map(|a| a.deref().Value())
            };
            let src_opt = {
                let src_opt = element.get_attribute(Null, "src").root();
                 src_opt.map(|a| a.deref().Value())
            };

            // Spawn additional parsing, network loads, etc. from tag and attrs
            let type_id = {
                let node: &JSRef<Node> = NodeCast::from_ref(&*element);
                node.type_id()
            };
            match type_id {
                // Handle CSS style sheets from <link> elements
                ElementNodeTypeId(HTMLLinkElementTypeId) => {
                    match (rel, href) {
                        (Some(ref rel), Some(ref href)) if rel.split(HTML_SPACE_CHARACTERS.as_slice())
                                                              .any(|s| {
                                    s.eq_ignore_ascii_case("stylesheet")
                                }) => {
                            debug!("found CSS stylesheet: {:s}", *href);
                            let url = parse_url(href.as_slice(), Some(url2.clone()));
                            css_chan2.send(CSSTaskNewFile(UrlProvenance(url, resource_task.clone())));
                        }
                        _ => {}
                    }
                }

                ElementNodeTypeId(HTMLIFrameElementTypeId) => {
                    let iframe_chan = discovery_chan.clone();
                    let iframe_element: &mut JSRef<HTMLIFrameElement> =
                        HTMLIFrameElementCast::to_mut_ref(&mut *element).unwrap();
                    let sandboxed = iframe_element.is_sandboxed();
                    for src in src_opt.iter() {
                        let iframe_url = parse_url(*src, Some(url2.clone()));
                        iframe_element.set_frame(iframe_url.clone());

                        // Subpage Id
                        let subpage_id = *next_subpage_id.borrow();
                        let SubpageId(id_num) = subpage_id;
                        *next_subpage_id.borrow_mut() = SubpageId(id_num + 1);

                        iframe_element.deref_mut().size = Some(IFrameSize {
                            pipeline_id: pipeline_id,
                            subpage_id: subpage_id,
                        });
                        iframe_chan.send(HtmlDiscoveredIFrame((iframe_url,
                                                               subpage_id,
                                                               sandboxed)));
                    }
                }
                _ => {}
            }

            unsafe { element.to_hubbub_node() }
        },
        create_text: |data: ~str| {
            debug!("create text");
            // NOTE: tmp vars are workaround for lifetime issues. Both required.
            let tmp_borrow = doc_cell.borrow();
            let tmp = &*tmp_borrow;
            let text = Text::new(data, *tmp).root();
            unsafe { text.deref().to_hubbub_node() }
        },
        ref_node: |_| {},
        unref_node: |_| {},
        append_child: |parent: hubbub::NodeDataPtr, child: hubbub::NodeDataPtr| {
            unsafe {
                debug!("append child {:x} {:x}", parent, child);
                let mut child = from_hubbub_node(child).root();
                let mut parent: Root<Node> = from_hubbub_node(parent).root();
                assert!(parent.AppendChild(&mut *child).is_ok());
            }
            child
        },
        insert_before: |_parent, _child| {
            debug!("insert before");
            0u
        },
        remove_child: |_parent, _child| {
            debug!("remove child");
            0u
        },
        clone_node: |_node, deep| {
            debug!("clone node");
            if deep { error!("-- deep clone unimplemented"); }
            fail!("clone node unimplemented")
        },
        reparent_children: |_node, _new_parent| {
            debug!("reparent children");
            0u
        },
        get_parent: |_node, _element_only| {
            debug!("get parent");
            0u
        },
        has_children: |_node| {
            debug!("has children");
            false
        },
        form_associate: |_form, _node| {
            debug!("form associate");
        },
        add_attributes: |_node, _attributes| {
            debug!("add attributes");
        },
        set_quirks_mode: |mode| {
            debug!("set quirks mode");
            // NOTE: tmp vars are workaround for lifetime issues. Both required.
            let mut tmp_borrow = doc_cell.borrow_mut();
            let tmp = &mut *tmp_borrow;
            tmp.set_quirks_mode(mode);
        },
        encoding_change: |encname| {
            debug!("encoding change");
            // NOTE: tmp vars are workaround for lifetime issues. Both required.
            let mut tmp_borrow = doc_cell.borrow_mut();
            let tmp = &mut *tmp_borrow;
            tmp.set_encoding_name(encname);
        },
        complete_script: |script| {
            unsafe {
                let script: &JSRef<Element> = &*from_hubbub_node(script).root();
                match script.get_attribute(Null, "src").root() {
                    Some(src) => {
                        debug!("found script: {:s}", src.deref().Value());
                        let new_url = parse_url(src.deref().value_ref(), Some(url3.clone()));
                        js_chan2.send(JSTaskNewFile(new_url));
                    }
                    None => {
                        let mut data = vec!();
                        let scriptnode: &JSRef<Node> = NodeCast::from_ref(script);
                        debug!("iterating over children {:?}", scriptnode.first_child());
                        for child in scriptnode.children() {
                            debug!("child = {:?}", child);
                            let text: &JSRef<Text> = TextCast::to_ref(&child).unwrap();
                            data.push(text.deref().characterdata.data.to_str());  // FIXME: Bad copy.
                        }

                        debug!("script data = {:?}", data);
                        js_chan2.send(JSTaskNewInlineScript(data.concat(), url3.clone()));
                    }
                }
            }
            debug!("complete script");
        },
        complete_style: |_| {
            // style parsing is handled in element::notify_child_list_changed.
        },
    };
    parser.set_tree_handler(&tree_handler);
    debug!("set tree handler");

    debug!("loaded page");
    loop {
        match load_response.progress_port.recv() {
            Payload(data) => {
                debug!("received data");
                parser.parse_chunk(data.as_slice());
            }
            Done(Err(..)) => {
                fail!("Failed to load page URL {:s}", url.to_str());
            }
            Done(..) => {
                break;
            }
        }
    }

    debug!("finished parsing");
    css_chan.send(CSSTaskExit);
    js_chan.send(JSTaskExit);

    HtmlParserResult {
        discovery_port: discovery_port,
    }
}

