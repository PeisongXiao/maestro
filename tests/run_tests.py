#!/usr/bin/env python3
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
TEST_ROOT = ROOT / "tests" / "mstr"


CASES = {
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
            "result": "[1,2,3,4]",
        },
    ],
    "objects": [
        {
            "module": "tests objects main",
            "result": "1",
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


def ensure_artifact(category: str) -> pathlib.Path:
    artifact = BUILD / "tests" / f"{category}.mstro"
    artifact.parent.mkdir(parents=True, exist_ok=True)
    src_dir = TEST_ROOT / category
    res = run(
        [str(BUILD / "maestroc"), "-d", str(src_dir), "-o", str(artifact)],
        ROOT,
    )
    if res.returncode:
        sys.stderr.write(res.stdout)
        sys.stderr.write(res.stderr)
        raise SystemExit(f"compile failed for {category}")
    return artifact


def check_externals(artifact: pathlib.Path, expected: list[str]) -> None:
    res = run([str(BUILD / "maestroexts"), str(artifact)], ROOT)
    if res.returncode:
        raise SystemExit(f"maestroexts failed for {artifact}")
    got = [line.strip() for line in res.stdout.splitlines() if line.strip()]
    if got != expected:
        raise SystemExit(f"externals mismatch for {artifact}: got {got}, want {expected}")


def selected_cases(filters: list[str]) -> dict[str, list[dict[str, object]]]:
    if not filters:
        return CASES
    normalized = {normalize_module(item) for item in filters}
    picked: dict[str, list[dict[str, object]]] = {}
    for category, cases in CASES.items():
        subset = [case for case in cases if case["module"] in normalized]
        if subset:
            picked[category] = subset
    missing = sorted(normalized - {case["module"] for cases in picked.values() for case in cases})
    if missing:
        raise SystemExit(f"unknown test modules: {', '.join(missing)}")
    return picked


def main(argv: list[str]) -> int:
    picked = selected_cases(argv[1:])
    total = 0
    for category, cases in picked.items():
        artifact = ensure_artifact(category)
        for case in cases:
            if "externals" in case:
                check_externals(artifact, case["externals"])
            cmd = [str(BUILD / "hostrun"), str(artifact), case["module"], *case.get("args", [])]
            res = run(cmd, ROOT)
            if case.get("error"):
                if res.returncode == 0:
                    raise SystemExit(f"expected runtime error for {case['module']}")
                print(f"ok {case['module']}")
                print("  result: <runtime error>")
                print(f"  stdout: {res.stdout!r}")
                print(f"  stderr: {res.stderr!r}")
                total += 1
                continue
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
            total += 1
    print(f"ran {total} source tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
