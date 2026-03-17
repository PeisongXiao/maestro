# Maestro

Maestro is an embedded, Lisp-like state-machine language with a packed
linked artifact format and a small C runtime.

This repository contains:

- the public C API
- the parser and linker
- the runtime loader and VM
- CLI tools for compile, run, and artifact inspection

## Build

Primary build targets:

- `make runtime`
- `make tools`
- `make examples`
- `make test`
- `make test-mstr`
- `make test-deep`

Everything is built into `build/`.

## Tools

- [`build/maestroc`](build/maestroc): standalone compiler frontend
- [`build/maestrovm`](build/maestrovm): standalone runtime driver with
  optional `.so` extension loading
- [`build/maestroexts`](build/maestroexts): required-external
  inspection tool

## Docs

- [`docs/quickstart.md`](docs/quickstart.md): the shortest path from
  source files to compiled artifacts, runtime execution, and test runs
- [`docs/api/api-common.md`](docs/api/api-common.md): shared public
  types, constants, flags, and callback typedefs from the common API
  surface
- [`docs/api/api-runtime.md`](docs/api/api-runtime.md): runtime
  context lifecycle, loading, validation, program execution, value
  creation, and value access APIs
- [`docs/api/api-compile.md`](docs/api/api-compile.md): parser and
  linker APIs for building `.mstro` artifacts from `.mstr` sources
- [`docs/extension-guide.md`](docs/extension-guide.md): how POSIX
  `.so` extensions register external function bindings and integrate
  with the runtime
- [`docs/design/language-specs.md`](docs/design/language-specs.md):
  the Maestro language definition, syntax, semantics, built-ins, and
  examples
- [`docs/design/maestro-design.md`](docs/design/maestro-design.md):
  implementation-facing design for the packed artifact format, runtime
  model, and library pipeline
- [`docs/tools/tools-overview.md`](docs/tools/tools-overview.md):
  index of the CLI tools and test runner manuals

## Typical Flow

Compile source files:

```sh
make examples
```

Inspect required external function bindings:

```sh
build/maestroexts build/examples/external.mstro
```

Compile, load, and run a module through the runtime driver:

```sh
build/maestrovm -d examples/modules -r "app caller" ""
```

Run categorized source-program tests:

```sh
make test-mstr
python3 tests/run_tests.py "tests modules caller"
```

Run deeper integration and bundle tests:

```sh
make test-deep
python3 tests/run_tests.py --deep "tests bundles alpha"
```

For embedded use, link against
[`build/libmaestro.a`](build/libmaestro.a) and load `.mstro` artifacts
through the runtime API instead of the CLI wrappers. Include
[`include/maestro/maestro.h`](include/maestro/maestro.h) as the
canonical public entry point.

For extension-based embedding or CLI use, see
[`docs/extension-guide.md`](docs/extension-guide.md) and
[`docs/tools/maestrovm.md`](docs/tools/maestrovm.md).
