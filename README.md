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

Everything is built into `build/`.

## Tools

- [`build/maestroc`](build/maestroc): standalone compiler frontend
- [`build/maestrovm`](build/maestrovm): thin runtime wrapper
- [`build/maestroexts`](build/maestroexts): required-external inspection tool

## Docs

- [`docs/quickstart.md`](docs/quickstart.md)
- [`docs/design/language-specs.md`](docs/design/language-specs.md)
- [`docs/design/maestro-design.md`](docs/design/maestro-design.md)
- [`docs/tools/maestro-tools.md`](docs/tools/maestro-tools.md)

## Typical Flow

Compile source files:

```sh
make examples
```

Inspect required external tools:

```sh
build/maestroexts build/examples/external.mstro
```

Run a module:

```sh
build/maestrovm build/examples/modules.mstro app.caller
```

Run categorized source-program tests:

```sh
make test-mstr
python3 tests/run_tests.py "tests modules caller"
```

For embedded use, link against [`build/libmaestro.a`](build/libmaestro.a)
and load `.mstro` artifacts through the runtime API instead of the CLI
wrappers. Include [`include/maestro/maestro.h`](include/maestro/maestro.h)
as the canonical public entry point.
