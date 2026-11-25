# RTM Review_01
"""Iris-Slide's review rules, template and step order, beside the shared workflow check.

WorkflowRunners_17 (ReviewBot_17.8; RTM Review_01). `.github/workflows/review.yml` is the review
workflow every public Aptix repository runs, and cmake/check_review_configuration.cmake, the check
those repositories share, holds its safety properties under `pull_request_target` (the trigger,
least privilege, base-only checkouts, the head as data, the confined token and key, forks
skipped, SHA pins). The tests here hold what that check does not: Iris-Slide's rubric, its pull
request template, the order of the steps in each job, and cmake/require_settings.cmake's
behaviour. The workflow is read as text, as tools/workflow_lint.py reads every workflow, so no
YAML parser joins the tools' dependencies.

Run: python3 -m unittest discover -s tools/tests
"""

import os
import re
import shutil
import subprocess
import tomllib
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
WORKFLOW = REPOSITORY / ".github" / "workflows" / "review.yml"
RUBRIC = REPOSITORY / ".github" / "review" / "rubric.toml"
TEMPLATE = REPOSITORY / ".github" / "pull_request_template.md"
REQUIRE_SETTINGS = "cmake/require_settings.cmake"
BOT_REPOSITORY = "repository: Aptix-Bio/governance"
WBS = "development/WBS.md"
RTM = "development/RTM.md"
SECRETS = ("ANTHROPIC_KEY", "GOVERNANCE_READ_TOKEN")

# Each rubric item and whether a failure of it fails the `code-review` check: the items every
# Aptix repository's rubric shares, in their order, then Iris-Slide's own.
ITEMS = {
    "Traceability": True,
    "StyleGuide": True,
    "MinimumSurface": True,
    "TestsFirst": True,
    "AbiAndConsumers": True,
    "PublicRepository": True,
    "SecurityImpact": True,
    "Pipeline": True,
    "DataProtection": True,
    "Documentation": False,
    "AgentDisclosure": True,
    "DependenciesThroughVcpkg": True,
    "ClinicalIntegrity": True,
}
REFERENCE_FILES = [
    "development/style_guide.md",
    "development/ci_runners.md",
    "development/ARCHITECTURE.md",
    "development/README.md",
    "development/PIPELINE.md",
    "development/RTM.md",
]
# The sections development/README.md gives a pull request's description, in its order, then the
# disclosure and the bot's section.
TEMPLATE_SECTIONS = [
    "Summary", "WBS", "RTM", "Decisions", "Measurements", "Test results", "Red first",
    "Failures fixed", "Clinical impact", "Consumer impact", "Security impact", "AI authorship",
    "Agent review", "Human verification",
]


def _job(name: str) -> str:
    """One job's text: a job opens with two spaces of indent under `jobs:`."""
    body = WORKFLOW.read_text(encoding="utf-8").split("\njobs:\n", 1)[1]
    pieces = re.split(r"^  ([\w-]+):\n", body, flags=re.MULTILINE)
    return dict(zip(pieces[1::2], pieces[2::2]))[name]


def _steps(job: str) -> list[str]:
    """Each step of a job, as text: a step opens with six spaces and `- `."""
    return job.split("\n      - ")[1:]


class StepOrder(unittest.TestCase):
    def test_each_job_names_a_missing_secret_before_it_needs_it(self) -> None:
        needed = {"review": SECRETS, "code-review": SECRETS[1:]}
        for name, secrets in needed.items():
            steps = _steps(_job(name))
            check = next(index for index, step in enumerate(steps) if REQUIRE_SETTINGS in step)
            bot = next(index for index, step in enumerate(steps) if BOT_REPOSITORY in step)
            with self.subTest(job=name):
                self.assertLess(check, bot)
                self.assertIn(f'run: cmake -DREQUIRED="{";".join(secrets)}" -P base/{REQUIRE_SETTINGS}', steps[check])

    def test_the_review_job_restores_its_conversation_before_the_review_and_saves_it_after(self) -> None:
        review = _job("review")
        steps = [review.index(marker) for marker in (
            "transcript-key --pr",
            "uses: actions/cache/restore@",
            "--transcript transcript.json",
            "uses: actions/cache/save@",
        )]
        self.assertEqual(steps, sorted(steps))
        self.assertIn("key: ${{ steps.previous.outputs.restore-key }}", review)
        self.assertIn("key: ${{ steps.review.outputs.save-key }}", review)

    def test_the_publish_step_reads_the_rubric_from_the_default_branch_checkout(self) -> None:
        self.assertIn("--rubric base/.github/review/rubric.toml --base-dir base", _job("code-review"))


class RequireSettings(unittest.TestCase):
    @unittest.skipIf(shutil.which("cmake") is None, "cmake is not on PATH")
    def test_require_settings_names_each_unset_setting(self) -> None:
        def run(required: str, flags: dict[str, str]) -> subprocess.CompletedProcess[str]:
            environment = {key: value for key, value in os.environ.items() if key not in SECRETS}
            return subprocess.run(["cmake", f"-DREQUIRED={required}", "-P", str(REPOSITORY / REQUIRE_SETTINGS)],
                                  env={**environment, **flags}, capture_output=True, text=True, check=False)

        missing = run(";".join(SECRETS), {"ANTHROPIC_KEY": "true", "GOVERNANCE_READ_TOKEN": "false"})
        self.assertNotEqual(missing.returncode, 0)
        # CMake wraps a message's lines; the names are read from the unwrapped text.
        self.assertIn("missing repository setting(s): GOVERNANCE_READ_TOKEN.", " ".join(missing.stderr.split()))
        # A step that forgot to pass the flag fails naming the variable, as a value that is not a flag does.
        self.assertNotEqual(run("ANTHROPIC_KEY", {}).returncode, 0)
        self.assertEqual(run("ANTHROPIC_KEY", {"ANTHROPIC_KEY": "true"}).returncode, 0, "a set secret passes")


class Rubric(unittest.TestCase):
    def test_the_rubric_names_iris_slides_rules(self) -> None:
        review = tomllib.loads(RUBRIC.read_text(encoding="utf-8"))["review"]
        self.assertEqual(review["reference_files"], REFERENCE_FILES)
        # The bot excerpts the items a pull request names by their `PascalCase_n` IDs, which is
        # how development/WBS.md writes them (`ReviewBot_17.8`).
        self.assertEqual(review["excerpt_files"], [WBS])
        self.assertIn("open-source decoder and encoder of Iris slide files", str(review["product"]))
        for name in REFERENCE_FILES + [WBS]:
            self.assertTrue((REPOSITORY / name).is_file(), name)

    def test_the_trace_checks_the_wbs_and_the_rtm(self) -> None:
        trace = tomllib.loads(RUBRIC.read_text(encoding="utf-8"))["trace"]
        self.assertEqual(trace, {"item": "Traceability", "ticks": [WBS], "rows": [RTM], "row_statuses": ["Implemented", "Verified"]})

    def test_the_items_and_which_of_them_block(self) -> None:
        items = tomllib.loads(RUBRIC.read_text(encoding="utf-8"))["item"]
        self.assertEqual([(str(item["id"]), bool(item["blocking"])) for item in items], list(ITEMS.items()))


class Template(unittest.TestCase):
    def test_the_template_has_readmes_sections_in_order(self) -> None:
        headings = [line[3:] for line in TEMPLATE.read_text(encoding="utf-8").splitlines() if line.startswith("## ")]
        self.assertEqual(headings, TEMPLATE_SECTIONS)

    def test_the_template_has_one_agent_review_box_per_rubric_item(self) -> None:
        text = TEMPLATE.read_text(encoding="utf-8")
        section = text[text.index("<!-- review-bot:begin -->"):text.index("<!-- review-bot:end -->")]
        boxes = [line.split("**")[1] for line in section.splitlines() if line.startswith("- [ ] **")]
        self.assertEqual(boxes, list(ITEMS))
        self.assertIn("<!-- human-verification:begin -->", text)
        self.assertIn("<!-- human-verification:end -->", text)


if __name__ == "__main__":
    unittest.main()
