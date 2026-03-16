# Maestro Tools

This document gives a high-level overview of the user-facing Maestro
tools built from this repository.

## `maestroc`

[`build/maestroc`](../../build/maestroc) is the standalone compiler
frontend.

It is responsible for:

- collecting source files from explicit file lists and directories
- recursively walking directories for `.mstr` files
- deduplicating discovered inputs
- parsing source files into ASTs
- linking ASTs into one packed `.mstro` artifact
- setting artifact header fields such as magic and capability

It does not run Maestro programs.

## `maestrovm`

[`build/maestrovm`](../../build/maestrovm) is a thin CLI wrapper
around the runtime library.

It is responsible for:

- reading a `.mstro` image
- loading it into a default `maestro_ctx`
- validating the image and host bindings
- running a module by logical module path
- printing the resulting `maestro_value`

It is useful for smoke tests and manual runtime checks.

## `maestroexts`

[`build/maestroexts`](../../build/maestroexts) is an inspection tool.

It reads a `.mstro` artifact and prints the required external tool
names recorded in the artifact header sections.

It does not execute program code.

## `tests/run_tests.py`

[`tests/run_tests.py`](../../tests/run_tests.py) is the source-suite
orchestrator used by `make test-mstr`.

It is responsible for:

- compiling categorized `.mstr` test bundles with
  [`build/maestroc`](../../build/maestroc)
- running selected modules through
  [`build/hostrun`](../../build/hostrun)
- capturing `print` and `log`
- validating required externals when needed
- filtering the run to specific module paths when positional arguments
  are supplied
