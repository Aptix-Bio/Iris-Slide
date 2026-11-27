"""tools/cloud_federation.sh's service-account handling, against a stub gcloud.

VcpkgCaching_2 (CacheIdentities_2.6; RTM Build_06). The script is run as a project owner would
run it, with a `gcloud` on PATH that answers from a state file and logs every call, so no cloud
project is touched: a missing account is created from its address's local part, an address in
another project is refused by name after the second describe, and a missing variable is refused
by name before any call.
Run: python3 -m unittest discover -s tools/tests
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent.parent
_SCRIPT = _ROOT / "tools" / "cloud_federation.sh"
_PROJECT = "project"
# Placeholders: a public file names no real bucket, project or account, nor the address form a
# real account has (PublicNames), so the stub makes NAME@PROJECT.example, as test_ci_scripts does.
_DOMAIN = f"{_PROJECT}.example"
_READER = f"cache-reader@{_DOMAIN}"
_WRITER = f"cache-writer@{_DOMAIN}"

# The stub: an account exists when its address is a line of ACCOUNTS; `create NAME` adds
# NAME@PROJECT's domain; every other call succeeds, printing what the script reads.
_STUB = '''#!/usr/bin/env python3
import os, sys
arguments = sys.argv[1:]
with open(os.environ["STUB_LOG"], "a") as log:
    log.write(" ".join(arguments) + "\\n")
accounts_file = os.environ["STUB_ACCOUNTS"]
accounts = open(accounts_file).read().split()
if arguments[:3] == ["iam", "service-accounts", "describe"]:
    sys.exit(0 if arguments[3] in accounts else 1)
if arguments[:3] == ["iam", "service-accounts", "create"]:
    project = next(a.split("=", 1)[1] for a in arguments if a.startswith("--project="))
    with open(accounts_file, "a") as out:
        out.write(f"{arguments[3]}@{project}.example\\n")
    sys.exit(0)
if arguments[:2] == ["projects", "describe"]:
    print("123")
if arguments[:3] == ["storage", "buckets", "describe"] and "--format=json" in arguments:
    print("{}")
sys.exit(0)
'''


@dataclass(frozen=True)
class Run:
    code: int
    output: str
    calls: list[str]


def _run(environment: dict[str, str], accounts: list[str]) -> Run:
    with tempfile.TemporaryDirectory() as folder:
        stub = Path(folder) / "gcloud"
        stub.write_text(_STUB)
        stub.chmod(0o755)
        log, state = Path(folder) / "log", Path(folder) / "accounts"
        log.touch()
        state.write_text("".join(f"{account}\n" for account in accounts))
        bash = shutil.which("bash")
        assert bash, "bash is needed to run the script"
        env = {"PATH": f"{folder}{os.pathsep}{os.environ['PATH']}", "STUB_LOG": str(log),
               "STUB_ACCOUNTS": str(state), **environment}
        done = subprocess.run([bash, str(_SCRIPT)], env=env, capture_output=True, text=True, check=False)
        return Run(done.returncode, done.stdout + done.stderr, log.read_text().splitlines())


def _environment(**overrides: str) -> dict[str, str]:
    return {"PROJECT": _PROJECT, "BUCKET": "cache-bucket", "READER": _READER, "WRITER": _WRITER, **overrides}


class CloudFederationAccountsTest(unittest.TestCase):
    def test_a_missing_account_is_created_from_its_local_part(self) -> None:
        run = _run(_environment(), accounts=[_READER])
        self.assertEqual(run.code, 0, run.output)
        creates = [call for call in run.calls if call.startswith("iam service-accounts create")]
        self.assertEqual(len(creates), 1, creates)
        self.assertIn("create cache-writer ", creates[0])
        self.assertIn(f"--project={_PROJECT}", creates[0])
        self.assertIn(f"IRIS_SLIDE_CACHE_WRITER={_WRITER}", run.output)

    def test_existing_accounts_are_not_created_again(self) -> None:
        run = _run(_environment(), accounts=[_READER, _WRITER])
        self.assertEqual(run.code, 0, run.output)
        self.assertFalse([call for call in run.calls if call.startswith("iam service-accounts create")])

    def test_an_address_in_another_project_is_refused_naming_it(self) -> None:
        elsewhere = "cache-writer@other.example"
        run = _run(_environment(WRITER=elsewhere), accounts=[_READER])
        self.assertEqual(run.code, 1, run.output)
        self.assertIn(f"no service account {elsewhere} in {_PROJECT}", run.output)
        self.assertFalse([call for call in run.calls if "add-iam-policy-binding" in call])

    def test_a_missing_variable_is_refused_by_name_before_any_call(self) -> None:
        environment = _environment()
        del environment["READER"]
        run = _run(environment, accounts=[])
        self.assertNotEqual(run.code, 0)
        self.assertIn("READER is unset", run.output)
        self.assertEqual(run.calls, [])


if __name__ == "__main__":
    unittest.main()
