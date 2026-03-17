# run_tests.py

Related docs:

- [`tools-overview.md`](tools-overview.md)
- [`../quickstart.md`](../quickstart.md)

## Purpose

[`tests/run_tests.py`](../../tests/run_tests.py) is the repository’s
source-suite runner.

It compiles categorized `.mstr` suites with
[`build/maestroc`](../../build/maestroc), runs selected modules
through [`build/hostrun`](../../build/hostrun), captures output, and
checks expected results.

## Synopsis

```text
python3 tests/run_tests.py [MODULE...]
python3 tests/run_tests.py --deep [MODULE...]
```

## Behavior

- without `--deep`, runs the categorized shallow source suites
- with `--deep`, runs heavier integration and bundle suites
- positional arguments filter the run to specific space-separated
  module paths
- prints `result`, `stdout`, and `stderr` for each executed case

## Examples

Run the full categorized suite:

```sh
python3 tests/run_tests.py
```

Run one shallow case:

```sh
python3 tests/run_tests.py "tests modules caller"
```

Run the deep bundle suite:

```sh
python3 tests/run_tests.py --deep "tests bundles alpha"
```
