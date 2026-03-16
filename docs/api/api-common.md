# Maestro Common API

This document is a standalone reference for the common public Maestro
API surface shared by compile-time and runtime APIs.

Related API docs:

- [`api-compile.md`](api-compile.md)
- [`api-runtime.md`](api-runtime.md)

Canonical headers:

- [`include/maestro/common.h`](../../include/maestro/common.h)
- [`include/maestro/maestro.h`](../../include/maestro/maestro.h)

## Purpose

`common.h` contains the shared public types, constants, enums, and
function typedefs used by both the compile and runtime APIs.

It does not expose concrete struct layouts for public opaque types.

## Numeric Types

### `maestro_int_t`

```c
typedef int64_t maestro_int_t;
```

The public integer type used by the API.

### `maestro_float_t`

```c
typedef float maestro_float_t;
```

The public floating-point type used by the API.

## Build and Artifact Constants

### `MAESTRO_MAGIC_STRING`

```c
#define MAESTRO_MAGIC_STRING "maestro"
```

The default string used to derive the default artifact magic.

### `MAESTRO_VERSION`

```c
#define MAESTRO_VERSION 1U
```

The public library/artifact version constant.

### `MAESTRO_DEFAULT_MAGIC`

```c
extern const uint8_t MAESTRO_DEFAULT_MAGIC[32];
```

The default 256-bit artifact magic used by the linker and loader.

## Status Codes

The common status codes are:

- `MAESTRO_OK`
- `MAESTRO_ERR_PARSE`
- `MAESTRO_ERR_LINK`
- `MAESTRO_ERR_LOAD`
- `MAESTRO_ERR_VALIDATE`
- `MAESTRO_ERR_RUNTIME`
- `MAESTRO_ERR_NOMEM`
- `MAESTRO_ERR_CAPABILITY`

These are shared across the public API.

## VM Logging Flags

The public VM logging flags are:

- `MAESTRO_VLOG_ERROR`
- `MAESTRO_VLOG_WARN`
- `MAESTRO_VLOG_INFO`
- `MAESTRO_VLOG_DEBUG`

These are used with the runtime context logging configuration.

## Validation Error Flags

The public validation flags are:

- `MAESTRO_VERR_IMAGE`
- `MAESTRO_VERR_OUTPUT`
- `MAESTRO_VERR_ALLOC`
- `MAESTRO_VERR_CAP`
- `MAESTRO_VERR_TOOL`

These are returned by the runtime validation API.

## Public Value Type Tags

The public runtime value type tags are:

- `MAESTRO_VAL_INVALID`
- `MAESTRO_VAL_INT`
- `MAESTRO_VAL_FLOAT`
- `MAESTRO_VAL_STRING`
- `MAESTRO_VAL_LIST`
- `MAESTRO_VAL_OBJECT`
- `MAESTRO_VAL_SYMBOL`
- `MAESTRO_VAL_BOOL`
- `MAESTRO_VAL_REF`
- `MAESTRO_VAL_STATE`
- `MAESTRO_VAL_MACRO`
- `MAESTRO_VAL_PROGRAM`
- `MAESTRO_VAL_BUILTIN`

These are primarily consumed through the runtime helper/accessor APIs.

## Public AST Node Type Tags

The public AST node type tags are:

- `MAESTRO_AST_INVALID`
- `MAESTRO_AST_INT`
- `MAESTRO_AST_FLOAT`
- `MAESTRO_AST_STRING`
- `MAESTRO_AST_IDENT`
- `MAESTRO_AST_SYMBOL`
- `MAESTRO_AST_FORM`
- `MAESTRO_AST_JSON`

These exist as part of the shared public type model even though the AST
containers themselves are opaque.

## Opaque Public Types

The common API forward-declares the opaque public types:

- `maestro_ctx`
- `maestro_value`
- `maestro_ast`
- `maestro_asts`
- `maestro_ast_node`
- `maestro_ast_kv`

These are defined internally, not in the public API.

## Function Typedefs

### `maestro_output`

```c
typedef int (*maestro_output)(maestro_ctx *ctx, const char *msg);
```

The public callback type used for:

- program-visible `print`
- program-visible `log`
- VM logging
- external tool bindings

### `maestro_alloc_fn`

```c
typedef void *(*maestro_alloc_fn)(size_t size);
```

The public allocator callback type.

### `maestro_free_fn`

```c
typedef void (*maestro_free_fn)(void *ptr);
```

The public deallocator callback type.

## See Also

- Compile-side API: [`api-compile.md`](api-compile.md)
- Runtime-side API: [`api-runtime.md`](api-runtime.md)
