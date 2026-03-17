#ifndef MAESTRO_COMMON_H
#define MAESTRO_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t maestro_int_t;
typedef float maestro_float_t;

#define MAESTRO_MAGIC_STRING "maestro"
#define MAESTRO_VERSION 1U

enum {
        MAESTRO_OK = 0,
        MAESTRO_ERR_PARSE = 1,
        MAESTRO_ERR_LINK = 2,
        MAESTRO_ERR_LOAD = 3,
        MAESTRO_ERR_VALIDATE = 4,
        MAESTRO_ERR_RUNTIME = 5,
        MAESTRO_ERR_NOMEM = 6,
        MAESTRO_ERR_CAPABILITY = 7,
};

enum {
        MAESTRO_VLOG_ERROR = 1ULL << 0,
        MAESTRO_VLOG_WARN = 1ULL << 1,
        MAESTRO_VLOG_INFO = 1ULL << 2,
        MAESTRO_VLOG_DEBUG = 1ULL << 3,
};

enum {
        MAESTRO_VERR_IMAGE = 1ULL << 0,
        MAESTRO_VERR_OUTPUT = 1ULL << 1,
        MAESTRO_VERR_ALLOC = 1ULL << 2,
        MAESTRO_VERR_CAP = 1ULL << 3,
        MAESTRO_VERR_FN = 1ULL << 4,
};

enum maestro_value_type {
        MAESTRO_VAL_INVALID = 0,
        MAESTRO_VAL_INT,
        MAESTRO_VAL_FLOAT,
        MAESTRO_VAL_STRING,
        MAESTRO_VAL_LIST,
        MAESTRO_VAL_OBJECT,
        MAESTRO_VAL_SYMBOL,
        MAESTRO_VAL_BOOL,
        MAESTRO_VAL_REF,
        MAESTRO_VAL_STATE,
        MAESTRO_VAL_MACRO,
        MAESTRO_VAL_PROGRAM,
        MAESTRO_VAL_BUILTIN,
};

enum {
        MAESTRO_VALUE_F_BORROWED = 1U << 0,
};

enum maestro_ast_node_type {
        MAESTRO_AST_INVALID = 0,
        MAESTRO_AST_INT,
        MAESTRO_AST_FLOAT,
        MAESTRO_AST_STRING,
        MAESTRO_AST_IDENT,
        MAESTRO_AST_SYMBOL,
        MAESTRO_AST_FORM,
        MAESTRO_AST_JSON,
};

typedef struct maestro_ctx maestro_ctx;
typedef struct maestro_value maestro_value;
typedef struct maestro_ast maestro_ast;
typedef struct maestro_asts maestro_asts;
typedef struct maestro_ast_node maestro_ast_node;
typedef struct maestro_ast_kv maestro_ast_kv;

typedef int (*maestro_output)(maestro_ctx *ctx, const char *msg);
typedef int (*maestro_fn)(maestro_ctx *ctx, maestro_value **args, size_t argc,
                          maestro_value **result);
typedef void *(*maestro_alloc_fn)(size_t size);
typedef void (*maestro_free_fn)(void *ptr);

extern const uint8_t MAESTRO_DEFAULT_MAGIC[32];

#ifdef __cplusplus
}
#endif

#endif
