# maestroexts

Related docs:

- [`tools-overview.md`](tools-overview.md)
- [`../api/api-runtime.md`](../api/api-runtime.md)

## Purpose

[`build/maestroexts`](../../build/maestroexts) inspects a `.mstro`
artifact and prints the required external function binding names.

It does not execute program code.

## Synopsis

```text
build/maestroexts ARTIFACT.mstro
```

## Behavior

- loads the artifact header and external section
- prints one required external name per line
- returns a non-zero status if the artifact cannot be loaded

## Example

```sh
build/maestroexts build/examples/external.mstro
```
