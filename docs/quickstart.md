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

Before embedding a bundle, inspect its required external function
bindings:

```sh
build/maestroexts build/examples/external.mstro
```

## Run a Program

Use [`build/maestrovm`](../build/maestrovm) to compile or load an
artifact, validate it, and optionally run a module:

```sh
build/maestrovm -d examples/modules -r "app caller" ""
```

For extension-driven runs, load `.so` libraries with `-l` or `-d`:

```sh
build/maestrovm \
  -l build/tests/dll/plugin_ok.so \
  -d tests/mstr/dll \
  -r "tests dll describe" "42 2.5 \"hi\" 'sym {\"user\":{\"name\":\"Ada\"}}"
```

If `-r` is omitted, `maestrovm` stays in validate-only mode and
reports success to `stderr`.

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

- [`docs/api/api-common.md`](api/api-common.md)
- [`docs/api/api-runtime.md`](api/api-runtime.md)
- [`docs/api/api-compile.md`](api/api-compile.md)
- [`docs/extension-guide.md`](extension-guide.md)
- [`docs/design/language-specs.md`](design/language-specs.md)
- [`docs/design/maestro-design.md`](design/maestro-design.md)
- [`docs/tools/tools-overview.md`](tools/tools-overview.md)
