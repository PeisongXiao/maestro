# maestroc

Related docs:

- [`tools-overview.md`](tools-overview.md)
- [`../api/api-compile.md`](../api/api-compile.md)
- [`../design/maestro-design.md`](../design/maestro-design.md)

## Purpose

`../../build/maestroc` is the standalone compiler
frontend for Maestro source files.

It discovers source inputs, parses them, links them, and writes one
packed `.mstro` artifact.

## Synopsis

```text
build/maestroc [options]
```

## Options

### `-h`, `--help`

Print help and exit immediately.

### `-v`, `--version`

Print the embedded Maestro version and exit immediately.

### `-m`, `--magic STRING`

Override the artifact magic string. The string is hashed into the
32-byte artifact magic field.

### `-c`, `--capabilities N`

Set the artifact capability bitmap from a decimal `uint64_t`.

### `-f`, `--files PATH...`

Add `.mstr` source files until the next option or the end of the
argument list.

This option is additive.

### `-d`, `--directory DIR`

Recursively add `.mstr` files from `DIR`.

This option is additive.

### `-o`, `--output PATH`

Write the linked artifact to `PATH`.

If omitted, the default output is `./artifact.mstro`.

## Behavior

- `-h` and `-v` short-circuit argument parsing.
- `-f` and `-d` may be combined.
- discovered inputs are deduplicated and linked into one artifact.
- directory walking is a tool responsibility, not a library parser
  responsibility.

## Examples

Compile an explicit file list:

```sh
build/maestroc -f app/main.mstr app/lib.mstr -o app.mstro
```

Compile an entire tree:

```sh
build/maestroc -d examples/modules -o build/examples/modules.mstro
```

Compile with custom header fields:

```sh
build/maestroc -m custom-magic -c 7 -d src -o app.mstro
```
