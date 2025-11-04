// ***************************************************************************
// Copyright (c) 2021 SAP SE or an SAP affiliate company. All rights reserved.
// ***************************************************************************
#include "napi.h"
#include "h/sqlany_utils.h"
#include "h/connection.h"
#include "h/stmt.h"

// Global variables
SQLAnywhereInterface api;
unsigned openConnections = 0;
uv_mutex_t api_mutex;

// Global flag for debug logging, off by default
bool g_debug_logging = false;

// N-API function to set the debug logging flag from JavaScript
Napi::Value SetDebugLogging(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBoolean()) {
        Napi::TypeError::New(env, "Boolean expected")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    g_debug_logging = info[0].As<Napi::Boolean>().Value();
    return env.Undefined();
}

// Addon entry point
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    uv_mutex_init(&api_mutex);

    if (!sqlany_initialize_interface(&api, NULL)) {
        Napi::Error::New(env, "Could not initialize the SQL Anywhere C API interface.")
            .ThrowAsJavaScriptException();
        return exports;
    }
    if (!api.sqlany_init("node-sqlanywhere", SQLANY_API_VERSION_4, NULL)) {
         Napi::Error::New(env, "Failed to initialize the SQL Anywhere C API.")
            .ThrowAsJavaScriptException();
        return exports;
    }

    Connection::Init(env, exports);
    StmtObject::Init(env, exports);
    
    // Create a top-level createConnection function for convenience
    Napi::Function conn_constructor = exports.Get("Connection").As<Napi::Function>();
    exports.Set("createConnection", conn_constructor);

    // Export the new debug logging function
    exports.Set("debugLogging", Napi::Function::New(env, SetDebugLogging));

    return exports;
}

NODE_API_MODULE(sqlanywhere, Init)