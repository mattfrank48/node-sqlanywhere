#pragma once
#include <stdio.h>

// Make the global flag available to all files that include this header
extern bool g_debug_logging;

// Using (void*)this as a unique ID for the Connection object
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
            fprintf(stderr, "[DEBUG %p] (%s:%d) " format "\n", (void*)conn_obj, __FILE__, __LINE__, ##__VA_ARGS__); \
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