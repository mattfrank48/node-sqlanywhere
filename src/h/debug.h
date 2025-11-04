#pragma once
#include <stdio.h>

// Make the global flag available to all files that include this header
extern bool g_debug_logging;

// Use (void*)this or (void*)conn_obj as a unique ID for the logged object
#define DEBUG_LOG(format, ...) \
    do { \
        if (g_debug_logging) { \
            fprintf(stderr, "[DEBUG %p] (%s:%d) " format "\n", (void*)this, __FILE__, __LINE__, ##__VA_ARGS__); \
            fflush(stderr); \
        } \
    } while (0)

// For workers, they need the conn_obj passed in
#define WORKER_LOG(conn_obj, format, ...) \
    do { \
        if (g_debug_logging) { \
            fprintf(stderr, "[DEBUG %p] (%s:%d) " format "\n", (conn_obj) ? (void*)conn_obj : NULL, __FILE__, __LINE__, ##__VA_ARGS__); \
            fflush(stderr); \
        } \
    } while (0)

// Specific logger for StmtObject
#define DEBUG_STMT_LOG(format, ...) \
    do { \
        if (g_debug_logging) { \
            fprintf(stderr, "[DEBUG Stmt %p] (%s:%d) " format "\n", (void*)this, __FILE__, __LINE__, ##__VA_ARGS__); \
            fflush(stderr); \
        } \
    } while (0)