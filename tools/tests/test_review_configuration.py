# RTM Review_01
"""The review bot's workflow, rubric, template and code owners hold the safety model of PIPELINE.md rule 10.

WorkflowRunners_17 (ReviewBot_17.8; RTM Review_01). `.github/workflows/review.yml` runs on
`pull_request_target`, so GitHub runs it as the default branch holds it, with the repository's
secrets and a token that can write. That is safe only while nothing from the pull request's head
is checked out or run, each job holds the least privilege it needs, the model key and the write
token sit in separate jobs, the read token for the bot's private repository reaches only the step
that checks the bot out, and a fork's pull request is not given to the model at all. Each test
below holds one of those properties. The workflow is read as text, as tools/workflow_lint.py reads
every workflow, so no YAML parser joins the tools' dependencies.

Run: python3 -m unittest discover -s tools/tests
"""

import re
import sys
import tomllib
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

REPOSITORY = Path(__file__).resolve().parents[2]
WORKFLOW = REPOSITORY / ".github" / "workflows" / "review.yml"
RUBRIC = REPOSITORY / ".github" / "review" / "rubric.toml"
TEMPLATE = REPOSITORY / ".github" / "pull_request_template.md"
CODEOWNERS = REPOSITORY / ".github" / "CODEOWNERS"
WBS = "development/WBS.md"
RTM = "development/RTM.md"
MAINTAINER = "@ryanlandvater"

TRIGGER = "  pull_request_target:\n    types: [opened, synchronize, reopened, ready_for_review]"
# Under `pull_request_target`, github.sha is the default branch's last commit: the rules, the
# reference files and CODEOWNERS are read from it, so a pull request cannot change them.
DEFAULT_BRANCH_CHECKOUT = "ref: ${{ github.sha }}"
BOT_REPOSITORY = "repository: Aptix-Bio/governance"
BOT_CHECKOUT = "ref: ${{ env.GOVERNANCE_COMMIT }}"
BOT_ACTION = "./governance/review-bot"
SAME_REPOSITORY = "github.event.pull_request.head.repo.full_name == github.repository"
FORK = "github.event.pull_request.head.repo.full_name != github.repository"
# The head's commit reaches a step only as this variable, the commit whose diff the bot fetches
# through the API as data.
HEAD_DATA = "HEAD_SHA: ${{ github.event.pull_request.head.sha }}"
FETCHES_CODE = re.compile(r"\b(?:git (?:checkout|fetch|switch|clone|pull)|gh pr checkout)\b|refs/pull/|github\.head_ref")
TOKEN_VALUE = "token: ${{ secrets.GOVERNANCE_READ_TOKEN }}"
TOKEN_PRESENCE = "GOVERNANCE_READ_TOKEN_SET: ${{ secrets.GOVERNANCE_READ_TOKEN != '' }}"
KEY_VALUE = "ANTHROPIC_API_KEY: ${{ secrets.ANTHROPIC_KEY }}"
KEY_PRESENCE = "ANTHROPIC_KEY_SET: ${{ secrets.ANTHROPIC_KEY != '' }}"
REQUIRE_SECRETS = "python3 base/tools/require_secrets.py"
FULL_SHA = re.compile(r"[0-9a-f]{40}")
PINNED = re.compile(r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$")
TRANSCRIPT = "transcript.json"

LEAST_PRIVILEGE = {
    "fork": {},
    "review": {"contents": "read", "pull-requests": "read"},
    "code-review": {"contents": "read", "pull-requests": "write"},
}
# Each rubric item and whether a failure of it fails the `code-review` check.
ITEMS = {
    "Traceability": True,
    "StyleGuide": True,
    "MinimumSurface": True,
    "TestsFirst": True,
    "SecurityImpact": True,
    "Pipeline": True,
    "DependenciesThroughVcpkg": True,
    "DataProtection": True,
    "PublicRepository": True,
    "Documentation": False,
    "ClinicalIntegrity": True,
    "AgentDisclosure": True,
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
    "Failures fixed", "Clinical impact", "Security impact", "AI authorship", "Agent review",
    "Human verification",
]


def _workflow() -> str:
    return WORKFLOW.read_text(encoding="utf-8")


def _jobs() -> dict[str, str]:
    """Each job's text by its ID: a job opens with two spaces of indent under `jobs:`."""
    body = _workflow().split("\njobs:\n", 1)[1]
    pieces = re.split(r"^  ([\w-]+):\n", body, flags=re.MULTILINE)
    return dict(zip(pieces[1::2], pieces[2::2]))


def _steps(job: str) -> list[str]:
    """Each step of a job, as text: a step opens with six spaces and `- `."""
    return job.split("\n      - ")[1:]


def _condition(job: str) -> str:
    found = re.search(r"^    if: (.+)$", job, re.MULTILINE)
    return "" if found is None else found.group(1)


def _permissions(job: str) -> dict[str, str]:
    inline = re.search(r"^    permissions: \{\}$", job, re.MULTILINE)
    if inline is not None:
        return {}
    block = re.search(r"^    permissions:\n((?:      [\w-]+: \w+\n)+)", job, re.MULTILINE)
    assert block is not None, job
    return dict(line.strip().split(": ") for line in block.group(1).splitlines())


def _uses(step: str) -> str:
    used = re.search(r"^ *uses: (\S+)", step, re.MULTILINE)
    return "" if used is None else used.group(1)


class TriggerAndPermissions(unittest.TestCase):
    def test_the_review_runs_on_pull_request_target_alone(self) -> None:
        triggers = _workflow().split("\non:\n", 1)[1].split("\n\n", 1)[0]
        self.assertEqual(triggers, TRIGGER)

    def test_the_workflow_grants_nothing_and_each_job_its_least_privilege(self) -> None:
        workflow = _workflow()
        self.assertIn("\npermissions: {}\n", workflow[:workflow.index("\njobs:\n")])
        self.assertEqual({name: _permissions(job) for name, job in _jobs().items()}, LEAST_PRIVILEGE)

    def test_only_the_review_job_may_save_to_the_cache(self) -> None:
        # `pull_request_target` reads the default branch's cache scope read-only unless a job
        # declares a write-capable cache-mode; the conversation is saved under a key that is the
        # SHA-256 of its content, which the bot checks on restore.
        self.assertEqual(re.findall(r"^ *cache-mode: *(\S*)$", _workflow(), re.MULTILINE), ["write"])
        self.assertIn("\n    cache-mode: write\n", _jobs()["review"])


class ForkPullRequests(unittest.TestCase):
    def test_the_model_reviews_only_pull_requests_from_this_repository(self) -> None:
        jobs = _jobs()
        for name in ("review", "code-review"):
            with self.subTest(job=name):
                self.assertIn(SAME_REPOSITORY, _condition(jobs[name]))

    def test_a_fork_pull_request_gets_a_notice_and_nothing_else(self) -> None:
        fork = _jobs()["fork"]
        self.assertIn(FORK, _condition(fork))
        self.assertNotIn("secrets.", fork)
        steps = _steps(fork)
        self.assertEqual(len(steps), 1, steps)
        self.assertTrue(steps[0].startswith("run: echo \"::notice"), steps[0])


class HeadReadAsData(unittest.TestCase):
    def test_checkouts_are_the_default_branch_or_the_pinned_bot(self) -> None:
        for name, job in _jobs().items():
            for step in _steps(job):
                if not _uses(step).startswith("actions/checkout@"):
                    continue
                with self.subTest(job=name, step=step.splitlines()[0]):
                    self.assertIn("persist-credentials: false", step)
                    if BOT_REPOSITORY in step:
                        self.assertIn(BOT_CHECKOUT, step)
                    else:
                        self.assertNotIn("repository:", step)
                        self.assertIn(DEFAULT_BRANCH_CHECKOUT, step)

    def test_nothing_from_the_head_is_checked_out_or_run(self) -> None:
        for name, job in _jobs().items():
            for step in _steps(job):
                with self.subTest(job=name, step=step.splitlines()[0]):
                    action = _uses(step)
                    if action.startswith("./"):
                        self.assertEqual(action, BOT_ACTION)
                    self.assertIsNone(FETCHES_CODE.search(step))
                    for line in step.splitlines():
                        if "github.event.pull_request.head" in line:
                            self.assertEqual(line.strip(), HEAD_DATA)

    def test_pull_request_text_reaches_a_run_step_only_through_env(self) -> None:
        for name, job in _jobs().items():
            for step in _steps(job):
                run = re.search(r"^ *-? *run: (.*(?:\n {10,}.*)*)", step, re.MULTILINE)
                if run is None:
                    continue
                with self.subTest(job=name, step=step.splitlines()[0]):
                    self.assertNotIn("${{", run.group(1))

    def test_both_jobs_run_the_bot_from_its_pinned_checkout(self) -> None:
        pin = re.search(r"^  GOVERNANCE_COMMIT: (\S+)$", _workflow(), re.MULTILINE)
        assert pin is not None
        self.assertIsNotNone(FULL_SHA.fullmatch(pin.group(1)), pin.group(1))
        jobs = _jobs()
        for name in ("review", "code-review"):
            with self.subTest(job=name):
                used = [_uses(step) for step in _steps(jobs[name])]
                self.assertEqual(used.count(BOT_ACTION), 1, used)
                checkout = next(index for index, step in enumerate(_steps(jobs[name])) if BOT_REPOSITORY in step)
                self.assertLess(checkout, used.index(BOT_ACTION))
        self.assertIn('--bot-ref "$GOVERNANCE_COMMIT"', jobs["review"])

    def test_every_other_action_is_pinned_by_its_full_commit_hash(self) -> None:
        for name, job in _jobs().items():
            for step in _steps(job):
                action = _uses(step)
                if action and not action.startswith("./"):
                    with self.subTest(job=name, action=action):
                        self.assertIsNotNone(PINNED.match(action))


class Secrets(unittest.TestCase):
    def test_the_read_token_reaches_only_the_bot_checkout_and_the_presence_check(self) -> None:
        for name, job in _jobs().items():
            for step in _steps(job):
                lines = [line.strip() for line in step.splitlines() if "secrets.GOVERNANCE_READ_TOKEN" in line]
                with self.subTest(job=name, step=step.splitlines()[0]):
                    for line in lines:
                        self.assertIn(line, (TOKEN_VALUE, TOKEN_PRESENCE))
                    if TOKEN_VALUE in lines:
                        self.assertIn(BOT_REPOSITORY, step)
                        self.assertNotIn("github.event", step)
                        self.assertNotIn("run:", step)

    def test_the_model_key_is_held_only_by_the_job_that_cannot_write(self) -> None:
        jobs = _jobs()
        for name, job in jobs.items():
            with self.subTest(job=name):
                held = [line.strip() for line in job.splitlines() if "secrets.ANTHROPIC_KEY" in line]
                if name == "review":
                    self.assertEqual(set(held), {KEY_VALUE, KEY_PRESENCE})
                else:
                    self.assertEqual(held, [])

    def test_each_job_names_a_missing_secret_before_it_needs_it(self) -> None:
        needed = {"review": ("ANTHROPIC_KEY", "GOVERNANCE_READ_TOKEN"), "code-review": ("GOVERNANCE_READ_TOKEN",)}
        jobs = _jobs()
        for name, secrets in needed.items():
            steps = _steps(jobs[name])
            check = next(index for index, step in enumerate(steps) if REQUIRE_SECRETS in step)
            bot = next(index for index, step in enumerate(steps) if BOT_REPOSITORY in step)
            with self.subTest(job=name):
                self.assertLess(check, bot)
                self.assertIn(f"{REQUIRE_SECRETS} {' '.join(secrets)}", steps[check])

    def test_require_secrets_names_each_unset_secret(self) -> None:
        from require_secrets import missing_secrets  # imported here so the other tests run without it

        environment = {"ANTHROPIC_KEY_SET": "true", "GOVERNANCE_READ_TOKEN_SET": "false"}
        self.assertEqual(missing_secrets(["ANTHROPIC_KEY", "GOVERNANCE_READ_TOKEN"], environment), ["GOVERNANCE_READ_TOKEN"])
        # A step that forgot to pass the presence flag fails as if the secret were unset.
        self.assertEqual(missing_secrets(["ANTHROPIC_KEY"], {}), ["ANTHROPIC_KEY"])
        self.assertEqual(missing_secrets(["ANTHROPIC_KEY"], {"ANTHROPIC_KEY_SET": "true"}), [])


class ConversationCache(unittest.TestCase):
    def test_the_review_job_restores_its_conversation_before_the_review_and_saves_it_after(self) -> None:
        review = _jobs()["review"]
        steps = [review.index(marker) for marker in (
            "transcript-key --pr",
            "uses: actions/cache/restore@",
            f"--transcript {TRANSCRIPT}",
            "uses: actions/cache/save@",
        )]
        self.assertEqual(steps, sorted(steps))
        self.assertIn("key: ${{ steps.previous.outputs.restore-key }}", review)
        self.assertIn("key: ${{ steps.review.outputs.save-key }}", review)


class RulesFromTheDefaultBranch(unittest.TestCase):
    def _review(self) -> dict[str, object]:
        review = tomllib.loads(RUBRIC.read_text(encoding="utf-8"))["review"]
        assert isinstance(review, dict)
        return review

    def test_the_bot_reads_the_rubric_from_the_default_branch_checkout(self) -> None:
        jobs = _jobs()
        self.assertIn("RUBRIC_COMMIT: ${{ github.sha }}", jobs["review"])
        self.assertIn('--base-dir base --base-sha "$BASE_SHA"', jobs["review"])
        self.assertIn('--rubric-commit "$RUBRIC_COMMIT"', jobs["review"])
        self.assertIn("--rubric base/.github/review/rubric.toml --base-dir base", jobs["code-review"])

    def test_the_rubric_names_iris_slides_rules(self) -> None:
        review = self._review()
        self.assertEqual(review["reference_files"], REFERENCE_FILES)
        self.assertEqual(review["excerpt_files"], [WBS])
        self.assertIn("open-source decoder and encoder of Iris slide files", str(review["product"]))
        for name in REFERENCE_FILES + [WBS]:
            self.assertTrue((REPOSITORY / name).is_file(), name)

    def test_the_trace_checks_the_wbs_and_the_rtm(self) -> None:
        trace = tomllib.loads(RUBRIC.read_text(encoding="utf-8"))["trace"]
        self.assertEqual(trace, {"item": "Traceability", "ticks": [WBS], "rows": [RTM], "row_statuses": ["Implemented", "Verified"]})

    def test_the_items_and_which_of_them_block(self) -> None:
        items = tomllib.loads(RUBRIC.read_text(encoding="utf-8"))["item"]
        self.assertEqual({str(item["id"]): bool(item["blocking"]) for item in items}, ITEMS)
        self.assertEqual([str(item["id"]) for item in items], list(ITEMS))


class TemplateAndOwners(unittest.TestCase):
    def test_the_template_has_readmes_sections_in_order(self) -> None:
        headings = re.findall(r"^## (.+)$", TEMPLATE.read_text(encoding="utf-8"), re.MULTILINE)
        self.assertEqual(headings, TEMPLATE_SECTIONS)

    def test_the_template_has_one_agent_review_box_per_rubric_item(self) -> None:
        text = TEMPLATE.read_text(encoding="utf-8")
        section = text[text.index("<!-- review-bot:begin -->"):text.index("<!-- review-bot:end -->")]
        self.assertEqual(re.findall(r"^- \[ \] \*\*(\w+)\*\*", section, re.MULTILINE), list(ITEMS))
        self.assertIn("<!-- human-verification:begin -->", text)
        self.assertIn("<!-- human-verification:end -->", text)

    def test_the_maintainer_owns_everything_and_the_review_rules(self) -> None:
        rules = [line.split("#", 1)[0].split() for line in CODEOWNERS.read_text(encoding="utf-8").splitlines()]
        rules = [fields for fields in rules if fields]
        # The bot reads its code owners from the last catch-all line, the one GitHub applies.
        self.assertEqual([fields[1:] for fields in rules if fields[0] == "*"][-1], [MAINTAINER])
        owned = {fields[0]: fields[1:] for fields in rules}
        for path in ("/.github/review/", "/.github/workflows/"):
            with self.subTest(path=path):
                self.assertEqual(owned.get(path), [MAINTAINER])


if __name__ == "__main__":
    unittest.main()
