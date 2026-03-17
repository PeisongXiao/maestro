# Maestro Extension Guide

Related docs:

- [`api/api-runtime.md`](api/api-runtime.md)
- [`tools/maestrovm.md`](tools/maestrovm.md)
- [`tools/tools-overview.md`](tools/tools-overview.md)

## Overview

Maestro runtime extensions are POSIX shared libraries in `.so` format.

An extension library is loaded into a `maestro_ctx`, then registers
host-provided external function bindings by name. This makes dynamic
libraries the canonical way to extend an existing `maestrovm` binary
without rebuilding the runtime itself.

## Required Entry Symbol

Every extension library must export:

```c
int maestro_dll_init(maestro_ctx *ctx);
```

The public symbol name is also available as
[`MAESTRO_DLL_INIT_SYMBOL`](../include/maestro/common.h).

Return `0` on success. Any non-zero result is treated as an extension
load failure.

## Registering Functions

Inside `maestro_dll_init()`, register each external function binding
with:

```c
int maestro_register_fn(maestro_ctx *ctx, const char *name, maestro_fn fn);
```

Duplicate registrations are rejected.

## Callback Contract

An external binding uses:

```c
typedef int (*maestro_fn)(maestro_ctx *ctx, maestro_value **args,
                          size_t argc, maestro_value **result);
```

Ownership rules:

- `args[i]` are borrowed for the duration of the callback
- the callback must not free or mutate borrowed argument handles
- `*result` must be set to a runtime-owned value created for `ctx`
- the runtime frees that returned value after it has cloned or
  consumed it

## Minimal Example

```c
#include "maestro/maestro.h"

static int fn_host_int(maestro_ctx *ctx, maestro_value **args, size_t argc,
                       maestro_value **result) {
	(void)args;

	if (!ctx || !result || argc != 0)
		return MAESTRO_ERR_RUNTIME;

	*result = maestro_value_new_int(ctx, 7);
	return *result ? 0 : MAESTRO_ERR_NOMEM;
}

int maestro_dll_init(maestro_ctx *ctx) {
	return maestro_register_fn(ctx, "host-int", fn_host_int);
}
```

## Building an Extension

Build a shared library with position-independent code:

```sh
cc -Iinclude -Wall -Wextra -Werror -std=c11 -O2 -fPIC -shared \
  -o plugin.so plugin.c
```

The runtime process must export Maestro API symbols globally. The
repository build does this for [`build/maestrovm`](../build/maestrovm)
and the DLL integration test binary.

## Loading Extensions

From the runtime API:

```c
int maestro_ctx_load_dll(maestro_ctx *ctx, const char *path);
```

From the CLI:

```sh
build/maestrovm -l plugin.so -d app -r "app main" ""
```

## Common Failures

- missing `maestro_dll_init` export
- non-zero return from `maestro_dll_init`
- duplicate calls to `maestro_register_fn()` for the same function
  name
- loading an artifact whose required external names are not registered
