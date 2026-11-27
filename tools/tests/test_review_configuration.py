# RTM Review_01, Review_02
"""Iris-Slide's review rules, template and step order, beside the shared workflow check.

WorkflowRunners_17 (ReviewBot_17.8; RTM Review_01) and DevelopmentSplit_21 (RTM Review_02). `.github/workflows/review.yml` is the review
workflow every public Aptix repository runs, and cmake/check_review_configuration.cmake, the check
those repositories share, holds its safety properties under `pull_request_target` (the trigger,
least privilege, base-only checkouts, the head as data, the confined token and key, forks
skipped, SHA pins). The tests here hold what that check does not: Iris-Slide's rubric, its pull
request template, the order of the steps in each job, and cmake/require_settings.cmake's
behaviour. Since 2026-11-27 the rubric selects the shared items and names the family's general
rules and the plans, which are kept privately, by file name; none of them is in this repository. The workflow is read as text, as tools/workflow_lint.py reads every workflow, so no
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
SECRETS = ("ANTHROPIC_KEY", "GOVERNANCE_READ_TOKEN")

# The items every Aptix repository's rubric shares, in their shared order, that apply to Iris-Slide;
# the bot reads their text from its own checkout. Each says whether a failure of it fails the
# `code-review` check.
SHARED_ITEMS = {
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
    "ClinicalIntegrity": True,
    "AgentDisclosure": True,
}
# Iris-Slide's own items, which follow the shared ones.
OWN_ITEMS = {
    "DependenciesThroughVcpkg": True,
}
# This repository's own documents, sent whole from the default branch.
REFERENCE_FILES = [
    "development/ARCHITECTURE.md",
    "development/README.md",
    "development/PIPELINE.md",
    "CONTRIBUTING.md",
]
# The family's general rules, named by file name; the bot reads them from its own checkout.
SHARED_REFERENCE_FILES = ["style_guide.md", "ci_runners.md", "planning.md", "public_repositories.md"]
# The plan files, named by file name: the work items and requirement rows are kept privately, and
# the bot reads them, and checks their ticks and rows, from its own checkout.
PLAN_EXCERPT_FILES = ["WBS.md", "RTM.md"]
# Files that left this repository on 2026-11-27: the plans and their record are kept privately, and
# the general rules are the family's, which the bot reads from its own checkout.
REMOVED = [
    "development/WBS.md",
    "development/RTM.md",
    "development/analysis",
    "development/style_guide.md",
    "development/ci_runners.md",
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
    def setUp(self) -> None:
        self.rubric = tomllib.loads(RUBRIC.read_text(encoding="utf-8"))

    def test_the_rubric_names_iris_slides_documents_and_the_shared_rules(self) -> None:
        review = self.rubric["review"]
        self.assertEqual(review["reference_files"], REFERENCE_FILES)
        self.assertEqual(review.get("excerpt_files", []), [])
        self.assertEqual(review["shared_reference_files"], SHARED_REFERENCE_FILES)
        self.assertIn("open-source decoder and encoder of Iris slide files", str(review["product"]))
        for name in REFERENCE_FILES:
            self.assertTrue((REPOSITORY / name).is_file(), name)

    def test_the_rubric_names_the_plan_files_by_file_name(self) -> None:
        # A path would tell a public reader where the plans are kept; the bot takes the directory
        # from the repository the review runs for.
        review = self.rubric["review"]
        self.assertEqual(review["plan_excerpt_files"], PLAN_EXCERPT_FILES)
        self.assertEqual(review.get("plan_reference_files", []), [])
        for name in SHARED_REFERENCE_FILES + PLAN_EXCERPT_FILES:
            self.assertNotIn("/", name)

    def test_the_trace_checks_the_kept_wbs_and_rtm(self) -> None:
        self.assertEqual(self.rubric["trace"], {
            "item": "Traceability", "ticks": [], "rows": [], "row_statuses": ["Implemented", "Verified"],
            "plan_ticks": ["WBS.md"], "plan_rows": ["RTM.md"],
        })

    def test_the_items_and_which_of_them_block(self) -> None:
        self.assertEqual(self.rubric["review"]["shared_items"], list(SHARED_ITEMS))
        own = [(str(item["id"]), bool(item["blocking"])) for item in self.rubric["item"]]
        self.assertEqual(own, list(OWN_ITEMS.items()))
        # Text added to a shared item's guidance belongs to an item the rubric selects.
        self.assertLessEqual(set(self.rubric["shared_guidance"]), set(SHARED_ITEMS))

    def test_the_plans_and_the_general_rules_are_not_in_this_repository(self) -> None:
        for name in REMOVED:
            self.assertFalse((REPOSITORY / name).exists(), name)


class Template(unittest.TestCase):
    def test_the_template_has_readmes_sections_in_order(self) -> None:
        headings = [line[3:] for line in TEMPLATE.read_text(encoding="utf-8").splitlines() if line.startswith("## ")]
        self.assertEqual(headings, TEMPLATE_SECTIONS)

    def test_the_template_has_one_agent_review_box_per_rubric_item(self) -> None:
        text = TEMPLATE.read_text(encoding="utf-8")
        section = text[text.index("<!-- review-bot:begin -->"):text.index("<!-- review-bot:end -->")]
        boxes = [line.split("**")[1] for line in section.splitlines() if line.startswith("- [ ] **")]
        self.assertEqual(boxes, list(SHARED_ITEMS) + list(OWN_ITEMS))
        self.assertIn("<!-- human-verification:begin -->", text)
        self.assertIn("<!-- human-verification:end -->", text)


if __name__ == "__main__":
    unittest.main()
