#!/usr/bin/env python3
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DLL_DIR = BUILD / "tests" / "dll"
SRC_DIR = ROOT / "tests" / "mstr" / "dll"


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)


def expect_ok(cmd: list[str], stdout: str | None = None, stderr_contains: str | None = None) -> None:
    res = run(cmd)
    if res.returncode:
        sys.stderr.write(res.stdout)
        sys.stderr.write(res.stderr)
        raise SystemExit(f"command failed: {' '.join(cmd)}")
    if stdout is not None and res.stdout.strip() != stdout:
        raise SystemExit(f"stdout mismatch for {' '.join(cmd)}: got {res.stdout.strip()!r}, want {stdout!r}")
    if stderr_contains is not None and stderr_contains not in res.stderr:
        raise SystemExit(
            f"stderr mismatch for {' '.join(cmd)}: missing {stderr_contains!r} in {res.stderr!r}"
        )


def expect_fail(cmd: list[str], stderr_contains: str | None = None) -> None:
    res = run(cmd)
    if res.returncode == 0:
        raise SystemExit(f"expected failure: {' '.join(cmd)}")
    if stderr_contains is not None and stderr_contains not in res.stderr:
        raise SystemExit(
            f"stderr mismatch for failing command {' '.join(cmd)}: missing {stderr_contains!r} in {res.stderr!r}"
        )


def main() -> int:
    maestrovm = str(BUILD / "maestrovm")
    maestroc = str(BUILD / "maestroc")
    ok_so = str(DLL_DIR / "plugin_ok.so")
    dup_so = str(DLL_DIR / "plugin_duplicate.so")
    fail_so = str(DLL_DIR / "plugin_fail.so")
    missing_so = str(DLL_DIR / "plugin_missing.so")
    artifact = str(BUILD / "tests" / "maestrovm-dll.mstro")
    run_args = '42 2.5 "hi" \'sym {"user":{"name":"Ada"}}'
    tmp_tree = BUILD / "tests" / "maestrovm-tree"

    expect_ok(
        [maestrovm, "-l", ok_so, "-d", str(SRC_DIR)],
        stderr_contains="validation succeeded:",
    )

    expect_ok(
        [maestrovm, "-l", ok_so, "-d", str(SRC_DIR), "-r", "tests dll describe", run_args],
        stdout='42|2.5|hi|sym|{"user":{"name":"Ada"}}',
    )

    expect_ok([maestroc, "-d", str(SRC_DIR), "-o", artifact])
    expect_ok(
        [maestrovm, "-l", ok_so, "-a", artifact, "-r", "tests dll describe", run_args],
        stdout='42|2.5|hi|sym|{"user":{"name":"Ada"}}',
    )

    expect_fail([maestrovm, "-a", artifact], stderr_contains="-a/--artifact requires -r/--run")
    expect_fail(
        [maestrovm, "-a", artifact, "-o", str(BUILD / "tests" / "bad.mstro"), "-r", "tests dll int", ""],
        stderr_contains="-a/--artifact cannot be used with -o/--output",
    )
    expect_fail(
        [maestrovm, "-a", artifact, "-f", str(SRC_DIR / "int.mstr"), "-r", "tests dll int", ""],
        stderr_contains="-a/--artifact cannot be used with source files",
    )
    expect_fail(
        [maestrovm, "-a", artifact, "-m", "other", "-r", "tests dll int", ""],
        stderr_contains="-a/--artifact cannot be used with -m/--magic",
    )

    expect_fail([maestrovm, "-l", missing_so, "-d", str(SRC_DIR)], stderr_contains="load dll")
    expect_fail([maestrovm, "-l", fail_so, "-d", str(SRC_DIR)], stderr_contains="load dll")
    expect_fail([maestrovm, "-l", ok_so, dup_so, "-d", str(SRC_DIR)], stderr_contains="load dll")

    if tmp_tree.exists():
        shutil.rmtree(tmp_tree)
    (tmp_tree / "nested" / "libs").mkdir(parents=True)
    (tmp_tree / "nested" / "src").mkdir(parents=True)
    shutil.copy2(DLL_DIR / "plugin_ok.so", tmp_tree / "nested" / "libs" / "plugin_ok.so")
    shutil.copy2(SRC_DIR / "describe.mstr", tmp_tree / "nested" / "src" / "describe.mstr")
    expect_ok(
        [maestrovm, "-d", str(tmp_tree), "-r", "tests dll describe", run_args],
        stdout='42|2.5|hi|sym|{"user":{"name":"Ada"}}',
    )

    print("maestrovm dll tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
