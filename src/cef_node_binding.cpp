#include <napi.h>
#include <map>
#include <mutex>
#include <string>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "include/cef_client.h"
#include "include/wrapper/cef_helpers.h"

using namespace Napi;

class SimpleCefApp : public CefApp {
 public:
  SimpleCefApp() = default;
  IMPLEMENT_REFCOUNTING(SimpleCefApp);
};

class SimpleClient : public CefClient, public CefLifeSpanHandler {
 public:
  SimpleClient(int id) : id_(id) {}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    std::lock_guard<std::mutex> lk(global_mutex);
    browser_map[browser->GetIdentifier()] = browser;
    printf("[cef_node] Browser created id=%d (node id=%d)\n", browser->GetIdentifier(), id_);
  }

  bool DoClose(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    return false;
  }

  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    std::lock_guard<std::mutex> lk(global_mutex);
    browser_map.erase(browser->GetIdentifier());
    printf("[cef_node] Browser closing id=%d\n", browser->GetIdentifier());
  }

  static std::map<int, CefRefPtr<CefBrowser>> browser_map;
  static std::mutex global_mutex;

 private:
  int id_;
  IMPLEMENT_REFCOUNTING(SimpleClient);
};

std::map<int, CefRefPtr<CefBrowser>> SimpleClient::browser_map;
std::mutex SimpleClient::global_mutex;

static std::map<std::string, CefRefPtr<CefRequestContext>> g_contexts;
static std::mutex g_contexts_mutex;
static bool g_cef_initialized = false;
static CefRefPtr<SimpleCefApp> g_cef_app;

std::string make_key_from_path(const std::string& p) {
  return p;
}

Value InitCEF(const CallbackInfo& info) {
  Env env = info.Env();

  if (g_cef_initialized) {
    return Boolean::New(env, true);
  }

  if (info.Length() > 0 && !info[0].IsObject()) {
    TypeError::New(env, "options must be an object").ThrowAsJavaScriptException();
    return env.Null();
  }

  int remote_debugging_port = 9222;
  bool multi_threaded = true;

  if (info.Length() > 0) {
    Object opts = info[0].As<Object>();
    if (opts.Has("remoteDebuggingPort")) {
      remote_debugging_port = opts.Get("remoteDebuggingPort").As<Number>().Int32Value();
    }
    if (opts.Has("multiThreadedMessageLoop")) {
      multi_threaded = opts.Get("multiThreadedMessageLoop").As<Boolean>().Value();
    }
  }

  CefMainArgs main_args(0, nullptr);
  g_cef_app = new SimpleCefApp();

  CefSettings settings;
  settings.no_sandbox = true;
  settings.remote_debugging_port = remote_debugging_port;
  settings.windowless_rendering_enabled = true;
  settings.multi_threaded_message_loop = multi_threaded ? 1 : 0;

  int exit_code = CefExecuteProcess(main_args, g_cef_app.get(), nullptr);
  if (exit_code >= 0) {
    Error::New(env, "CEF subprocess launched unexpectedly").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!CefInitialize(main_args, settings, g_cef_app.get(), nullptr)) {
    Error::New(env, "Failed to initialize CEF").ThrowAsJavaScriptException();
    return env.Null();
  }

  g_cef_initialized = true;
  printf("[cef_node] CEF initialized (remote_debugging_port=%d, multi_threaded=%d)\n", remote_debugging_port, (int)multi_threaded);
  return Boolean::New(env, true);
}

Value CreateContext(const CallbackInfo& info) {
  Env env = info.Env();
  if (!g_cef_initialized) {
    Error::New(env, "CEF not initialized; call initCEF() first").ThrowAsJavaScriptException();
    return env.Null();
  }
  if (info.Length() < 1 || !info[0].IsString()) {
    TypeError::New(env, "cachePath (string) required").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string cache_path = info[0].As<String>().Utf8Value();
  CefRequestContextSettings rcs;
  CefString(&rcs.cache_path) = cache_path;

  CefRefPtr<CefRequestContext> ctx = CefRequestContext::CreateContext(rcs, nullptr);

  std::string id = make_key_from_path(cache_path);

  {
    std::lock_guard<std::mutex> lk(g_contexts_mutex);
    g_contexts[id] = ctx;
  }

  printf("[cef_node] Created request context id=%s cache_path=%s\n", id.c_str(), cache_path.c_str());
  return String::New(env, id);
}

Value CreateBrowser(const CallbackInfo& info) {
  Env env = info.Env();
  if (!g_cef_initialized) {
    Error::New(env, "CEF not initialized; call initCEF() first").ThrowAsJavaScriptException();
    return env.Null();
  }
  if (info.Length() < 2) {
    TypeError::New(env, "createBrowser(contextId, url[, windowless])").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string ctxId = info[0].As<String>().Utf8Value();
  std::string url = info[1].As<String>().Utf8Value();
  bool windowless = true;
  if (info.Length() >= 3 && info[2].IsBoolean()) windowless = info[2].As<Boolean>().Value();

  CefRefPtr<CefRequestContext> ctx;
  {
    std::lock_guard<std::mutex> lk(g_contexts_mutex);
    auto it = g_contexts.find(ctxId);
    if (it == g_contexts.end()) {
      Error::New(env, "Unknown contextId").ThrowAsJavaScriptException();
      return env.Null();
    }
    ctx = it->second;
  }

  CefBrowserSettings browser_settings;
  CefWindowInfo window_info;
#if defined(OS_WIN)
  if (windowless) {
    window_info.SetAsWindowless(NULL, true);
  } else {
    window_info.SetAsPopup(NULL, "CEF Popup");
  }
#else
  window_info.windowless_rendering_enabled = windowless;
#endif

  static int nextClientId = 1;
  int myClientId = nextClientId++;
  CefRefPtr<SimpleClient> client = new SimpleClient(myClientId);

  CefBrowserHost::CreateBrowser(window_info, client.get(), url, browser_settings, nullptr, ctx);

  return Number::New(env, myClientId);
}

Value GetContexts(const CallbackInfo& info) {
  Env env = info.Env();
  Array arr = Array::New(env);
  std::lock_guard<std::mutex> lk(g_contexts_mutex);
  uint32_t idx = 0;
  for (auto &kv : g_contexts) {
    arr.Set(idx++, String::New(env, kv.first));
  }
  return arr;
}

Value DisposeContext(const CallbackInfo& info) {
  Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    TypeError::New(env, "contextId required").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string id = info[0].As<String>().Utf8Value();
  {
    std::lock_guard<std::mutex> lk(g_contexts_mutex);
    auto it = g_contexts.find(id);
    if (it == g_contexts.end()) {
      Error::New(env, "Unknown contextId").ThrowAsJavaScriptException();
      return env.Null();
    }
    g_contexts.erase(it);
  }
  printf("[cef_node] Disposed context %s\n", id.c_str());
  return Boolean::New(env, true);
}

Value ShutdownCEF(const CallbackInfo& info) {
  Env env = info.Env();
  if (!g_cef_initialized) {
    return Boolean::New(env, true);
  }
  CefShutdown();
  g_cef_initialized = false;
  printf("[cef_node] CEF shutdown\n");
  return Boolean::New(env, true);
}

Object InitAll(Napi::Env env, Object exports) {
  exports.Set("initCEF", Function::New(env, InitCEF));
  exports.Set("createContext", Function::New(env, CreateContext));
  exports.Set("createBrowser", Function::New(env, CreateBrowser));
  exports.Set("getContexts", Function::New(env, GetContexts));
  exports.Set("disposeContext", Function::New(env, DisposeContext));
  exports.Set("shutdownCEF", Function::New(env, ShutdownCEF));
  return exports;
}

NODE_API_MODULE(cef_node_binding, InitAll)
