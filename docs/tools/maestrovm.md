# maestrovm

Related docs:

- [`tools-overview.md`](tools-overview.md)
- [`../extension-guide.md`](../extension-guide.md)
- [`../api/api-runtime.md`](../api/api-runtime.md)

## Purpose

`../../build/maestrovm` is the standalone runtime
driver for Maestro bundles.

It can:

- compile `.mstr` sources into a temporary or explicit artifact
- load an existing `.mstro` artifact
- load POSIX `.so` extension libraries
- validate required external function bindings
- optionally run an entry module with static literal arguments

## Synopsis

```text
build/maestrovm [options]
```

## Options

### `-h`, `--help`

Print help and exit immediately.

### `-v`, `--version`

Print the embedded Maestro runtime version and exit immediately.

### `-l`, `--libs PATH...`

Add shared libraries until the next option or the end of the argument
list.

Each library must export the
`../../include/maestro/common.h` entry symbol
described in [`../extension-guide.md`](../extension-guide.md).

This option is additive.

### `-d`, `--directory DIR`

Recursively add `.so` and `.mstr` files from `DIR`.

This option is additive.

### `-f`, `--files PATH...`

Add `.mstr` source files until the next option or the end of the
argument list.

This option is additive.

### `-o`, `--output PATH`

When compiling source inputs, write the artifact to `PATH`.

If omitted, `maestrovm` creates a temporary artifact path with a
system call and leaves that file in place.

### `-a`, `--artifact PATH`

Load an existing artifact instead of compiling source inputs.

This option is only valid when `-r` is also present.

### `-m`, `--magic STRING`

Override the artifact magic string when compiling source inputs.

### `-r`, `--run MODULE ARGS`

Run the space-separated module path `MODULE` with the static literal
argument string `ARGS`.

If `-r` is omitted, `maestrovm` stays in validate-only mode.

## Modes

### Validate-only mode

When `-r` is absent:

- source inputs are compiled
- extension libraries are loaded
- the artifact is loaded and validated
- success is reported to `stderr`

`-a` is invalid in this mode.

### Run mode

When `-r` is present:

- source inputs may be compiled, or an existing artifact may be loaded
  with `-a`
- extension libraries are loaded before validation
- the requested module is run after validation succeeds
- the result is printed to `stdout`

## Output and Errors

- Maestro `print` output is routed to `stdout`
- Maestro `log` output is routed to `stderr`
- runtime and CLI diagnostics are routed to `stderr`
- VM runtime failures emit `ERROR: ...` lines through the configured
  VM logger, including invalid builtin use and invalid JSON snippet
  evaluation
- the first detected runtime error aborts execution immediately
- runtime failure prints an error message to `stderr` and exits non-zero

## Static Argument Grammar

The `ARGS` string accepted by `-r` supports only static literal
values:

- decimal integer literals
- decimal float literals
- double-quoted strings
- symbols with a leading `'`
- JSON object literals with balanced braces

Examples:

```text
""
42
42 2.5 "hi" 'sym {"user":{"name":"Ada"}}
```

The JSON literal form is parsed as one argument even when it contains
spaces.

## Source and Artifact Rules

- `-a` may not be used with `.mstr` source inputs.
- `-a` may not be used with `-o`.
- `-a` may not be used with `-m`.
- `-m` applies only when compiling source inputs.

## Examples

Validate source inputs against an extension library:

```sh
build/maestrovm -l build/tests/dll/plugin_ok.so -d tests/mstr/dll
```

Compile sources, load an extension library, and run a module:

```sh
build/maestrovm \
  -l build/tests/dll/plugin_ok.so \
  -d tests/mstr/dll \
  -r "tests dll describe" "42 2.5 \"hi\" 'sym {\"user\":{\"name\":\"Ada\"}}"
```

Run an existing artifact:

```sh
build/maestrovm \
  -l build/tests/dll/plugin_ok.so \
  -a build/tests/dllapi.mstro \
  -r "tests dll int" ""
```
