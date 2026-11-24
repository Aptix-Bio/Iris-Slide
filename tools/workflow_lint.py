"""The rules of development/ci_runners.md and PIPELINE.md that a file can be checked against.

VcpkgCaches_16 (WorkflowLint_16.4; RTM Build_02, Build_05). Reads every workflow and composite
action under .github/, and the CMake files, line by line (the standard library has no YAML
parser, and every rule here is a property of one line or of an indented block), and prints one
line per violation with its file and line number. Exit status 1 when any is found.

Rules:
- runner: every runner label is a pinned Blacksmith label (Ryan, 2026-11-24: "We should be using
  blacksmith for our CI runners"); a GitHub-hosted label (`ubuntu-24.04`, `macos-latest`) is
  refused wherever it appears, in `runs-on` or in a matrix value passed to one.
- action-pin: an action from another repository is pinned to a full commit SHA with its version
  in a trailing comment, since a tag can be moved to other code.
- cache-key: an actions/cache key carries a content hash (`hashFiles(`), or reuses the primary key
  a restore step computed (`cache-primary-key`), so a key changes exactly when its content does.
- timeout: every job that runs on a runner has `timeout-minutes`.
- parallel: `-j` and `--parallel` name a job count (`-j$CPU_COUNT` with CPU_COUNT unset is
  unbounded and was in four workflows).
- pr-branches: a `pull_request` trigger has no `branches:` filter, so every branch's pull requests
  run (Ryan, 2026-11-24: "use the vcpkg on all branches").
- external-project: no third-party library is built by ExternalProject_Add; vcpkg provides them
  prebuilt from its binary cache (ci_runners.md, "Prebuilt over built").
- global-build-state: no directory-scope `include_directories(` or `add_compile_definitions(`
  in Iris-Slide's own CMake files (B7); settings are target-scoped.

Usage: python3 tools/workflow_lint.py [repository root]
"""

import re
import sys
from collections.abc import Iterator
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path


class Rule(StrEnum):
    RUNNER = "runner"
    ACTION_PIN = "action-pin"
    CACHE_KEY = "cache-key"
    TIMEOUT = "timeout"
    PARALLEL = "parallel"
    PR_BRANCHES = "pr-branches"
    EXTERNAL_PROJECT = "external-project"
    GLOBAL_BUILD_STATE = "global-build-state"


@dataclass(frozen=True)
class Violation:
    path: Path
    line: int
    rule: Rule
    detail: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.detail}"


# Blacksmith's labels, read 2026-11-24 from https://docs.blacksmith.sh/blacksmith-runners/overview.
_BLACKSMITH = re.compile(
    r"^blacksmith-(?:(?:2|4|8|16|32)vcpu-ubuntu-2404(?:-arm)?"
    r"|(?:2|4|8|16|32)vcpu-windows-2025"
    r"|(?:6|12)vcpu-macos-(?:15|26|27))$")
# A GitHub-hosted image label, wherever it appears as a value.
_HOSTED = re.compile(r"(?<![\w-])(?:ubuntu|macos|windows)-(?:latest|\d+(?:\.\d+)?)(?:-arm|-large|-xlarge)?(?![\w.-])")
_BLACKSMITH_TOKEN = re.compile(r"blacksmith-[\w-]+")
_USES = re.compile(r"^\s*-?\s*uses:\s*(\S+)(.*)$")
_PINNED = re.compile(r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$")
_KEY = re.compile(r"^\s*key:\s*(.+)$")
_PARALLEL = re.compile(r"(?:\s-j(?!\s*\d)(?:\$\w+|\s|$)|--parallel(?!\s+\d|=\d))")
_EXTERNAL_PROJECT = re.compile(r"^\s*ExternalProject_Add\s*\(", re.IGNORECASE)
_GLOBAL_STATE = re.compile(r"^\s*(include_directories|add_compile_definitions)\s*\(", re.IGNORECASE)


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def _code(line: str) -> str:
    """The line without a trailing YAML comment (a `#` after whitespace, outside `${{ }}`)."""
    return re.sub(r"(^|\s)#.*$", "", line)


def _block(lines: list[str], start: int) -> Iterator[tuple[int, str]]:
    """The lines indented under lines[start], with their indices."""
    parent = _indent(lines[start])
    for index in range(start + 1, len(lines)):
        line = lines[index]
        if line.strip() and not line.lstrip().startswith("#") and _indent(line) <= parent:
            return
        yield index, line


def lint_yaml(path: Path, text: str) -> list[Violation]:
    lines = text.splitlines()
    found: list[Violation] = []
    in_cache_step = False
    for number, raw in enumerate(lines, start=1):
        line = _code(raw)
        if not line.strip():
            continue
        for label in _HOSTED.findall(line):
            found.append(Violation(path, number, Rule.RUNNER, f"`{label}` is a GitHub-hosted runner; use a Blacksmith label"))
        for token in _BLACKSMITH_TOKEN.findall(line):
            if not _BLACKSMITH.match(token):
                found.append(Violation(path, number, Rule.RUNNER, f"`{token}` is not one of Blacksmith's labels"))
        uses = _USES.match(raw)
        if uses:
            target = uses.group(1).strip("'\"")
            in_cache_step = target.startswith("actions/cache")
            if not target.startswith("./") and not target.startswith("docker://"):
                if not _PINNED.match(target) or not re.search(r"#\s*v?\d", uses.group(2)):
                    found.append(Violation(path, number, Rule.ACTION_PIN,
                                           f"`{target}` is not pinned to a commit SHA with its version in a comment"))
        elif re.match(r"^\s*-\s", line):
            in_cache_step = False
        key = _KEY.match(line)
        if key and in_cache_step and "hashFiles(" not in key.group(1) and "cache-primary-key" not in key.group(1):
            found.append(Violation(path, number, Rule.CACHE_KEY, f"cache key `{key.group(1).strip()}` has no content hash"))
        if _PARALLEL.search(line):
            found.append(Violation(path, number, Rule.PARALLEL, "a parallel build with no job count"))
    found += _jobs_without_timeout(path, lines)
    found += _filtered_pull_requests(path, lines)
    return found


def _jobs_without_timeout(path: Path, lines: list[str]) -> list[Violation]:
    found: list[Violation] = []
    for index, line in enumerate(lines):
        if line.rstrip() != "jobs:":
            continue
        for job_index, job_line in _block(lines, index):
            if not job_line.strip() or job_line.lstrip().startswith("#") or _indent(job_line) != 2:
                continue
            body = [_code(text).strip() for _, text in _block(lines, job_index) if _indent(text) == 4]
            runs_on = any(entry.startswith("runs-on:") for entry in body)
            if runs_on and not any(entry.startswith("timeout-minutes:") for entry in body):
                name = job_line.strip().rstrip(":")
                found.append(Violation(path, job_index + 1, Rule.TIMEOUT, f"job `{name}` has no timeout-minutes"))
    return found


def _filtered_pull_requests(path: Path, lines: list[str]) -> list[Violation]:
    found: list[Violation] = []
    for index, line in enumerate(lines):
        if _code(line).strip() not in ("pull_request:", "pull_request_target:"):
            continue
        child = _indent(line) + 2
        for sub_index, sub in _block(lines, index):
            if _indent(sub) == child and _code(sub).strip().startswith(("branches:", "branches-ignore:")):
                found.append(Violation(path, sub_index + 1, Rule.PR_BRANCHES,
                                       "a pull_request trigger filtered by branch: every branch's pull requests run"))
    return found


def lint_cmake(path: Path, text: str) -> list[Violation]:
    found: list[Violation] = []
    for number, line in enumerate(text.splitlines(), start=1):
        if _EXTERNAL_PROJECT.match(line):
            found.append(Violation(path, number, Rule.EXTERNAL_PROJECT,
                                   "a dependency built by ExternalProject_Add; take it from vcpkg.json"))
        state = _GLOBAL_STATE.match(line)
        if state:
            found.append(Violation(path, number, Rule.GLOBAL_BUILD_STATE,
                                   f"directory-scope {state.group(1)}; use the target_ form"))
    return found


def lint_tree(root: Path) -> list[Violation]:
    found: list[Violation] = []
    github = root / ".github"
    for path in sorted([*github.glob("workflows/*.yml"), *github.glob("actions/*/action.yml")]):
        found += lint_yaml(path.relative_to(root), path.read_text(encoding="utf-8"))
    cmake_files = [root / "CMakeLists.txt", root / "javascript" / "CMakeLists.txt", *sorted((root / "cmake").glob("*.cmake"))]
    for path in cmake_files:
        if path.is_file():
            found += lint_cmake(path.relative_to(root), path.read_text(encoding="utf-8"))
    return found


def main(arguments: list[str]) -> int:
    root = Path(arguments[0]) if arguments else Path(__file__).resolve().parent.parent
    violations = lint_tree(root)
    for violation in violations:
        print(violation)
    print(f"workflow_lint: {len(violations)} violation(s)", file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
