#pragma once
#include <uv.h>
#include "napi.h"
#include "connection.h"
#include "stmt.h"
#include "execute_data.h"
#include <vector>
#include <string>

// --- Optimized Native C++ Fetch Structures ---
struct ColumnMeta {
    std::string name;
    a_sqlany_data_type type;
};

struct FetchValue {
    bool is_null;
    std::string string_val;
    double double_val;
    float float_val;
    long long val64;
    unsigned long long uval64;
    int val32;
    unsigned int uval32;
    short val16;
    unsigned short uval16;
    signed char val8;
    unsigned char uval8;
};

struct FetchResult {
    int affected_rows = 0;
    int num_cols = 0;
    std::vector<ColumnMeta> meta;             // Stored ONCE per query
    std::vector<std::vector<FetchValue>> rows; // Contains only values
};

// --- Standalone Helpers ---
void fetchResultSet(a_sqlany_stmt* stmt, FetchResult& out_result);
Napi::Value buildJSResult(Napi::Env env, const FetchResult& fetch_result);

// --- Worker Classes ---
class ConnectWorker;
class NoParamsWorker;
class ExecWorker;
class PrepareWorker;
class ExecStmtWorker;
class DropStmtWorker;
class GetMoreResultsWorker;

class ExecWorker : public Napi::AsyncWorker {
public:
    ExecWorker(Connection* conn_obj, const Napi::Function& callback, std::string sql, Napi::Array params);
    void Execute();
    void OnOK();
private:
    Connection* conn_obj;
    std::string sql;
    Napi::Value result;
    std::string error_msg;
    a_sqlany_stmt* stmt_handle = nullptr;
    std::vector<a_sqlany_bind_param> bind_params;
    ExecuteData param_data;
    FetchResult fetch_result;
};

class ExecStmtWorker : public Napi::AsyncWorker {
public:
    ExecStmtWorker(StmtObject* stmt_obj, const Napi::Function& callback, Napi::Array params);
    void Execute();
    void OnOK();
private:
    StmtObject* stmt_obj;
    Napi::Value result;
    std::string error_msg;
    std::vector<a_sqlany_bind_param> bind_params;
    ExecuteData param_data;
    FetchResult fetch_result;
};

class ConnectWorker : public Napi::AsyncWorker {
public:
    ConnectWorker(Connection* conn_obj, const Napi::Function& callback, std::string conn_str);
    void Execute();
    void OnOK();
private:
    Connection* conn_obj;
    std::string conn_str;
    std::string error_msg;
};

class NoParamsWorker : public Napi::AsyncWorker {
public:
    enum class Task { Commit, Rollback, Disconnect };
    NoParamsWorker(Connection* conn_obj, const Napi::Function& callback, Task task);
    void Execute();
    void OnOK();
private:
    Connection* conn_obj;
    Task task;
    std::string error_msg;
};

class PrepareWorker : public Napi::AsyncWorker {
public:
    PrepareWorker(Connection* conn_obj, const Napi::Function& callback, std::string sql);
    void Execute();
    void OnOK();
private:
    Connection* conn_obj;
    std::string sql;
    a_sqlany_stmt* stmt_handle = nullptr;
    std::string error_msg;
};

class DropStmtWorker : public Napi::AsyncWorker {
public:
    DropStmtWorker(StmtObject* stmt_obj, const Napi::Function& callback);
    void Execute();
    void OnOK();
private:
    StmtObject* stmt_obj;
};

class GetMoreResultsWorker : public Napi::AsyncWorker {
public:
    GetMoreResultsWorker(StmtObject* stmt_obj, const Napi::Function& callback);
    void Execute();
    void OnOK();
private:
    StmtObject* stmt_obj;
    Napi::Value result;
    std::string error_msg;
    bool has_more_results = false;
    FetchResult fetch_result;
};