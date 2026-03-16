# Maestro Runtime API

This document is a standalone reference for the public runtime-facing
Maestro API.

Related API docs:

- [`api-common.md`](api-common.md)
- [`api-compile.md`](api-compile.md)

Canonical umbrella header:

- [`include/maestro/maestro.h`](../../include/maestro/maestro.h)

Direct runtime headers:

- [`include/maestro/runtime.h`](../../include/maestro/runtime.h)
- [`include/maestro/runtime-helpers.h`](../../include/maestro/runtime-helpers.h)
- [`include/maestro/common.h`](../../include/maestro/common.h)

## Model

The public runtime API is intentionally opaque:

- `maestro_ctx` is an opaque runtime context
- `maestro_value` is an opaque runtime value

Callers create and destroy these objects only through public functions.

## Common Types

Runtime code commonly uses:

- `maestro_int_t`
- `maestro_float_t`
- `maestro_output`
- `maestro_alloc_fn`
- `maestro_free_fn`

These are declared in
[`include/maestro/common.h`](../../include/maestro/common.h) and are
also summarized in [`api-common.md`](api-common.md).

## Runtime Context Lifecycle

### `maestro_ctx_new`

```c
maestro_ctx *maestro_ctx_new(void);
```

Creates a new runtime context with default output functions and default
allocation hooks.

### `maestro_ctx_free`

```c
void maestro_ctx_free(maestro_ctx *ctx);
```

Destroys a runtime context and releases runtime-owned context state.

## Runtime Context Configuration

### `maestro_ctx_set_output`

```c
int maestro_ctx_set_output(maestro_ctx *ctx, maestro_output print_fn,
                           maestro_output log_fn);
```

Sets the program-visible `print` and `log` sinks.

### `maestro_ctx_set_vm_logger`

```c
int maestro_ctx_set_vm_logger(maestro_ctx *ctx, maestro_output fn);
```

Sets the VM-internal logging sink.

### `maestro_ctx_set_allocator`

```c
int maestro_ctx_set_allocator(maestro_ctx *ctx, maestro_alloc_fn alloc,
                              maestro_free_fn dealloc);
```

Overrides the allocator pair used for runtime-owned allocations created
after the change.

### `maestro_ctx_set_capability`

```c
void maestro_ctx_set_capability(maestro_ctx *ctx, uint64_t vm_cap);
```

Sets the runtime capability bitmap used by validation.

### `maestro_ctx_set_log_flags`

```c
void maestro_ctx_set_log_flags(maestro_ctx *ctx, uint64_t flags);
```

Sets VM logging verbosity flags.

### `maestro_ctx_add_tool`

```c
int maestro_ctx_add_tool(maestro_ctx *ctx, const char *name, maestro_output fn);
```

Registers one external tool binding by name.

## Loading and Validation

### `maestro_load`

```c
int maestro_load(maestro_ctx *ctx, const void *src);
```

Loads a packed `.mstro` image into the runtime context. The runtime uses
the image zero-copy.

### `maestro_ctx_set_image_len`

```c
void maestro_ctx_set_image_len(maestro_ctx *ctx, size_t len);
```

Sets the loaded image size for stricter validation.

### `maestro_validate`

```c
uint64_t maestro_validate(maestro_ctx *ctx, FILE *err);
```

Validates the loaded image and current runtime bindings. Errors are
reported as bit flags and may also be written to `err`.

### `maestro_list_externals`

```c
size_t maestro_list_externals(maestro_ctx *ctx, const char ***names);
```

Returns the required external tool names recorded in the artifact.

## Program Execution

### `maestro_run`

```c
int maestro_run(maestro_ctx *ctx, const char *module_path,
                maestro_value **args, size_t argc,
                maestro_value **result);
```

Runs the program identified by `module_path`.

Input argument rules:

- `args` may be `NULL` only when `argc == 0`
- each input argument is a borrowed `maestro_value *`
- callers must keep the argument values alive and unchanged until
  `maestro_run` returns
- the runtime clones values that must escape into longer-lived runtime
  state

Result rules:

- `result` receives a runtime-allocated `maestro_value *`
- the caller owns that returned handle
- the caller must release it with `maestro_value_free(ctx, result_value)`

## Value Construction

These APIs create runtime values associated with a context.

### `maestro_value_new_invalid`

```c
maestro_value *maestro_value_new_invalid(maestro_ctx *ctx);
```

Creates an invalid value handle.

### `maestro_value_new_int`

```c
maestro_value *maestro_value_new_int(maestro_ctx *ctx, maestro_int_t v);
```

Creates an integer value.

### `maestro_value_new_float`

```c
maestro_value *maestro_value_new_float(maestro_ctx *ctx, maestro_float_t v);
```

Creates a float value.

### `maestro_value_new_bool`

```c
maestro_value *maestro_value_new_bool(maestro_ctx *ctx, bool v);
```

Creates a boolean value.

### `maestro_value_new_string`

```c
maestro_value *maestro_value_new_string(maestro_ctx *ctx, const char *s);
```

Creates a string value.

### `maestro_value_new_symbol`

```c
maestro_value *maestro_value_new_symbol(maestro_ctx *ctx, const char *s);
```

Creates a symbol value.

### `maestro_value_new_list`

```c
maestro_value *maestro_value_new_list(maestro_ctx *ctx);
```

Creates an empty list value.

### `maestro_value_new_json`

```c
maestro_value *maestro_value_new_json(maestro_ctx *ctx,
                                      const char *json_snippet);
```

Parses a static JSON snippet into a Maestro value. This is the public
object-construction path.

### `maestro_value_free`

```c
void maestro_value_free(maestro_ctx *ctx, maestro_value *v);
```

Releases a runtime-owned value handle.

## Value Helpers

### `maestro_list_push`

```c
int maestro_list_push(maestro_ctx *ctx, maestro_value *list, maestro_value *v);
```

Appends one value to a Maestro list.

The pushed value is conceptually copied into the list; callers still own
their original handle.

## Value Accessors

### `maestro_value_type`

```c
int maestro_value_type(const maestro_value *v);
```

Returns the runtime value type tag.

### `maestro_value_as_int`

```c
int maestro_value_as_int(const maestro_value *v, maestro_int_t *out);
```

Reads an integer value.

### `maestro_value_as_float`

```c
int maestro_value_as_float(const maestro_value *v, maestro_float_t *out);
```

Reads a float value.

### `maestro_value_as_bool`

```c
int maestro_value_as_bool(const maestro_value *v, bool *out);
```

Reads a boolean value.

### `maestro_value_as_string`

```c
const char *maestro_value_as_string(const maestro_value *v);
```

Returns the string contents for a string value, or `NULL` otherwise.

### `maestro_value_as_symbol`

```c
const char *maestro_value_as_symbol(const maestro_value *v);
```

Returns the symbol contents for a symbol value, or `NULL` otherwise.

### `maestro_value_list_len`

```c
size_t maestro_value_list_len(const maestro_value *v);
```

Returns the list length, or `0` for non-list values.

### `maestro_value_list_get`

```c
maestro_value *maestro_value_list_get(const maestro_value *v, size_t idx);
```

Returns a borrowed handle to the list element at `idx`, or `NULL` if the
value is not a list or the index is out of range.

## Notes

- Include [`include/maestro/maestro.h`](../../include/maestro/maestro.h)
  if you want the whole public API.
- Include only
  [`include/maestro/runtime.h`](../../include/maestro/runtime.h) and
  [`include/maestro/runtime-helpers.h`](../../include/maestro/runtime-helpers.h)
  if you want a narrower runtime-only surface.
- Shared constants and typedefs are covered in
  [`api-common.md`](api-common.md).
