# RTM Review_01
"""Fails a workflow job, naming each repository secret it needs that is not set.

WorkflowRunners_17 (ReviewBot_17.8). GitHub gives an unset secret as an empty string, so a job
that needs one otherwise fails later with a message that does not name it (a checkout refused, the
review bot missing an environment variable). A step's `if:` cannot read the `secrets` context, so
the step passes `<NAME>_SET: ${{ secrets.<NAME> != '' }}` for each secret: this script learns
whether each is set and never sees its value.

Usage: python3 tools/require_secrets.py NAME [NAME ...]
"""

import os
import sys
from collections.abc import Mapping, Sequence

# The value GitHub's expression `secrets.<NAME> != ''` gives a set secret.
SET = "true"


def missing_secrets(names: Sequence[str], environment: Mapping[str, str]) -> list[str]:
    """The names whose `<NAME>_SET` flag is not `true`; a flag the step did not pass counts as unset."""
    return [name for name in names if environment.get(f"{name}_SET") != SET]


def main(arguments: list[str]) -> int:
    if not arguments:
        raise SystemExit("require_secrets: name at least one secret, e.g. `require_secrets.py ANTHROPIC_KEY`")
    repository = os.environ.get("GITHUB_REPOSITORY", "<owner>/<repository>")
    missing = missing_secrets(arguments, os.environ)
    for name in missing:
        # A workflow command: GitHub shows it as an error annotation on the run's summary page.
        print(f"::error title=Missing secret {name}::The repository secret {name} is not set. "
              f"An administrator sets it with `gh secret set {name} --repo {repository}` (development/PIPELINE.md, rule 10).")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
