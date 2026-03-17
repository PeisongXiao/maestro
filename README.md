# Maestro

Maestro is an embedded, Lisp-like state-machine language with a packed
linked artifact format and a small C runtime. Surface language
semantics are defined in
[`docs/design/language-specs.md`](docs/design/language-specs.md).

## Docs

- Start here: [`docs/quickstart.md`](docs/quickstart.md) The shortest
  path from source files to compiled artifacts, runtime execution, and
  test runs.
- Language:
  [`docs/design/language-specs.md`](docs/design/language-specs.md) The
  Maestro language definition, syntax, semantics, built-ins, and
  examples.
- Tools:
  [`docs/tools/tools-overview.md`](docs/tools/tools-overview.md) Index
  of the compiler, runtime driver, artifact inspector, and test runner
  manuals.
- [`docs/extension-guide.md`](docs/extension-guide.md): how POSIX
  `.so` extensions register external function bindings and integrate
  with the runtime
- [`docs/api/api-common.md`](docs/api/api-common.md): shared public
  types, constants, flags, and callback typedefs from the common API
  surface
- [`docs/api/api-runtime.md`](docs/api/api-runtime.md): runtime
  context lifecycle, loading, validation, program execution, value
  creation, and value access APIs
- [`docs/api/api-compile.md`](docs/api/api-compile.md): parser and
  linker APIs for building `.mstro` artifacts from `.mstr` sources
- [`docs/design/maestro-design.md`](docs/design/maestro-design.md):
  implementation-facing design for the packed artifact format, runtime
  model, and library pipeline

## Build

Primary build targets:

- `make runtime`
- `make tools`
- `make examples`
- `make test`
- `make test-mstr`
- `make test-deep`

Everything is built into `build/`.

## Typical Flow

Build the toolchain and example bundles:

```sh
make tools
make examples
```

Inspect required external function bindings in a bundle:

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

## Embedding

For embedded use, build the runtime library with `make runtime`, load
`.mstro` artifacts through the runtime API, and include
[`include/maestro/maestro.h`](include/maestro/maestro.h) as the
canonical public entry point.

For `.so`-based extension loading, see
[`docs/extension-guide.md`](docs/extension-guide.md). For runtime
driver behavior, see
[`docs/tools/maestrovm.md`](docs/tools/maestrovm.md).
