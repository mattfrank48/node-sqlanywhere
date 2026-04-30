#include "h/async_workers.h"
#include <cmath>
#include "h/debug.h"

// Forward declaration
void prepareBindParams(Napi::Array params, std::vector<a_sqlany_bind_param>& bind_params, ExecuteData& param_data);

// --- RUNS ON BACKGROUND C++ THREAD ---
void fetchResultSet(a_sqlany_stmt* stmt, FetchResult& out_result) {
    out_result.num_cols = api.sqlany_num_cols(stmt);
    if (out_result.num_cols <= 0) {
        out_result.affected_rows = api.sqlany_affected_rows(stmt);
        return;
    }

    // MEMORY OPTIMIZATION: Store Metadata exactly once.
    out_result.meta.resize(out_result.num_cols);
    for (int i = 0; i < out_result.num_cols; i++) {
        a_sqlany_column_info info;
        api.sqlany_get_column_info(stmt, i, &info);
        out_result.meta[i].name = info.name;
        out_result.meta[i].type = info.type; 
    }

    while (api.sqlany_fetch_next(stmt)) {
        std::vector<FetchValue> row(out_result.num_cols);
        for (int i = 0; i < out_result.num_cols; i++) {
            a_sqlany_data_value val;
            api.sqlany_get_column(stmt, i, &val);

            row[i].is_null = *(val.is_null);

            if (!row[i].is_null) {
                switch(val.type) {
                    case A_BINARY:
                    case A_STRING:
                         row[i].string_val = std::string(val.buffer, *(val.length));
                         break;
                    case A_DOUBLE: row[i].double_val = *(double*)val.buffer; break;
                    case A_FLOAT: row[i].float_val = *(float*)val.buffer; break;
                    case A_VAL64: row[i].val64 = *(long long*)val.buffer; break;
                    case A_UVAL64: row[i].uval64 = *(unsigned long long*)val.buffer; break;
                    case A_VAL32: row[i].val32 = *(int*)val.buffer; break;
                    case A_UVAL32: row[i].uval32 = *(unsigned int*)val.buffer; break;
                    case A_VAL16: row[i].val16 = *(short*)val.buffer; break;
                    case A_UVAL16: row[i].uval16 = *(unsigned short*)val.buffer; break;
                    case A_VAL8: row[i].val8 = *(signed char*)val.buffer; break;
                    case A_UVAL8: row[i].uval8 = *(unsigned char*)val.buffer; break;
                    default: row[i].string_val = "Unsupported Type"; break;
                }
            }
        }
        out_result.rows.push_back(std::move(row));
    }
}

// --- RUNS ON NODE.JS MAIN THREAD ---
Napi::Value buildJSResult(Napi::Env env, const FetchResult& fetch_result) {
    if (fetch_result.num_cols <= 0) {
        return Napi::Number::New(env, fetch_result.affected_rows);
    }

    Napi::Array results = Napi::Array::New(env);
    for (uint32_t r = 0; r < fetch_result.rows.size(); r++) {
        Napi::Object row = Napi::Object::New(env);
        const auto& f_row = fetch_result.rows[r];

        for (uint32_t c = 0; c < f_row.size(); c++) {
            const FetchValue& val = f_row[c];
            const ColumnMeta& meta = fetch_result.meta[c]; // Grab metadata

            if (val.is_null) {
                row.Set(meta.name, env.Null());
            } else {
                switch(meta.type) { // Map using standard metadata type
                    case A_BINARY:
                        row.Set(meta.name, Napi::Buffer<char>::Copy(env, val.string_val.data(), val.string_val.size()));
                        break;
                    case A_STRING:
                        row.Set(meta.name, Napi::String::New(env, val.string_val.data(), val.string_val.size()));
                        break;
                    case A_DOUBLE: row.Set(meta.name, Napi::Number::New(env, val.double_val)); break;
                    case A_FLOAT: row.Set(meta.name, Napi::Number::New(env, val.float_val)); break;
                    case A_VAL64: row.Set(meta.name, Napi::Number::New(env, (double)val.val64)); break;
                    case A_UVAL64: row.Set(meta.name, Napi::Number::New(env, (double)val.uval64)); break;
                    case A_VAL32: row.Set(meta.name, Napi::Number::New(env, val.val32)); break;
                    case A_UVAL32: row.Set(meta.name, Napi::Number::New(env, val.uval32)); break;
                    case A_VAL16: row.Set(meta.name, Napi::Number::New(env, val.val16)); break;
                    case A_UVAL16: row.Set(meta.name, Napi::Number::New(env, val.uval16)); break;
                    case A_VAL8: row.Set(meta.name, Napi::Number::New(env, val.val8)); break;
                    case A_UVAL8: row.Set(meta.name, Napi::Number::New(env, val.uval8)); break;
                    default: row.Set(meta.name, Napi::String::New(env, val.string_val)); break;
                }
            }
        }
        results[r] = row;
    }
    return results;
}

Napi::Value buildResult(Napi::Env env, a_sqlany_stmt* stmt) {
    int num_cols = api.sqlany_num_cols(stmt);
    if (num_cols <= 0) {
        return Napi::Number::New(env, api.sqlany_affected_rows(stmt));
    }
    Napi::Array results = Napi::Array::New(env);
    uint32_t row_num = 0;
    while (api.sqlany_fetch_next(stmt)) {
        Napi::Object row = Napi::Object::New(env);
        for (int i = 0; i < num_cols; i++) {
            a_sqlany_column_info info;
            api.sqlany_get_column_info(stmt, i, &info);
            a_sqlany_data_value val;
            api.sqlany_get_column(stmt, i, &val);
            if (*val.is_null) {
                row.Set(info.name, env.Null());
            } else {
                switch(val.type) {
                    case A_BINARY:
                        row.Set(info.name, Napi::Buffer<char>::Copy(env, val.buffer, *val.length));
                        break;
                    case A_STRING: 
                        row.Set(info.name, Napi::String::New(env, val.buffer, *val.length)); 
                        break;
                    case A_DOUBLE: 
                        row.Set(info.name, Napi::Number::New(env, *(double*)val.buffer)); 
                        break;
                    case A_FLOAT:
                        row.Set(info.name, Napi::Number::New(env, *(float*)val.buffer)); 
                        break;
                    case A_VAL64: 
                        row.Set(info.name, Napi::Number::New(env, (double)(*(long long*)val.buffer))); 
                        break;
                    case A_UVAL64: 
                        row.Set(info.name, Napi::Number::New(env, (double)(*(unsigned long long*)val.buffer))); 
                        break;
                    case A_VAL32: 
                        row.Set(info.name, Napi::Number::New(env, *(int*)val.buffer)); 
                        break;
                    case A_UVAL32: 
                        row.Set(info.name, Napi::Number::New(env, *(unsigned int*)val.buffer)); 
                        break;
                    case A_VAL16: 
                        row.Set(info.name, Napi::Number::New(env, *(short*)val.buffer)); 
                        break;
                    case A_UVAL16: 
                        row.Set(info.name, Napi::Number::New(env, *(unsigned short*)val.buffer)); 
                        break;
                    case A_VAL8: 
                        row.Set(info.name, Napi::Number::New(env, *(signed char*)val.buffer)); 
                        break;
                    case A_UVAL8: 
                        row.Set(info.name, Napi::Number::New(env, *(unsigned char*)val.buffer)); 
                        break;
                    default: 
                        row.Set(info.name, Napi::String::New(env, "Unsupported Type"));
                }
            }
        }
        results[row_num++] = row;
    }
    return results;
}

// --- Helper: Prepare C++ bind parameters (shared logic) ---
void prepareBindParams(Napi::Array params, std::vector<a_sqlany_bind_param>& bind_params, ExecuteData& param_data) {
    for (uint32_t i = 0; i < params.Length(); i++) {
        a_sqlany_bind_param p;
        memset(&p, 0, sizeof(p));
        p.direction = DD_INPUT;
        Napi::Value val = params.Get(i);

        if (val.IsBuffer()) {
            Napi::Buffer<char> buffer = val.As<Napi::Buffer<char>>();
            size_t* len = new size_t(buffer.Length());
            char* buf = new char[*len];
            memcpy(buf, buffer.Data(), *len);
            
            p.value.buffer = buf;
            p.value.type = A_BINARY;
            p.value.length = len;
            param_data.addString(buf, len);
        } else if (val.IsString()) {
            std::string str = val.ToString().Utf8Value();
            size_t* len = new size_t(str.length());
            char* buf = new char[*len + 1];
            memcpy(buf, str.c_str(), *len + 1);
            
            p.value.buffer = buf;
            p.value.type = A_STRING;
            p.value.length = len;
            param_data.addString(buf, len);
        } else if (val.IsNumber()) {
            double num_val = val.ToNumber().DoubleValue();
            if (floor(num_val) == num_val && num_val < 9007199254740991.0 && num_val > -9007199254740991.0) { // Is a safe integer
                if (num_val >= -2147483648 && num_val <= 2147483647) {
                    int* int_val = new int((int)num_val);
                    p.value.buffer = (char*)int_val;
                    p.value.type = A_VAL32;
                    param_data.addInt(int_val);
                } else {
                    long long* ll_val = new long long((long long)num_val);
                    p.value.buffer = (char*)ll_val;
                    p.value.type = A_VAL64;
                    param_data.addLongLong(ll_val);
                }
            } else {
                double* dbl_val = new double(num_val);
                p.value.buffer = (char*)dbl_val;
                p.value.type = A_DOUBLE;
                param_data.addDouble(dbl_val);
            }
        } else if (val.IsNull() || val.IsUndefined()) {
             p.value.buffer = NULL;
             p.value.length = NULL; 
             p.value.type = A_INVALID_TYPE;
        }

        bind_params.push_back(p);
    }
}


ExecWorker::ExecWorker(Connection* c, const Napi::Function& cb, std::string s, Napi::Array p)
    : Napi::AsyncWorker(cb), conn_obj(c), sql(s), result(Env().Undefined()), error_msg("") {
    WORKER_LOG(conn_obj, "ExecWorker: CREATED. SQL: %s", sql.substr(0, 50).c_str());
    prepareBindParams(p, bind_params, param_data);
}
void ExecWorker::Execute() {
    WORKER_LOG(conn_obj, "ExecWorker: Execute: Entry");
    uv_mutex_lock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "ExecWorker: Execute: Mutex locked. conn: %p", conn_obj->conn);

    if (!conn_obj->conn) {
        WORKER_LOG(conn_obj, "ExecWorker: Execute: ERROR - Not connected.");
        error_msg = "Not connected.";
    } else {
        if (bind_params.empty()) {
            WORKER_LOG(conn_obj, "ExecWorker: Execute: Calling sqlany_execute_direct.");
            stmt_handle = api.sqlany_execute_direct(conn_obj->conn, sql.c_str());
            if (stmt_handle) {
                fetchResultSet(stmt_handle, fetch_result); // <--- FETCH BACKGROUND
            }
        } else {
            WORKER_LOG(conn_obj, "ExecWorker: Execute: Calling sqlany_prepare.");
            stmt_handle = api.sqlany_prepare(conn_obj->conn, sql.c_str());
            if(stmt_handle) {
                WORKER_LOG(conn_obj, "ExecWorker: Execute: sqlany_prepare OK. Binding %zu params.", bind_params.size());
                for (size_t i = 0; i < bind_params.size(); i++) {
                    if (!api.sqlany_bind_param(stmt_handle, i, &bind_params[i])) {
                        getErrorMsg(conn_obj->conn, error_msg);
                        WORKER_LOG(conn_obj, "ExecWorker: Execute: ERROR binding param %zu: %s", i, error_msg.c_str());
                        break;
                    }
                }
                if(error_msg.empty()) {
                    WORKER_LOG(conn_obj, "ExecWorker: Execute: Calling sqlany_execute.");
                    if (!api.sqlany_execute(stmt_handle)) {
                        getErrorMsg(conn_obj->conn, error_msg);
                        WORKER_LOG(conn_obj, "ExecWorker: Execute: ERROR executing prepared statement: %s", error_msg.c_str());
                    } else {
                        fetchResultSet(stmt_handle, fetch_result); // <--- FETCH BACKGROUND
                    }
                }
            }
        }
        if (!stmt_handle && error_msg.empty()) {
            getErrorMsg(conn_obj->conn, error_msg);
            WORKER_LOG(conn_obj, "ExecWorker: Execute: ERROR getting statement handle: %s", error_msg.c_str());
        }

        // Clean up the statement safely in the background
        if (stmt_handle) {
            WORKER_LOG(conn_obj, "ExecWorker: Execute: Freeing statement handle %p", (void*)stmt_handle);
            api.sqlany_free_stmt(stmt_handle);
            stmt_handle = nullptr; 
        }
    }
    uv_mutex_unlock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "ExecWorker: Execute: Mutex unlocked. Exiting.");
}

void ExecWorker::OnOK() {
    WORKER_LOG(conn_obj, "ExecWorker: OnOK: Entry. error: '%s'", error_msg.c_str());
    Napi::HandleScope scope(Env());

    if (!error_msg.empty()) {
        Callback().Call({Napi::Error::New(Env(), error_msg).Value()});
    } else {
        WORKER_LOG(conn_obj, "ExecWorker: OnOK: Building result.");
        result = buildJSResult(Env(), fetch_result); // <--- BUILD JAVASCRIPT OBJECTS
        Callback().Call({Env().Null(), result});
    }
    WORKER_LOG(conn_obj, "ExecWorker: OnOK: Exiting.");
}


ExecStmtWorker::ExecStmtWorker(StmtObject* s, const Napi::Function& cb, Napi::Array p)
    : Napi::AsyncWorker(cb), stmt_obj(s), result(Env().Undefined()), error_msg("") {
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "ExecStmtWorker: CREATED for stmt %p", (void*)stmt_obj);
    prepareBindParams(p, bind_params, param_data);
}
void ExecStmtWorker::Execute() {
    WORKER_LOG(NULL, "ExecStmtWorker: Execute: Entry for stmt %p", (void*)stmt_obj);
    
    if (stmt_obj == nullptr) {
        error_msg = "Statement is not valid (it may have been dropped).";
        return; 
    }

    // THREAD SAFETY: Capture the connection pointer BEFORE locking
    Connection* safe_conn = stmt_obj->connection;
    if (safe_conn == nullptr) {
        error_msg = "Statement connection is null (dropped).";
        return;
    }

    uv_mutex_lock(&safe_conn->conn_mutex);
    WORKER_LOG(safe_conn, "ExecStmtWorker: Execute: Mutex locked.");
    
    // THREAD SAFETY: Re-check that the statement wasn't dropped while we waited for the lock
    if (stmt_obj->connection != safe_conn || stmt_obj->sqlany_stmt == nullptr) {
        error_msg = "Statement was dropped while waiting for lock.";
        uv_mutex_unlock(&safe_conn->conn_mutex);
        return; 
    }

    for (size_t i = 0; i < bind_params.size(); i++) {
        if (!api.sqlany_bind_param(stmt_obj->sqlany_stmt, i, &bind_params[i])) {
            getErrorMsg(safe_conn->conn, error_msg);
            WORKER_LOG(safe_conn, "ExecStmtWorker: Execute: ERROR binding param %zu: %s", i, error_msg.c_str());
            break;
        }
    }

    if(error_msg.empty() && !api.sqlany_execute(stmt_obj->sqlany_stmt)) {
        getErrorMsg(safe_conn->conn, error_msg);
        WORKER_LOG(safe_conn, "ExecStmtWorker: Execute: ERROR executing: %s", error_msg.c_str());
    } else if (error_msg.empty()) {
        fetchResultSet(stmt_obj->sqlany_stmt, fetch_result);
    }

    uv_mutex_unlock(&safe_conn->conn_mutex);
    WORKER_LOG(safe_conn, "ExecStmtWorker: Execute: Mutex unlocked. Exiting.");
}

void ExecStmtWorker::OnOK() {
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "ExecStmtWorker: OnOK: Entry. error: '%s'", error_msg.c_str());
    Napi::HandleScope scope(Env());
    if (error_msg.empty()) {
        result = buildJSResult(Env(), fetch_result); // <--- BUILD JAVASCRIPT OBJECTS
        Callback().Call({Env().Null(), result});
    } else {
        Callback().Call({Napi::Error::New(Env(), error_msg).Value()});
    }
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "ExecStmtWorker: OnOK: Exiting.");
}


ConnectWorker::ConnectWorker(Connection* c, const Napi::Function& cb, std::string s)
    : Napi::AsyncWorker(cb), conn_obj(c), conn_str(s), error_msg("") {
    WORKER_LOG(conn_obj, "ConnectWorker: CREATED.");
}
void ConnectWorker::Execute() {
    WORKER_LOG(conn_obj, "ConnectWorker: Execute: Entry");
    uv_mutex_lock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "ConnectWorker: Execute: Mutex locked. Current conn: %p", conn_obj->conn);
    if (conn_obj->conn) {
        error_msg = "Connection already exists.";
        WORKER_LOG(conn_obj, "ConnectWorker: Execute: ERROR - Connection already exists.");
    }
    else {
        WORKER_LOG(conn_obj, "ConnectWorker: Execute: Calling sqlany_new_connection.");
        conn_obj->conn = api.sqlany_new_connection();
        WORKER_LOG(conn_obj, "ConnectWorker: Execute: New connection handle: %p", conn_obj->conn);
        if (!api.sqlany_connect(conn_obj->conn, conn_str.c_str())) {
            getErrorMsg(conn_obj->conn, error_msg);
            WORKER_LOG(conn_obj, "ConnectWorker: Execute: ERROR connecting: %s", error_msg.c_str());
            api.sqlany_free_connection(conn_obj->conn);
            conn_obj->conn = NULL;
            WORKER_LOG(conn_obj, "ConnectWorker: Execute: Connection freed due to error. conn set to NULL.");
        } else {
            openConnections++;
            WORKER_LOG(conn_obj, "ConnectWorker: Execute: Connection successful.");
        }
    }
    uv_mutex_unlock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "ConnectWorker: Execute: Mutex unlocked. Exiting.");
}
void ConnectWorker::OnOK() {
    WORKER_LOG(conn_obj, "ConnectWorker: OnOK: Entry. error: '%s'", error_msg.c_str());
    Napi::HandleScope scope(Env());
    if (!error_msg.empty()) { Callback().Call({Napi::Error::New(Env(), error_msg).Value()}); }
    else { Callback().Call({Env().Null()}); }
    WORKER_LOG(conn_obj, "ConnectWorker: OnOK: Exiting.");
}

NoParamsWorker::NoParamsWorker(Connection* c, const Napi::Function& cb, Task t)
    : Napi::AsyncWorker(cb), conn_obj(c), task(t), error_msg("") {
    const char* task_name = (t == Task::Commit) ? "Commit" : (t == Task::Rollback) ? "Rollback" : "Disconnect";
    WORKER_LOG(conn_obj, "NoParamsWorker: CREATED. Task: %s", task_name);
}
void NoParamsWorker::Execute() {
    const char* task_name = (task == Task::Commit) ? "Commit" : (task == Task::Rollback) ? "Rollback" : "Disconnect";
    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Entry. Task: %s", task_name);
    
    uv_mutex_lock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Mutex locked. conn: %p. Task: %s", conn_obj->conn, task_name);
    
    if (!conn_obj->conn && task != Task::Disconnect) {
        error_msg = "Not connected.";
        WORKER_LOG(conn_obj, "NoParamsWorker: Execute: ERROR - Not connected. Task: %s", task_name);
    }
    else {
        bool success = false;
        switch(task) {
            case Task::Commit:
                WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Calling sqlany_commit.");
                success = api.sqlany_commit(conn_obj->conn);
                break;
            case Task::Rollback:
                WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Calling sqlany_rollback.");
                success = api.sqlany_rollback(conn_obj->conn);
                break;
            case Task::Disconnect:
                WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Disconnect task started.");
                if(conn_obj->conn) {
                    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Calling cleanupStmts.");
                    conn_obj->cleanupStmts();
                    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Calling sqlany_disconnect.");
                    api.sqlany_disconnect(conn_obj->conn);
                    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Calling sqlany_free_connection.");
                    api.sqlany_free_connection(conn_obj->conn);
                    conn_obj->conn = NULL;
                    openConnections--;
                    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Connection freed. conn set to NULL.");
                } else {
                    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Already disconnected (conn is NULL).");
                }
                success = true;
                break;
        }
        if (!success) {
            getErrorMsg(conn_obj->conn, error_msg);
            WORKER_LOG(conn_obj, "NoParamsWorker: Execute: ERROR performing task %s: %s", task_name, error_msg.c_str());
        }
    }
    uv_mutex_unlock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "NoParamsWorker: Execute: Mutex unlocked. Exiting. Task: %s", task_name);
}
void NoParamsWorker::OnOK() {
    const char* task_name = (task == Task::Commit) ? "Commit" : (task == Task::Rollback) ? "Rollback" : "Disconnect";
    WORKER_LOG(conn_obj, "NoParamsWorker: OnOK: Entry. Task: %s, error: '%s'", task_name, error_msg.c_str());
    Napi::HandleScope scope(Env());
    if (!error_msg.empty()) { Callback().Call({Napi::Error::New(Env(), error_msg).Value()}); }
    else { Callback().Call({Env().Null()}); }
    WORKER_LOG(conn_obj, "NoParamsWorker: OnOK: Exiting. Task: %s", task_name);
}

PrepareWorker::PrepareWorker(Connection* c, const Napi::Function& cb, std::string s)
    : Napi::AsyncWorker(cb), conn_obj(c), sql(s), error_msg("") {
    WORKER_LOG(conn_obj, "PrepareWorker: CREATED. SQL: %s", sql.substr(0, 50).c_str());
}
void PrepareWorker::Execute() {
    WORKER_LOG(conn_obj, "PrepareWorker: Execute: Entry");
    uv_mutex_lock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "PrepareWorker: Execute: Mutex locked. conn: %p", conn_obj->conn);
    if (!conn_obj->conn) {
        error_msg = "Not connected.";
        WORKER_LOG(conn_obj, "PrepareWorker: Execute: ERROR - Not connected.");
    } else {
        WORKER_LOG(conn_obj, "PrepareWorker: Execute: Calling sqlany_prepare.");
        stmt_handle = api.sqlany_prepare(conn_obj->conn, sql.c_str());
        if (!stmt_handle) {
            getErrorMsg(conn_obj->conn, error_msg);
            WORKER_LOG(conn_obj, "PrepareWorker: Execute: ERROR preparing statement: %s", error_msg.c_str());
        } else {
            WORKER_LOG(conn_obj, "PrepareWorker: Execute: sqlany_prepare OK. stmt_handle: %p", (void*)stmt_handle);
        }
    }
    uv_mutex_unlock(&conn_obj->conn_mutex);
    WORKER_LOG(conn_obj, "PrepareWorker: Execute: Mutex unlocked. Exiting.");
}
void PrepareWorker::OnOK() {
    WORKER_LOG(conn_obj, "PrepareWorker: OnOK: Entry. stmt_handle: %p, error: '%s'", (void*)stmt_handle, error_msg.c_str());
    Napi::HandleScope scope(Env());
    if (stmt_handle) {
        Napi::Object stmt_obj = StmtObject::constructor.New({});
        StmtObject* unwrapped = Napi::ObjectWrap<StmtObject>::Unwrap(stmt_obj);
        unwrapped->sqlany_stmt = stmt_handle;
        
        WORKER_LOG(conn_obj, "PrepareWorker: OnOK: Creating new StmtObject %p and setting its connection.", (void*)unwrapped);
        unwrapped->setConnection(conn_obj);
        
        Callback().Call({Env().Null(), stmt_obj});
    } else {
        Callback().Call({Napi::Error::New(Env(), error_msg).Value()});
    }
    WORKER_LOG(conn_obj, "PrepareWorker: OnOK: Exiting.");
}

DropStmtWorker::DropStmtWorker(StmtObject* s, const Napi::Function& cb)
    : Napi::AsyncWorker(cb), stmt_obj(s) {
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "DropStmtWorker: CREATED for stmt %p", (void*)stmt_obj);
}
void DropStmtWorker::Execute() {
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "DropStmtWorker: Execute: Entry for stmt %p", (void*)stmt_obj);
    
    if (stmt_obj == nullptr) {
         WORKER_LOG(NULL, "DropStmtWorker: Execute: StmtObject is already null.");
         return;
    }
    
    // cleanup() is safe to call even if connection is null
    stmt_obj->cleanup(); 
    
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "DropStmtWorker: Execute: Exiting for stmt %p", (void*)stmt_obj);
}
void DropStmtWorker::OnOK() {
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "DropStmtWorker: OnOK: Entry for stmt %p", (void*)stmt_obj);
    Callback().Call({Env().Null()});
}

GetMoreResultsWorker::GetMoreResultsWorker(StmtObject* s, const Napi::Function& cb)
    : Napi::AsyncWorker(cb), stmt_obj(s), result(Env().Undefined()), error_msg(""), has_more_results(false) {
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "GetMoreResultsWorker: CREATED for stmt %p", (void*)stmt_obj);
}
void GetMoreResultsWorker::Execute() {
    WORKER_LOG(NULL, "GetMoreResultsWorker: Execute: Entry for stmt %p", (void*)stmt_obj);
    if (stmt_obj == nullptr) {
        error_msg = "Statement is not valid (it may have been dropped).";
        return; 
    }

    // THREAD SAFETY: Capture the connection pointer BEFORE locking
    Connection* safe_conn = stmt_obj->connection;
    if (safe_conn == nullptr) {
        error_msg = "Statement connection is null (dropped).";
        return;
    }

    uv_mutex_lock(&safe_conn->conn_mutex);
    WORKER_LOG(safe_conn, "GetMoreResultsWorker: Execute: Mutex locked.");
    
    // THREAD SAFETY: Re-check that the statement wasn't dropped while we waited for the lock
    if (stmt_obj->connection != safe_conn || stmt_obj->sqlany_stmt == nullptr) {
        error_msg = "Statement was dropped while waiting for lock.";
        uv_mutex_unlock(&safe_conn->conn_mutex);
        return;
    }
    
    WORKER_LOG(safe_conn, "GetMoreResultsWorker: Execute: Calling sqlany_get_next_result.");
    has_more_results = api.sqlany_get_next_result(stmt_obj->sqlany_stmt);
    
    if (!has_more_results) {
        char buffer[SACAPI_ERROR_SIZE];
        int rc = api.sqlany_error(safe_conn->conn, buffer, sizeof(buffer));
        if (rc != 0 && rc != 100) { 
            error_msg = buffer;
            WORKER_LOG(safe_conn, "GetMoreResultsWorker: Execute: ERROR getting next result: %s", error_msg.c_str());
        }
    } else {
        WORKER_LOG(safe_conn, "GetMoreResultsWorker: Execute: More results found.");
        fetchResultSet(stmt_obj->sqlany_stmt, fetch_result);
    }

    uv_mutex_unlock(&safe_conn->conn_mutex);
    WORKER_LOG(safe_conn, "GetMoreResultsWorker: Execute: Mutex unlocked. Exiting.");
}

void GetMoreResultsWorker::OnOK() {
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "GetMoreResultsWorker: OnOK: Entry. has_more: %s, error: '%s'", has_more_results ? "true" : "false", error_msg.c_str());
    Napi::HandleScope scope(Env());
    if (!error_msg.empty()) {
        Callback().Call({Napi::Error::New(Env(), error_msg).Value()});
    } else if (has_more_results) {
        result = buildJSResult(Env(), fetch_result); // <--- BUILD JAVASCRIPT OBJECTS
        Callback().Call({Env().Null(), result});
    } else {
        Callback().Call({Env().Null(), Env().Undefined()});
    }
    WORKER_LOG(stmt_obj ? stmt_obj->connection : NULL, "GetMoreResultsWorker: OnOK: Exiting.");
}