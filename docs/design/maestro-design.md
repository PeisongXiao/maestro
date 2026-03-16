# Maestro Library Design

This document describes the implementation-facing design of the
Maestro library and toolchain. Surface language semantics live in
[`docs/design/language-specs.md`](language-specs.md).

## Public Surface

The canonical public entry point is:

- [`include/maestro/maestro.h`](../../include/maestro/maestro.h)

That umbrella header includes:

- [`include/maestro/common.h`](../../include/maestro/common.h)
- [`include/maestro/compile.h`](../../include/maestro/compile.h)
- [`include/maestro/runtime.h`](../../include/maestro/runtime.h)
- [`include/maestro/runtime-helpers.h`](../../include/maestro/runtime-helpers.h)

The public API is intentionally opaque. Embedders do not manipulate
runtime or parser structs directly. The concrete definitions for
`maestro_ctx`, `maestro_value`, `maestro_ast`, and `maestro_asts` stay
internal under [`src/`](../../src/).

## High-Level Pipeline

Maestro is split into a frontend and a runtime:

1. parse explicit `.mstr` files into opaque AST collections
2. link those ASTs into one standalone packed `.mstro` artifact
3. load the artifact into an opaque runtime context
4. validate the loaded image and host bindings
5. run a module from the artifact

The current public entrypoints are:

```c
maestro_asts *maestro_asts_new(void);
void maestro_asts_free(maestro_asts *asts);

int maestro_parse_file(maestro_asts *dest, FILE *err, const char *src);
int maestro_parse_list(maestro_asts *dest, FILE *err, const char **srcs, int src_cnt);
int maestro_link(FILE *dest, maestro_asts *src);
int maestro_link_ex(FILE *dest, maestro_asts *src, const uint8_t *magic, uint64_t capability);

maestro_ctx *maestro_ctx_new(void);
void maestro_ctx_free(maestro_ctx *ctx);
int maestro_load(maestro_ctx *ctx, const void *src);
void maestro_ctx_set_image_len(maestro_ctx *ctx, size_t len);
uint64_t maestro_validate(maestro_ctx *ctx, FILE *err);
int maestro_run(maestro_ctx *ctx, const char *module_path,
		maestro_value **args, size_t argc, maestro_value **result);
size_t maestro_list_externals(maestro_ctx *ctx, const char ***names);
```

Parser diagnostics are written directly to `FILE *err`. Directory
walking is not part of the parser library;
[`build/maestroc`](../../build/maestroc) owns source discovery.

## Runtime Values

`maestro_value` is opaque and context-associated. The public helper
layer exposes construction, destruction, and a narrow set of
accessors:

```c
maestro_value *maestro_value_new_invalid(maestro_ctx *ctx);
maestro_value *maestro_value_new_int(maestro_ctx *ctx, maestro_int_t v);
maestro_value *maestro_value_new_float(maestro_ctx *ctx, maestro_float_t v);
maestro_value *maestro_value_new_bool(maestro_ctx *ctx, bool v);
maestro_value *maestro_value_new_string(maestro_ctx *ctx, const char *s);
maestro_value *maestro_value_new_symbol(maestro_ctx *ctx, const char *s);
maestro_value *maestro_value_new_list(maestro_ctx *ctx);
maestro_value *maestro_value_new_json(maestro_ctx *ctx, const char *json_snippet);
void maestro_value_free(maestro_ctx *ctx, maestro_value *v);

int maestro_list_push(maestro_ctx *ctx, maestro_value *list, maestro_value *v);

int maestro_value_type(const maestro_value *v);
int maestro_value_as_int(const maestro_value *v, maestro_int_t *out);
int maestro_value_as_float(const maestro_value *v, maestro_float_t *out);
int maestro_value_as_bool(const maestro_value *v, bool *out);
const char *maestro_value_as_string(const maestro_value *v);
const char *maestro_value_as_symbol(const maestro_value *v);
size_t maestro_value_list_len(const maestro_value *v);
maestro_value *maestro_value_list_get(const maestro_value *v, size_t idx);
```

Object construction is intentionally narrow on the public side:
`maestro_value_new_json()` parses a static JSON snippet into a runtime
value. There is no public object mutation API.

### Argument Ownership

`maestro_run()` borrows its input argument handles for the duration of
the call:

- callers keep `args[i]` alive and unchanged until `maestro_run()`
  returns
- the runtime clones values that need to escape into longer-lived
  runtime state

The returned `result` handle is runtime-allocated and becomes the
caller’s responsibility. The caller must later destroy it with
`maestro_value_free(ctx, result)`.

## Artifact Model

A `.mstro` file is the output of the linking stage. It is:

- standalone
- linked
- packed
- zero-copy loadable
- representation-deduplicated

The runtime executes directly against the packed image through offsets
and indexes. It does not rebuild code into a second heap-owned image.

The current image layout is:

1. header
2. module table
3. external table
4. identifier table
5. node table
6. JSON key/value table
7. string table

The runtime is optimized around:

- zero-copy loading
- packed section layout
- deduplicated strings and identifiers
- identifier-ID-backed lookup in hot paths
- early external discovery for validation and inspection

The current executor still runs a packed AST-like node graph rather
than lowered bytecode.

## Header and Validation

Each image starts with a packed header:

```c
struct img_hdr {
	uint8_t magic[32];
	uint32_t version;
	uint32_t size;
	uint64_t capability;
	uint32_t mod_off;
	uint32_t mod_nr;
	uint32_t ext_off;
	uint32_t ext_nr;
	uint32_t ident_off;
	uint32_t ident_nr;
	uint32_t node_off;
	uint32_t node_nr;
	uint32_t kv_off;
	uint32_t kv_nr;
	uint32_t str_off;
	uint32_t str_sz;
};
```

Validation order is:

1. `magic`
2. `version`
3. `capability`
4. section bounds and sizes

The default magic is derived from:

```c
#define MAESTRO_MAGIC_STRING "maestro"
extern const uint8_t MAESTRO_DEFAULT_MAGIC[32];
```

`maestro_link_ex()` can override the artifact magic and capability
bitmap. `maestro_validate()` checks that the VM capability bitmap in
`maestro_ctx` is a superset of the artifact requirements.

## Externals

Required externals are recorded in a dedicated section before the node
table. That allows fast inspection and validation without walking the
code graph.

Inspection entrypoints:

- `maestro_validate()`
- `maestro_list_externals()`
- [`build/maestroexts`](../../build/maestroexts)

## Build Layout

The repository builds with a single [`Makefile`](../../Makefile).
Primary targets are:

- `make runtime`
- `make tools`
- `make examples`
- `make test`
- `make test-mstr`

Outputs are written under [`build/`](../../build/), including:

- [`build/libmaestro.a`](../../build/libmaestro.a)
- [`build/maestroc`](../../build/maestroc)
- [`build/maestrovm`](../../build/maestrovm)
- [`build/maestroexts`](../../build/maestroexts)
- [`build/hostrun`](../../build/hostrun)
- [`build/examples/`](../../build/examples/)

## Current Scope

The current implementation is intentionally minimal but end-to-end:

- parse and link happen ahead of runtime
- [`build/maestroc`](../../build/maestroc) is the standalone compiler
  driver
- runtime loads packed linked bundles zero-copy
- runtime values are opaque handles
- inspection and diagnostics stay available through names and strings
  in the image

The next major optimization step, if needed later, would be lowering
the packed AST image into a denser execution format. That is not part
of the current implementation.
