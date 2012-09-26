// Copyright (c) 2011, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#define VERBOSE 0

#if VERBOSE
  static bool gDebugLog = true;
#else
  static bool gDebugLog = false;
#endif

#define DEBUGLOG if (gDebugLog) fprintf
#define IGNORE_DEBUGGER "BREAKPAD_IGNORE_DEBUGGER"

#import "common/mac/SimpleStringDictionary.h"

#import "client/mac/crash_generation/ConfigFile.h"
#import "client/mac/sender/uploader.h"
#import "client/mac/handler/exception_handler.h"
#import "client/mac/handler/minidump_generator.h"
#import "client/ios/Breakpad.h"
#import "client/ios/handler/ios_exception_minidump_generator.h"
#import "client/mac/handler/protected_memory_allocator.h"

#import <sys/stat.h>
#import <sys/sysctl.h>

#import <Foundation/Foundation.h>

using google_breakpad::ConfigFile;
using google_breakpad::EnsureDirectoryPathExists;
using google_breakpad::KeyValueEntry;
using google_breakpad::SimpleStringDictionary;
using google_breakpad::SimpleStringDictionaryIterator;

//=============================================================================
// We want any memory allocations which are used by breakpad during the
// exception handling process (after a crash has happened) to be read-only
// to prevent them from being smashed before a crash occurs.  Unfortunately
// we cannot protect against smashes to our exception handling thread's
// stack.
//
// NOTE: Any memory allocations which are not used during the exception
// handling process may be allocated in the normal ways.
//
// The ProtectedMemoryAllocator class provides an Allocate() method which
// we'll using in conjunction with placement operator new() to control
// allocation of C++ objects.  Note that we don't use operator delete()
// but instead call the objects destructor directly:  object->~ClassName();
//
ProtectedMemoryAllocator *gMasterAllocator = NULL;
ProtectedMemoryAllocator *gKeyValueAllocator = NULL;
ProtectedMemoryAllocator *gBreakpadAllocator = NULL;

// Mutex for thread-safe access to the key/value dictionary used by breakpad.
// It's a global instead of an instance variable of Breakpad
// since it can't live in a protected memory area.
pthread_mutex_t gDictionaryMutex;

//=============================================================================
// Stack-based object for thread-safe access to a memory-protected region.
// It's assumed that normally the memory block (allocated by the allocator)
// is protected (read-only).  Creating a stack-based instance of
// ProtectedMemoryLocker will unprotect this block after taking the lock.
// Its destructor will first re-protect the memory then release the lock.
class ProtectedMemoryLocker {
public:
  // allocator may be NULL, in which case no Protect() or Unprotect() calls
  // will be made, but a lock will still be taken
  ProtectedMemoryLocker(pthread_mutex_t *mutex,
                        ProtectedMemoryAllocator *allocator)
  : mutex_(mutex), allocator_(allocator) {
    // Lock the mutex
    assert(pthread_mutex_lock(mutex_) == 0);

    // Unprotect the memory
    if (allocator_ ) {
      allocator_->Unprotect();
    }
  }

  ~ProtectedMemoryLocker() {
    // First protect the memory
    if (allocator_) {
      allocator_->Protect();
    }

    // Then unlock the mutex
    assert(pthread_mutex_unlock(mutex_) == 0);
  };

private:
  //  Keep anybody from ever creating one of these things not on the stack.
  ProtectedMemoryLocker() { }
  ProtectedMemoryLocker(const ProtectedMemoryLocker&);
  ProtectedMemoryLocker & operator=(ProtectedMemoryLocker&);

  pthread_mutex_t           *mutex_;
  ProtectedMemoryAllocator  *allocator_;
};

//=============================================================================
class Breakpad {
 public:
  // factory method
  static Breakpad *Create(NSDictionary *parameters) {
    // Allocate from our special allocation pool
    Breakpad *breakpad =
      new (gBreakpadAllocator->Allocate(sizeof(Breakpad)))
        Breakpad();

    if (!breakpad)
      return NULL;

    if (!breakpad->Initialize(parameters)) {
      // Don't use operator delete() here since we allocated from special pool
      breakpad->~Breakpad();
      return NULL;
    }

    return breakpad;
  }

  ~Breakpad();

  void SetKeyValue(NSString *key, NSString *value);
  NSString *KeyValue(NSString *key);
  void RemoveKeyValue(NSString *key);
  NSString *NextCrashReportToUpload();
  void UploadNextReport();
  void UploadData(NSData *data, NSString *name,
                  NSDictionary *server_parameters);
  NSDictionary *GenerateReport(NSDictionary *server_parameters);

 private:
  Breakpad()
    : handler_(NULL),
      config_params_(NULL) {}

  bool Initialize(NSDictionary *parameters);

  bool ExtractParameters(NSDictionary *parameters);

  // Dispatches to HandleMinidump()
  static bool HandleMinidumpCallback(const char *dump_dir,
                                     const char *minidump_id,
                                     void *context, bool succeeded);

  bool HandleMinidump(const char *dump_dir,
                      const char *minidump_id);

  // NSException handler
  static void UncaughtExceptionHandler(NSException *exception);

  // Handle an uncaught NSException.
  void HandleUncaughtException(NSException *exception);

  // Since ExceptionHandler (w/o namespace) is defined as typedef in OSX's
  // MachineExceptions.h, we have to explicitly name the handler.
  google_breakpad::ExceptionHandler *handler_; // The actual handler (STRONG)

  SimpleStringDictionary  *config_params_; // Create parameters (STRONG)

  ConfigFile config_file_;

  // A static reference to the current Breakpad instance. Used for handling
  // NSException.
  static Breakpad *current_breakpad_;
};

Breakpad *Breakpad::current_breakpad_ = NULL;

#pragma mark -
#pragma mark Helper functions

//=============================================================================
// Helper functions

//=============================================================================
static BOOL IsDebuggerActive() {
  BOOL result = NO;
  NSUserDefaults *stdDefaults = [NSUserDefaults standardUserDefaults];

  // We check both defaults and the environment variable here

  BOOL ignoreDebugger = [stdDefaults boolForKey:@IGNORE_DEBUGGER];

  if (!ignoreDebugger) {
    char *ignoreDebuggerStr = getenv(IGNORE_DEBUGGER);
    ignoreDebugger =
        (ignoreDebuggerStr ? strtol(ignoreDebuggerStr, NULL, 10) : 0) != 0;
  }

  if (!ignoreDebugger) {
    pid_t pid = getpid();
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    int mibSize = sizeof(mib) / sizeof(int);
    size_t actualSize;

    if (sysctl(mib, mibSize, NULL, &actualSize, NULL, 0) == 0) {
      struct kinfo_proc *info = (struct kinfo_proc *)malloc(actualSize);

      if (info) {
        // This comes from looking at the Darwin xnu Kernel
        if (sysctl(mib, mibSize, info, &actualSize, NULL, 0) == 0)
          result = (info->kp_proc.p_flag & P_TRACED) ? YES : NO;

        free(info);
      }
    }
  }

  return result;
}

//=============================================================================
bool Breakpad::HandleMinidumpCallback(const char *dump_dir,
                                      const char *minidump_id,
                                      void *context, bool succeeded) {
  Breakpad *breakpad = (Breakpad *)context;

  // If our context is damaged or something, just return false to indicate that
  // the handler should continue without us.
  if (!breakpad || !succeeded)
    return false;

  return breakpad->HandleMinidump(dump_dir, minidump_id);
}

//=============================================================================
void Breakpad::UncaughtExceptionHandler(NSException *exception) {
  NSSetUncaughtExceptionHandler(NULL);
  if (current_breakpad_) {
    current_breakpad_->HandleUncaughtException(exception);
  }
}

//=============================================================================
#pragma mark -

//=============================================================================
bool Breakpad::Initialize(NSDictionary *parameters) {
  // Initialize
  current_breakpad_ = this;
  config_params_ = NULL;
  handler_ = NULL;

  // Gather any user specified parameters
  if (!ExtractParameters(parameters)) {
    return false;
  }

  // Check for debugger
  if (IsDebuggerActive()) {
    DEBUGLOG(stderr, "Debugger is active:  Not installing handler\n");
    return true;
  }

  // Create the handler (allocating it in our special protected pool)
  handler_ =
      new (gBreakpadAllocator->Allocate(
          sizeof(google_breakpad::ExceptionHandler)))
          google_breakpad::ExceptionHandler(
              config_params_->GetValueForKey(BREAKPAD_DUMP_DIRECTORY),
              0, &HandleMinidumpCallback, this, true, 0);
  NSSetUncaughtExceptionHandler(&Breakpad::UncaughtExceptionHandler);
  return true;
}

//=============================================================================
Breakpad::~Breakpad() {
  NSSetUncaughtExceptionHandler(NULL);
  current_breakpad_ = NULL;
  // Note that we don't use operator delete() on these pointers,
  // since they were allocated by ProtectedMemoryAllocator objects.
  //
  if (config_params_) {
    config_params_->~SimpleStringDictionary();
  }

  if (handler_)
    handler_->~ExceptionHandler();
}

//=============================================================================
bool Breakpad::ExtractParameters(NSDictionary *parameters) {
  NSString *serverType = [parameters objectForKey:@BREAKPAD_SERVER_TYPE];
  NSString *display = [parameters objectForKey:@BREAKPAD_PRODUCT_DISPLAY];
  NSString *product = [parameters objectForKey:@BREAKPAD_PRODUCT];
  NSString *version = [parameters objectForKey:@BREAKPAD_VERSION];
  NSString *urlStr = [parameters objectForKey:@BREAKPAD_URL];
  NSString *vendor =
      [parameters objectForKey:@BREAKPAD_VENDOR];
  NSString *dumpSubdirectory =
      [parameters objectForKey:@BREAKPAD_DUMP_DIRECTORY];

  NSDictionary *serverParameters =
      [parameters objectForKey:@BREAKPAD_SERVER_PARAMETER_DICT];

  if (!product)
    product = [parameters objectForKey:@"CFBundleName"];

  if (!display) {
    display = [parameters objectForKey:@"CFBundleDisplayName"];
    if (!display) {
      display = product;
    }
  }

  if (!version)
    version = [parameters objectForKey:@"CFBundleVersion"];

  if (!vendor) {
    vendor = @"Vendor not specified";
  }

  if (!dumpSubdirectory) {
    NSString *cachePath =
        [NSSearchPathForDirectoriesInDomains(NSCachesDirectory,
                                             NSUserDomainMask,
                                             YES)
            objectAtIndex:0];
    dumpSubdirectory =
        [cachePath stringByAppendingPathComponent:@kDefaultLibrarySubdirectory];

    EnsureDirectoryPathExists(dumpSubdirectory);
  }

  // The product, version, and URL are required values.
  if (![product length]) {
    DEBUGLOG(stderr, "Missing required product key.\n");
    return false;
  }

  if (![version length]) {
    DEBUGLOG(stderr, "Missing required version key.\n");
    return false;
  }

  if (![urlStr length]) {
    DEBUGLOG(stderr, "Missing required URL key.\n");
    return false;
  }

  config_params_ =
      new (gKeyValueAllocator->Allocate(sizeof(SimpleStringDictionary)) )
        SimpleStringDictionary();

  SimpleStringDictionary &dictionary = *config_params_;

  dictionary.SetKeyValue(BREAKPAD_SERVER_TYPE,     [serverType UTF8String]);
  dictionary.SetKeyValue(BREAKPAD_PRODUCT_DISPLAY, [display UTF8String]);
  dictionary.SetKeyValue(BREAKPAD_PRODUCT,         [product UTF8String]);
  dictionary.SetKeyValue(BREAKPAD_VERSION,         [version UTF8String]);
  dictionary.SetKeyValue(BREAKPAD_URL,             [urlStr UTF8String]);
  dictionary.SetKeyValue(BREAKPAD_VENDOR,          [vendor UTF8String]);
  dictionary.SetKeyValue(BREAKPAD_DUMP_DIRECTORY,
                         [dumpSubdirectory UTF8String]);

  struct timeval tv;
  gettimeofday(&tv, NULL);
  char timeStartedString[32];
  sprintf(timeStartedString, "%zd", tv.tv_sec);
  dictionary.SetKeyValue(BREAKPAD_PROCESS_START_TIME, timeStartedString);

  if (serverParameters) {
    // For each key-value pair, call BreakpadAddUploadParameter()
    NSEnumerator *keyEnumerator = [serverParameters keyEnumerator];
    NSString *aParameter;
    while ((aParameter = [keyEnumerator nextObject])) {
      BreakpadAddUploadParameter(this, aParameter,
				 [serverParameters objectForKey:aParameter]);
    }
  }
  return true;
}

//=============================================================================
void Breakpad::SetKeyValue(NSString *key, NSString *value) {
  // We allow nil values. This is the same as removing the keyvalue.
  if (!config_params_ || !key)
    return;

  config_params_->SetKeyValue([key UTF8String], [value UTF8String]);
}

//=============================================================================
NSString *Breakpad::KeyValue(NSString *key) {
  if (!config_params_ || !key)
    return nil;

  const char *value = config_params_->GetValueForKey([key UTF8String]);
  return value ? [NSString stringWithUTF8String:value] : nil;
}

//=============================================================================
void Breakpad::RemoveKeyValue(NSString *key) {
  if (!config_params_ || !key) return;

  config_params_->RemoveKey([key UTF8String]);
}

//=============================================================================
NSString *Breakpad::NextCrashReportToUpload() {
  NSString *directory = KeyValue(@BREAKPAD_DUMP_DIRECTORY);
  if (!directory)
    return nil;
  NSArray *dirContents = [[NSFileManager defaultManager]
      contentsOfDirectoryAtPath:directory error:nil];
  NSArray *configs = [dirContents filteredArrayUsingPredicate:[NSPredicate
      predicateWithFormat:@"self BEGINSWITH 'Config-'"]];
  NSString *config = [configs lastObject];
  if (!config)
    return nil;
  return [NSString stringWithFormat:@"%@/%@", directory, config];
}

//=============================================================================
void Breakpad::UploadNextReport() {
  NSString *configFile = NextCrashReportToUpload();
  if (configFile) {
    Uploader *uploader = [[[Uploader alloc]
        initWithConfigFile:[configFile UTF8String]] autorelease];
    if (uploader)
      [uploader report];
  }
}

//=============================================================================
void Breakpad::UploadData(NSData *data, NSString *name,
                          NSDictionary *server_parameters) {
  NSMutableDictionary *config = [NSMutableDictionary dictionary];

  SimpleStringDictionaryIterator it(*config_params_);
  while (const KeyValueEntry *next = it.Next()) {
    [config setValue:[NSString stringWithUTF8String:next->GetValue()]
              forKey:[NSString stringWithUTF8String:next->GetKey()]];
  }

  Uploader *uploader =
      [[[Uploader alloc] initWithConfig:config] autorelease];
  for (NSString *key in server_parameters) {
    [uploader addServerParameter:[server_parameters objectForKey:key]
                          forKey:key];
  }
  [uploader uploadData:data name:name];
}

//=============================================================================
NSDictionary *Breakpad::GenerateReport(NSDictionary *server_parameters) {
  NSString *dumpDirAsNSString = KeyValue(@BREAKPAD_DUMP_DIRECTORY);
  if (!dumpDirAsNSString)
    return nil;
  const char *dumpDir = [dumpDirAsNSString UTF8String];

  google_breakpad::MinidumpGenerator generator(mach_task_self(),
                                               MACH_PORT_NULL);
  std::string dumpId;
  std::string dumpFilename = generator.UniqueNameInDirectory(dumpDir, &dumpId);
  bool success = generator.Write(dumpFilename.c_str());
  if (!success)
    return nil;

  SimpleStringDictionary params = *config_params_;
  for (NSString *key in server_parameters) {
    params.SetKeyValue([key UTF8String],
                       [[server_parameters objectForKey:key] UTF8String]);
  }
  ConfigFile config_file;
  config_file.WriteFile(dumpDir, &params, dumpDir, dumpId.c_str());

  // Handle results.
  NSMutableDictionary *result = [NSMutableDictionary dictionary];
  NSString *dumpFullPath = [dumpDirAsNSString stringByAppendingPathComponent:
      [NSString stringWithUTF8String:dumpFilename.c_str()]];
  [result setValue:dumpFullPath
            forKey:@BREAKPAD_OUTPUT_DUMP_FILE];
  [result setValue:[NSString stringWithUTF8String:config_file.GetFilePath()]
            forKey:@BREAKPAD_OUTPUT_CONFIG_FILE];
  return result;
}

//=============================================================================
bool Breakpad::HandleMinidump(const char *dump_dir,
                              const char *minidump_id) {
  DEBUGLOG(stderr, "Breakpad: a minidump has been created.\n");

  config_file_.WriteFile(dump_dir,
                         config_params_,
                         dump_dir,
                         minidump_id);

  // Return true here to indicate that we've processed things as much as we
  // want.
  return true;
}

//=============================================================================
void Breakpad::HandleUncaughtException(NSException *exception) {
  // Generate the minidump.
  google_breakpad::IosExceptionMinidumpGenerator generator(exception);
  const char *minidump_path =
      config_params_->GetValueForKey(BREAKPAD_DUMP_DIRECTORY);
  std::string minidump_id;
  std::string minidump_filename = generator.UniqueNameInDirectory(minidump_path,
                                                                  &minidump_id);
  generator.Write(minidump_filename.c_str());

  // Copy the config params and our custom parameter. This is necessary for 2
  // reasons:
  // 1- config_params_ is protected.
  // 2- If the application crash while trying to handle this exception, a usual
  //    report will be generated. This report must not contain these special
  //    keys.
  SimpleStringDictionary params = *config_params_;
  params.SetKeyValue(BREAKPAD_SERVER_PARAMETER_PREFIX "type", "exception");
  params.SetKeyValue(BREAKPAD_SERVER_PARAMETER_PREFIX "exceptionName",
                     [[exception name] UTF8String]);
  params.SetKeyValue(BREAKPAD_SERVER_PARAMETER_PREFIX "exceptionReason",
                     [[exception reason] UTF8String]);

  // And finally write the config file.
  ConfigFile config_file;
  config_file.WriteFile(minidump_path,
                        &params,
                        minidump_path,
                        minidump_id.c_str());
}

//=============================================================================

#pragma mark -
#pragma mark Public API

//=============================================================================
BreakpadRef BreakpadCreate(NSDictionary *parameters) {
  try {
    // This is confusing.  Our two main allocators for breakpad memory are:
    //    - gKeyValueAllocator for the key/value memory
    //    - gBreakpadAllocator for the Breakpad, ExceptionHandler, and other
    //      breakpad allocations which are accessed at exception handling time.
    //
    // But in order to avoid these two allocators themselves from being smashed,
    // we'll protect them as well by allocating them with gMasterAllocator.
    //
    // gMasterAllocator itself will NOT be protected, but this doesn't matter,
    // since once it does its allocations and locks the memory, smashes to
    // itself don't affect anything we care about.
    gMasterAllocator =
        new ProtectedMemoryAllocator(sizeof(ProtectedMemoryAllocator) * 2);

    gKeyValueAllocator =
        new (gMasterAllocator->Allocate(sizeof(ProtectedMemoryAllocator)))
            ProtectedMemoryAllocator(sizeof(SimpleStringDictionary));

    // Create a mutex for use in accessing the SimpleStringDictionary
    int mutexResult = pthread_mutex_init(&gDictionaryMutex, NULL);
    if (mutexResult == 0) {

      // With the current compiler, gBreakpadAllocator is allocating 1444 bytes.
      // Let's round up to the nearest page size.
      //
      int breakpad_pool_size = 4096;

      /*
       sizeof(Breakpad)
       + sizeof(google_breakpad::ExceptionHandler)
       + sizeof( STUFF ALLOCATED INSIDE ExceptionHandler )
       */

      gBreakpadAllocator =
          new (gMasterAllocator->Allocate(sizeof(ProtectedMemoryAllocator)))
              ProtectedMemoryAllocator(breakpad_pool_size);

      // Stack-based autorelease pool for Breakpad::Create() obj-c code.
      NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
      Breakpad *breakpad = Breakpad::Create(parameters);

      if (breakpad) {
        // Make read-only to protect against memory smashers
        gMasterAllocator->Protect();
        gKeyValueAllocator->Protect();
        gBreakpadAllocator->Protect();
        // Can uncomment this line to figure out how much space was actually
        // allocated using this allocator
        //     printf("gBreakpadAllocator allocated size = %d\n",
        //         gBreakpadAllocator->GetAllocatedSize() );
        [pool release];
        return (BreakpadRef)breakpad;
      }

      [pool release];
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadCreate() : error\n");
  }

  if (gKeyValueAllocator) {
    gKeyValueAllocator->~ProtectedMemoryAllocator();
    gKeyValueAllocator = NULL;
  }

  if (gBreakpadAllocator) {
    gBreakpadAllocator->~ProtectedMemoryAllocator();
    gBreakpadAllocator = NULL;
  }

  delete gMasterAllocator;
  gMasterAllocator = NULL;

  return NULL;
}

//=============================================================================
void BreakpadRelease(BreakpadRef ref) {
  try {
    Breakpad *breakpad = (Breakpad *)ref;

    if (gMasterAllocator) {
      gMasterAllocator->Unprotect();
      gKeyValueAllocator->Unprotect();
      gBreakpadAllocator->Unprotect();

      breakpad->~Breakpad();

      // Unfortunately, it's not possible to deallocate this stuff
      // because the exception handling thread is still finishing up
      // asynchronously at this point...  OK, it could be done with
      // locks, etc.  But since BreakpadRelease() should usually only
      // be called right before the process exits, it's not worth
      // deallocating this stuff.
#if 0
      gKeyValueAllocator->~ProtectedMemoryAllocator();
      gBreakpadAllocator->~ProtectedMemoryAllocator();
      delete gMasterAllocator;

      gMasterAllocator = NULL;
      gKeyValueAllocator = NULL;
      gBreakpadAllocator = NULL;
#endif

      pthread_mutex_destroy(&gDictionaryMutex);
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadRelease() : error\n");
  }
}

//=============================================================================
void BreakpadSetKeyValue(BreakpadRef ref, NSString *key, NSString *value) {
  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad && key && gKeyValueAllocator) {
      ProtectedMemoryLocker locker(&gDictionaryMutex, gKeyValueAllocator);

      breakpad->SetKeyValue(key, value);
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadSetKeyValue() : error\n");
  }
}

void BreakpadAddUploadParameter(BreakpadRef ref,
                                NSString *key,
                                NSString *value) {
  // The only difference, internally, between an upload parameter and
  // a key value one that is set with BreakpadSetKeyValue is that we
  // prepend the keyname with a special prefix.  This informs the
  // crash sender that the parameter should be sent along with the
  // POST of the crash dump upload.
  try {
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad && key && gKeyValueAllocator) {
      ProtectedMemoryLocker locker(&gDictionaryMutex, gKeyValueAllocator);

      NSString *prefixedKey = [@BREAKPAD_SERVER_PARAMETER_PREFIX
				stringByAppendingString:key];
      breakpad->SetKeyValue(prefixedKey, value);
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadSetKeyValue() : error\n");
  }
}

void BreakpadRemoveUploadParameter(BreakpadRef ref,
                                   NSString *key) {
  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad && key && gKeyValueAllocator) {
      ProtectedMemoryLocker locker(&gDictionaryMutex, gKeyValueAllocator);

      NSString *prefixedKey = [NSString stringWithFormat:@"%@%@",
                                        @BREAKPAD_SERVER_PARAMETER_PREFIX, key];
      breakpad->RemoveKeyValue(prefixedKey);
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadRemoveKeyValue() : error\n");
  }
}
//=============================================================================
NSString *BreakpadKeyValue(BreakpadRef ref, NSString *key) {
  NSString *value = nil;

  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (!breakpad || !key || !gKeyValueAllocator)
      return nil;

    ProtectedMemoryLocker locker(&gDictionaryMutex, gKeyValueAllocator);

    value = breakpad->KeyValue(key);
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadKeyValue() : error\n");
  }

  return value;
}

//=============================================================================
void BreakpadRemoveKeyValue(BreakpadRef ref, NSString *key) {
  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad && key && gKeyValueAllocator) {
      ProtectedMemoryLocker locker(&gDictionaryMutex, gKeyValueAllocator);

      breakpad->RemoveKeyValue(key);
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadRemoveKeyValue() : error\n");
  }
}

//=============================================================================
bool BreakpadHasCrashReportToUpload(BreakpadRef ref) {
  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad) {
       return breakpad->NextCrashReportToUpload() != 0;
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadHasCrashReportToUpload() : error\n");
  }
  return false;
}

//=============================================================================
void BreakpadUploadNextReport(BreakpadRef ref) {
  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad) {
       breakpad->UploadNextReport();
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadUploadNextReport() : error\n");
  }
}

//=============================================================================
void BreakpadUploadData(BreakpadRef ref, NSData *data, NSString *name,
                        NSDictionary *server_parameters) {
  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad) {
      breakpad->UploadData(data, name, server_parameters);
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadUploadData() : error\n");
  }
}

//=============================================================================
NSDictionary *BreakpadGenerateReport(BreakpadRef ref,
                                     NSDictionary *server_parameters) {
  try {
    // Not called at exception time
    Breakpad *breakpad = (Breakpad *)ref;

    if (breakpad) {
      return breakpad->GenerateReport(server_parameters);
    } else {
      return nil;
    }
  } catch(...) {    // don't let exceptions leave this C API
    fprintf(stderr, "BreakpadGenerateReport() : error\n");
    return nil;
  }
}
