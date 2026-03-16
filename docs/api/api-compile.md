# Maestro Compile API

This document is a standalone reference for the public parse/link side
of the Maestro API.

Related API docs:

- [`api-common.md`](api-common.md)
- [`api-runtime.md`](api-runtime.md)

Canonical umbrella header:

- [`include/maestro/maestro.h`](../../include/maestro/maestro.h)

Direct compile headers:

- [`include/maestro/compile.h`](../../include/maestro/compile.h)
- [`include/maestro/common.h`](../../include/maestro/common.h)

## Model

The public compile API is intentionally small.

- `maestro_asts` is an opaque container for parsed modules
- parsing works on explicit file paths only
- directory discovery is a tool responsibility, not a library
  responsibility

`build/maestroc` is the standalone compiler frontend that performs file
collection and then calls into this API.

## AST Collection Lifecycle

### `maestro_asts_new`

```c
maestro_asts *maestro_asts_new(void);
```

Creates a new opaque AST collection.

### `maestro_asts_free`

```c
void maestro_asts_free(maestro_asts *asts);
```

Destroys an AST collection and all parsed module data owned by it.

## Parsing

### `maestro_parse_file`

```c
int maestro_parse_file(maestro_asts *dest, FILE *err, const char *src);
```

Parses one `.mstr` source file and appends it to `dest`.

Rules:

- `dest` must be a valid AST collection
- syntax errors are reported to `err`
- the return value is `0` on success and non-zero on failure

### `maestro_parse_list`

```c
int maestro_parse_list(maestro_asts *dest, FILE *err, const char **srcs, int src_cnt);
```

Parses an explicit list of source files and appends them to `dest`.

Rules:

- parsing is file-list-based only
- the library does not walk directories
- diagnostics are written directly to `err`

## Linking

### `maestro_link`

```c
int maestro_link(FILE *dest, maestro_asts *src);
```

Links the parsed modules into one packed `.mstro` artifact using default
artifact header settings.

### `maestro_link_ex`

```c
int maestro_link_ex(FILE *dest, maestro_asts *src, const uint8_t *magic,
                    uint64_t capability);
```

Links the parsed modules into one packed `.mstro` artifact while
overriding the default artifact header fields.

Arguments:

- `magic`: optional 32-byte artifact magic; pass `NULL` to use the
  default
- `capability`: required-capabilities bitmap to write into the artifact

## Related Constants

These are declared in
[`include/maestro/common.h`](../../include/maestro/common.h):

- `MAESTRO_MAGIC_STRING`
- `MAESTRO_DEFAULT_MAGIC`
- `MAESTRO_VERSION`
- status codes such as `MAESTRO_ERR_PARSE` and `MAESTRO_ERR_LINK`

## Notes

- The compile API works on source files and opaque AST collections.
- The runtime API works on packed `.mstro` images.
- If you want the full public surface, include
  [`include/maestro/maestro.h`](../../include/maestro/maestro.h).
- Shared constants and typedefs are covered in
  [`api-common.md`](api-common.md).
