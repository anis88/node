// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "node.h"
#include "handle_wrap.h"

#include <stdlib.h>

namespace node {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

static Cached<String> change_sym;
static Cached<String> onchange_sym;
static Cached<String> rename_sym;

class FSEventWrap: public HandleWrap {
 public:
  static void Initialize(Handle<Object> target);
  static void New(const FunctionCallbackInfo<Value>& args);
  static void Start(const FunctionCallbackInfo<Value>& args);
  static void Close(const FunctionCallbackInfo<Value>& args);

 private:
  explicit FSEventWrap(Handle<Object> object);
  virtual ~FSEventWrap();

  static void OnEvent(uv_fs_event_t* handle, const char* filename, int events,
    int status);

  uv_fs_event_t handle_;
  bool initialized_;
};


FSEventWrap::FSEventWrap(Handle<Object> object)
    : HandleWrap(object, reinterpret_cast<uv_handle_t*>(&handle_)) {
  initialized_ = false;
}


FSEventWrap::~FSEventWrap() {
  assert(initialized_ == false);
}


void FSEventWrap::Initialize(Handle<Object> target) {
  HandleScope scope(node_isolate);

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->InstanceTemplate()->SetInternalFieldCount(1);
  t->SetClassName(FIXED_ONE_BYTE_STRING(node_isolate, "FSEvent"));

  NODE_SET_PROTOTYPE_METHOD(t, "start", Start);
  NODE_SET_PROTOTYPE_METHOD(t, "close", Close);

  target->Set(FIXED_ONE_BYTE_STRING(node_isolate, "FSEvent"), t->GetFunction());

  change_sym = FIXED_ONE_BYTE_STRING(node_isolate, "change");
  onchange_sym = FIXED_ONE_BYTE_STRING(node_isolate, "onchange");
  rename_sym = FIXED_ONE_BYTE_STRING(node_isolate, "rename");
}


void FSEventWrap::New(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);
  assert(args.IsConstructCall());
  new FSEventWrap(args.This());
}


void FSEventWrap::Start(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);

  FSEventWrap* wrap;
  NODE_UNWRAP(args.This(), FSEventWrap, wrap);

  if (args.Length() < 1 || !args[0]->IsString()) {
    return ThrowTypeError("Bad arguments");
  }

  String::Utf8Value path(args[0]);

  int err = uv_fs_event_init(uv_default_loop(),
                             &wrap->handle_,
                             *path,
                             OnEvent,
                             0);
  if (err == 0) {
    // Check for persistent argument
    if (!args[1]->IsTrue()) {
      uv_unref(reinterpret_cast<uv_handle_t*>(&wrap->handle_));
    }
    wrap->initialized_ = true;
  }

  args.GetReturnValue().Set(err);
}


void FSEventWrap::OnEvent(uv_fs_event_t* handle, const char* filename,
    int events, int status) {
  HandleScope scope(node_isolate);
  Handle<String> eventStr;

  FSEventWrap* wrap = static_cast<FSEventWrap*>(handle->data);

  assert(wrap->persistent().IsEmpty() == false);

  // We're in a bind here. libuv can set both UV_RENAME and UV_CHANGE but
  // the Node API only lets us pass a single event to JS land.
  //
  // The obvious solution is to run the callback twice, once for each event.
  // However, since the second event is not allowed to fire if the handle is
  // closed after the first event, and since there is no good way to detect
  // closed handles, that option is out.
  //
  // For now, ignore the UV_CHANGE event if UV_RENAME is also set. Make the
  // assumption that a rename implicitly means an attribute change. Not too
  // unreasonable, right? Still, we should revisit this before v1.0.
  if (status) {
    eventStr = String::Empty(node_isolate);
  } else if (events & UV_RENAME) {
    eventStr = rename_sym;
  } else if (events & UV_CHANGE) {
    eventStr = change_sym;
  } else {
    assert(0 && "bad fs events flag");
    abort();
  }

  Handle<Value> argv[3] = {
    Integer::New(status, node_isolate),
    eventStr,
    Null(node_isolate)
  };

  if (filename != NULL) {
    argv[2] = OneByteString(node_isolate, filename);
  }

  MakeCallback(wrap->object(), onchange_sym, ARRAY_SIZE(argv), argv);
}


void FSEventWrap::Close(const FunctionCallbackInfo<Value>& args) {
  HandleScope scope(node_isolate);

  FSEventWrap* wrap;
  NODE_UNWRAP_NO_ABORT(args.This(), FSEventWrap, wrap);

  if (wrap == NULL || wrap->initialized_ == false) return;
  wrap->initialized_ = false;

  HandleWrap::Close(args);
}

}  // namespace node

NODE_MODULE(node_fs_event_wrap, node::FSEventWrap::Initialize)
