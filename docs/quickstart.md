# Maestro Quickstart

This is the shortest path from source files to a runnable `.mstro`
artifact.

## Build

Build the runtime and tool binaries:

```sh
make runtime
make tools
make examples
```

Or build everything:

```sh
make
```

## Compile Sources

Use [`build/maestroc`](../build/maestroc) to compile one or more
`.mstr` files into a single linked `.mstro` bundle.

Examples:

```sh
build/maestroc -f app/main.mstr app/lib.mstr
build/maestroc -d examples/modules -o build/examples/modules.mstro
build/maestroc -m custom-magic -c 7 -d src -o app.mstro
```

Useful options:

- `-h`, `--help`
- `-v`, `--version`
- `-m`, `--magic`
- `-c`, `--capabilities`
- `-f`, `--files`
- `-d`, `--directory`
- `-o`, `--output`

`-f` and `-d` are additive and may be combined.

## Inspect Required Externals

Before embedding a bundle, inspect its required tool bindings:

```sh
build/maestroexts build/examples/external.mstro
```

## Run a Program

Use [`build/maestrovm`](../build/maestrovm) to load and run a module
from the artifact:

```sh
build/maestrovm build/examples/modules.mstro app.caller
```

Note that [`build/maestrovm`](../build/maestrovm) is a thin runtime
wrapper. Real embedded use should load the artifact through the library
and bind required tools in `maestro_ctx`.

## Run Source Suites

The repository also contains categorized `.mstr` suites under
[`tests/mstr/`](../tests/mstr/).

Run them all:

```sh
make test-mstr
```

Run only selected modules:

```sh
python3 tests/run_tests.py "tests modules caller" "tests json parse"
```

## More Reading

- [`docs/design/language-specs.md`](design/language-specs.md)
- [`docs/design/maestro-design.md`](design/maestro-design.md)
- [`docs/tools/maestro-tools.md`](tools/maestro-tools.md)
