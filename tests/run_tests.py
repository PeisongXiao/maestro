#!/usr/bin/env python3
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
TEST_ROOT = ROOT / "tests" / "mstr"


SHALLOW_CASES = {
    "basics": [
        {
            "module": "tests basics hello",
            "args": ["str:Ada"],
            "result": "hello Ada",
        },
    ],
    "bindings": [
        {
            "module": "tests bindings main",
            "result": "5",
        },
    ],
    "control": [
        {
            "module": "tests control main",
            "result": "ok",
        },
    ],
    "modules": [
        {
            "module": "tests modules imports",
            "result": "import:wild",
        },
        {
            "module": "tests modules caller",
            "result": "worker:Ada",
        },
    ],
    "state": [
        {
            "module": "tests state last",
            "result": "alpha",
        },
        {
            "module": "tests state handoff src",
            "result": "handoff",
        },
    ],
    "refs": [
        {
            "module": "tests refs main",
            "result": "42",
        },
    ],
    "predicates": [
        {
            "module": "tests predicates main",
            "result": "[true,true,true,true,true,true,true,true,true,true,true,true,true]",
        },
    ],
    "arithmetic": [
        {
            "module": "tests arithmetic main",
            "result": "[6,5,24,4,2,3,4]",
        },
    ],
    "strings": [
        {
            "module": "tests strings main",
            "result": "abc-3",
        },
    ],
    "lists": [
        {
            "module": "tests lists main",
            "result": "[1,[2,3,4,5,ok],4,true]",
        },
        {
            "module": "tests lists invalid-concat",
            "error": True,
        },
        {
            "module": "tests lists invalid-append",
            "error": True,
        },
        {
            "module": "tests lists invalid-first-empty",
            "error": True,
        },
        {
            "module": "tests lists invalid-first-type",
            "error": True,
        },
        {
            "module": "tests lists invalid-rest-type",
            "error": True,
        },
        {
            "module": "tests lists invalid-rest-empty",
            "error": True,
        },
        {
            "module": "tests lists invalid-nth-type",
            "error": True,
        },
        {
            "module": "tests lists invalid-nth-negative",
            "error": True,
        },
        {
            "module": "tests lists invalid-nth-range",
            "error": True,
        },
    ],
    "higherorder": [
        {
            "module": "tests higherorder main",
            "result": "[[2,3,4],[2,4],10,123,true,true,[true,true,true]]",
        },
        {
            "module": "tests higherorder external",
            "result": "[[2,3,4],[2,4],true]",
            "externals": ["host-even", "host-inc"],
        },
    ],
    "objects": [
        {
            "module": "tests objects main",
            "result": "[Ada,38]",
        },
        {
            "module": "tests objects missing-get",
            "result": "[true,true,true,true]",
        },
        {
            "module": "tests objects missing-ref",
            "error": True,
        },
        {
            "module": "tests objects wrong-type",
            "error": True,
        },
        {
            "module": "tests objects expr-root",
            "result": "Ada",
        },
        {
            "module": "tests objects probe",
            "result": "[true,false,true,true]",
        },
        {
            "module": "tests objects invalid-get",
            "error": True,
        },
    ],
    "json": [
        {
            "module": "tests json main",
            "result": "38",
        },
        {
            "module": "tests json parse",
            "result": "5",
        },
        {
            "module": "tests json serialize",
            "result": "{\"age\":38,\"name\":\"Ada\"}",
        },
        {
            "module": "tests json invalid",
            "error": True,
        },
    ],
    "output": [
        {
            "module": "tests output main",
            "result": "7",
            "stdout": "out!",
            "stderr": "err!",
        },
    ],
    "external": [
        {
            "module": "tests external main",
            "result": "5",
            "externals": ["echo"],
        },
    ],
}

DEEP_SUITES = {
    "integration": {
        "compile": [("dir", TEST_ROOT / "integration")],
        "externals": [
            "host-bool",
            "host-check",
            "host-float",
            "host-int",
            "host-list",
            "host-object",
            "host-string",
            "host-symbol",
        ],
        "cases": [
            {
                "module": "tests integration main",
                "result": "{\"seen\":[\"Ada\",\"yes\"],\"tag\":\"worker\"}:tail",
            },
            {
                "module": "tests integration accessors",
                "result": "checked:{\"user\":{\"name\":\"Ada\",\"meta\":{\"active\":\"yes\"}},\"scores\":[1,2,3]}",
            },
        ],
    },
    "bundles": {
        "compile": [("dir", TEST_ROOT / "bundles")],
        "externals": ["echo", "host-object"],
        "cases": [
            {
                "module": "tests bundles alpha",
                "result": "{\"name\":\"alpha\",\"msg\":\"host\"}",
            },
            {
                "module": "tests bundles beta",
                "result": "6",
            },
            {
                "module": "tests bundles delta",
                "result": "delta:ok",
            },
            {
                "module": "tests bundles gamma",
                "result": "{\"user\":{\"name\":\"Ada\",\"meta\":{\"active\":\"yes\"}},\"scores\":[1,2,3]}",
            },
        ],
    },
}


def normalize_module(text: str) -> str:
    return " ".join(part for part in text.replace(".", " ").split() if part)


def parse_sections(text: str) -> dict[str, str]:
    tags = {"stdout", "stderr", "result"}
    out: dict[str, str] = {}
    key = None
    chunks: list[str] = []
    for line in text.splitlines():
        if line.startswith("[") and line.endswith("]"):
            tag = line[1:-1]
            if not tag.startswith("/") and tag in tags:
                key = tag
                chunks = []
                continue
            if key and tag.startswith("/") and tag[1:] == key:
                out[key] = "\n".join(chunks)
                key = None
                chunks = []
                continue
        if key is not None:
            chunks.append(line)
    return out


def run(cmd: list[str], cwd: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)


def compile_artifact(name: str, specs: list[tuple[str, pathlib.Path]]) -> pathlib.Path:
    artifact = BUILD / "tests" / f"{name}.mstro"
    cmd = [str(BUILD / "maestroc")]

    artifact.parent.mkdir(parents=True, exist_ok=True)
    for kind, path in specs:
        if kind == "dir":
            cmd.extend(["-d", str(path)])
        elif kind == "file":
            cmd.extend(["-f", str(path)])
        else:
            raise SystemExit(f"unknown compile spec kind: {kind}")
    cmd.extend(["-o", str(artifact)])
    res = run(cmd, ROOT)
    if res.returncode:
        sys.stderr.write(res.stdout)
        sys.stderr.write(res.stderr)
        raise SystemExit(f"compile failed for {name}")
    return artifact


def ensure_shallow_artifact(category: str) -> pathlib.Path:
    return compile_artifact(category, [("dir", TEST_ROOT / category)])


def ensure_deep_artifact(suite: str) -> pathlib.Path:
    return compile_artifact(f"deep-{suite}", DEEP_SUITES[suite]["compile"])


def check_externals(artifact: pathlib.Path, expected: list[str]) -> None:
    res = run([str(BUILD / "maestroexts"), str(artifact)], ROOT)
    if res.returncode:
        raise SystemExit(f"maestroexts failed for {artifact}")
    got = sorted(line.strip() for line in res.stdout.splitlines() if line.strip())
    want = sorted(expected)
    if got != want:
        raise SystemExit(f"externals mismatch for {artifact}: got {got}, want {want}")


def selected_shallow(filters: list[str]) -> dict[str, list[dict[str, object]]]:
    if not filters:
        return SHALLOW_CASES
    normalized = {normalize_module(item) for item in filters}
    picked: dict[str, list[dict[str, object]]] = {}
    for category, cases in SHALLOW_CASES.items():
        subset = [case for case in cases if case["module"] in normalized]
        if subset:
            picked[category] = subset
    missing = sorted(normalized - {case["module"] for cases in picked.values() for case in cases})
    if missing:
        raise SystemExit(f"unknown test modules: {', '.join(missing)}")
    return picked


def selected_deep(filters: list[str]) -> dict[str, dict[str, object]]:
    if not filters:
        return DEEP_SUITES
    normalized = {normalize_module(item) for item in filters}
    picked: dict[str, dict[str, object]] = {}
    for suite, spec in DEEP_SUITES.items():
        subset = [case for case in spec["cases"] if case["module"] in normalized]
        if subset:
            picked[suite] = {
                "compile": spec["compile"],
                "externals": spec.get("externals", []),
                "cases": subset,
            }
    missing = sorted(normalized - {case["module"] for spec in picked.values() for case in spec["cases"]})
    if missing:
        raise SystemExit(f"unknown deep test modules: {', '.join(missing)}")
    return picked


def run_case(artifact: pathlib.Path, case: dict[str, object]) -> None:
    cmd = [str(BUILD / "hostrun"), str(artifact), case["module"], *case.get("args", [])]
    res = run(cmd, ROOT)
    if case.get("error"):
        if res.returncode == 0:
            raise SystemExit(f"expected runtime error for {case['module']}")
        print(f"ok {case['module']}")
        print("  result: <runtime error>")
        print(f"  stdout: {res.stdout!r}")
        print(f"  stderr: {res.stderr!r}")
        return
    if res.returncode:
        sys.stderr.write(res.stdout)
        sys.stderr.write(res.stderr)
        raise SystemExit(f"run failed for {case['module']}")
    sections = parse_sections(res.stdout)
    got_result = sections.get("result", "")
    got_stdout = sections.get("stdout", "")
    got_stderr = sections.get("stderr", "")
    if got_result != case["result"]:
        raise SystemExit(
            f"{case['module']}: result got {got_result!r}, want {case['result']!r}"
        )
    if got_stdout != case.get("stdout", ""):
        raise SystemExit(
            f"{case['module']}: stdout got {got_stdout!r}, want {case.get('stdout', '')!r}"
        )
    if got_stderr != case.get("stderr", ""):
        raise SystemExit(
            f"{case['module']}: stderr got {got_stderr!r}, want {case.get('stderr', '')!r}"
        )
    print(f"ok {case['module']}")
    print(f"  result: {got_result!r}")
    print(f"  stdout: {got_stdout!r}")
    print(f"  stderr: {got_stderr!r}")


def main(argv: list[str]) -> int:
    deep = False
    filters: list[str] = []
    total = 0

    for arg in argv[1:]:
        if arg == "--deep":
            deep = True
        else:
            filters.append(arg)

    if deep:
        picked = selected_deep(filters)
        for suite, spec in picked.items():
            artifact = ensure_deep_artifact(suite)
            if spec.get("externals"):
                check_externals(artifact, spec["externals"])
            for case in spec["cases"]:
                run_case(artifact, case)
                total += 1
        print(f"ran {total} deep source tests")
        return 0

    picked = selected_shallow(filters)
    for category, cases in picked.items():
        artifact = ensure_shallow_artifact(category)
        for case in cases:
            if "externals" in case:
                check_externals(artifact, case["externals"])
            run_case(artifact, case)
            total += 1
    print(f"ran {total} source tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
