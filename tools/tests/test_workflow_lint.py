"""tools/workflow_lint.py: each rule fails on the case it names and passes the corrected line.

VcpkgCaches_16 (WorkflowLint_16.4; RTM Build_02). Run: python3 -m unittest discover -s tools/tests
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from workflow_lint import Rule, lint_cmake, lint_tree, lint_yaml  # noqa: E402

_PATH = Path("ci.yml")
_CHECKOUT = "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1"


def _rules(text: str) -> list[Rule]:
    return [violation.rule for violation in lint_yaml(_PATH, text)]


def _job(runs_on: str, *, timeout: bool = True, steps: str = "") -> str:
    lines = ["jobs:", "  build:", f"    runs-on: {runs_on}"]
    if timeout:
        lines.append("    timeout-minutes: 30")
    lines += ["    steps:", f"      - uses: {_CHECKOUT}", steps]
    return "\n".join(lines) + "\n"


class RunnerRule(unittest.TestCase):
    def test_hosted_labels_refused(self) -> None:
        for label in ("ubuntu-latest", "ubuntu-24.04", "ubuntu-24.04-arm", "macos-latest", "windows-2025"):
            with self.subTest(label=label):
                self.assertEqual(_rules(_job(label)), [Rule.RUNNER])

    def test_blacksmith_labels_pass(self) -> None:
        for label in ("blacksmith-4vcpu-ubuntu-2404", "blacksmith-4vcpu-ubuntu-2404-arm",
                      "blacksmith-6vcpu-macos-15", "blacksmith-4vcpu-windows-2025"):
            with self.subTest(label=label):
                self.assertEqual(_rules(_job(label)), [])

    def test_unknown_blacksmith_size_refused(self) -> None:
        self.assertEqual(_rules(_job("blacksmith-3vcpu-ubuntu-2404")), [Rule.RUNNER])

    def test_matrix_value_checked(self) -> None:
        text = _job("${{ matrix.os }}") + "    strategy:\n      matrix:\n        os: [ubuntu-latest]\n"
        self.assertEqual(_rules(text), [Rule.RUNNER])

    def test_comment_ignored(self) -> None:
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404  # once ubuntu-latest")), [])


class ActionPinRule(unittest.TestCase):
    def test_tag_refused(self) -> None:
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps="      - uses: actions/cache@v4")),
                         [Rule.ACTION_PIN])

    def test_sha_without_version_comment_refused(self) -> None:
        steps = "      - uses: actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830"
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps=steps)), [Rule.ACTION_PIN])

    def test_local_action_passes(self) -> None:
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps="      - uses: ./.github/actions/vcpkg")), [])


class CacheKeyRule(unittest.TestCase):
    _CACHE = "      - uses: actions/cache/restore@0057852bfaa89a56745cba8c7296529d2fc39830 # v4.3.0\n        with:\n"

    def test_static_key_refused(self) -> None:
        steps = self._CACHE + "          key: ccache-${{ runner.os }}"
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps=steps)), [Rule.CACHE_KEY])

    def test_content_hash_passes(self) -> None:
        steps = self._CACHE + "          key: src-${{ runner.os }}-${{ hashFiles('vcpkg.json') }}"
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps=steps)), [])

    def test_key_of_another_step_ignored(self) -> None:
        steps = "      - name: x\n        with:\n          key: anything"
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps=steps)), [])


class TimeoutRule(unittest.TestCase):
    def test_missing_timeout_refused(self) -> None:
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", timeout=False)), [Rule.TIMEOUT])

    def test_reusable_call_needs_none(self) -> None:
        self.assertEqual(_rules("jobs:\n  build:\n    uses: ./.github/workflows/_build.yml\n"), [])


class ParallelRule(unittest.TestCase):
    def test_unbounded_refused(self) -> None:
        for command in ("cmake --build build -j$CPU_COUNT", "cmake --build build --parallel", "make -j"):
            with self.subTest(command=command):
                steps = f"      - run: {command}"
                self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps=steps)), [Rule.PARALLEL])

    def test_bounded_passes(self) -> None:
        steps = "      - run: cmake --build build --parallel 4"
        self.assertEqual(_rules(_job("blacksmith-2vcpu-ubuntu-2404", steps=steps)), [])


class PullRequestBranchesRule(unittest.TestCase):
    def test_branch_filter_refused(self) -> None:
        text = "on:\n  pull_request:\n    branches:\n      - main\n" + _job("blacksmith-2vcpu-ubuntu-2404")
        self.assertEqual(_rules(text), [Rule.PR_BRANCHES])

    def test_types_pass(self) -> None:
        text = "on:\n  pull_request:\n    types: [opened]\n" + _job("blacksmith-2vcpu-ubuntu-2404")
        self.assertEqual(_rules(text), [])


class CMakeRules(unittest.TestCase):
    def test_external_project_refused(self) -> None:
        rules = [v.rule for v in lint_cmake(Path("x.cmake"), "ExternalProject_Add(\n  hwy\n)\n")]
        self.assertEqual(rules, [Rule.EXTERNAL_PROJECT])

    def test_directory_scope_refused(self) -> None:
        rules = [v.rule for v in lint_cmake(Path("x.cmake"), "include_directories(${X})\nadd_compile_definitions(A=1)\n")]
        self.assertEqual(rules, [Rule.GLOBAL_BUILD_STATE, Rule.GLOBAL_BUILD_STATE])

    def test_target_scope_passes(self) -> None:
        self.assertEqual(lint_cmake(Path("x.cmake"), "target_include_directories(t PRIVATE x)\n"), [])


class Repository(unittest.TestCase):
    """The repository itself follows every rule."""

    def test_tree_is_clean(self) -> None:
        root = Path(__file__).resolve().parent.parent.parent
        violations = lint_tree(root)
        self.assertEqual([str(violation) for violation in violations], [])


if __name__ == "__main__":
    unittest.main()
