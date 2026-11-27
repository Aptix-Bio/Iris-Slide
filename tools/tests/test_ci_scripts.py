"""The CI's CMake scripts: scripts/ci_caches.cmake and scripts/cache_report.cmake.

VcpkgCaching_2 (OneReusableBuild_2.5; RTM Build_06). Each case runs the script with `cmake -P` as a job
would, with the environment GitHub Actions gives it, and reads the files it writes.
Run: python3 -m unittest discover -s tools/tests
"""

import os
import subprocess
import tempfile
import unittest
from dataclasses import dataclass
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent.parent
_CACHES = _ROOT / "scripts" / "ci_caches.cmake"
_REPORT = _ROOT / "scripts" / "cache_report.cmake"
_PROVIDER = "projects/1/locations/global/workloadIdentityPools/pool/providers/provider"
# Placeholders for the repository variables that name the cache bucket and its two service
# accounts: a public file names none of them (PublicNames in test_review_configuration.py).
_BUCKET = "cache-bucket"
_READER = "cache-reader@project.example"
_WRITER = "cache-writer@project.example"
# The repository variables the sign-in needs, by the environment name the caches action gives each.
_VARIABLES = {"GCP_PROVIDER": _PROVIDER, "GCP_PROJECT": "project", "CACHE_BUCKET": _BUCKET,
              "CACHE_READER": _READER, "CACHE_WRITER": _WRITER}


@dataclass(frozen=True)
class Run:
    code: int
    output: str
    files: dict[str, str]


def _run(script: Path, defines: dict[str, str], environment: dict[str, str]) -> Run:
    with tempfile.TemporaryDirectory() as folder:
        files = {name: Path(folder) / name for name in ("GITHUB_OUTPUT", "GITHUB_ENV", "GITHUB_STEP_SUMMARY")}
        for path in files.values():
            path.touch()
        env = {"PATH": os.environ["PATH"], "RUNNER_TEMP": folder, **{k: str(v) for k, v in files.items()}, **environment}
        arguments = ["cmake", *[f"-D{key}={value}" for key, value in defines.items()], "-P", str(script)]
        done = subprocess.run(arguments, env=env, capture_output=True, text=True, check=False)
        return Run(done.returncode, done.stdout + done.stderr, {name: path.read_text() for name, path in files.items()})


def _mode(**environment: str) -> Run:
    base = {"REQUESTED": "quarantine", "EVENT": "pull_request", "HEAD_REPOSITORY": "Aptix-Bio/Iris-Slide",
            "REPOSITORY": "Aptix-Bio/Iris-Slide", **_VARIABLES}
    return _run(_CACHES, {"STEP": "mode"}, {**base, **environment})


class CacheMode(unittest.TestCase):
    def test_same_repository_pull_request_gets_the_quarantine(self) -> None:
        run = _mode()
        self.assertEqual(run.code, 0, run.output)
        self.assertEqual(run.files["GITHUB_OUTPUT"], f"mode=quarantine\naccount={_READER}\n")

    def test_only_write_signs_in_as_the_writer(self) -> None:
        for requested, account in (("quarantine", _READER), ("read", _READER), ("write", _WRITER)):
            with self.subTest(requested=requested):
                run = _mode(REQUESTED=requested, EVENT="push")
                self.assertEqual(run.code, 0, run.output)
                self.assertIn(f"account={account}\n", run.files["GITHUB_OUTPUT"])

    def test_fork_pull_request_builds_without_the_store(self) -> None:
        run = _mode(HEAD_REPOSITORY="someone/Iris-Slide", **{variable: "" for variable in _VARIABLES})
        self.assertEqual(run.code, 0, run.output)
        self.assertEqual(run.files["GITHUB_OUTPUT"], "mode=none\naccount=\n")

    def test_missing_variables_fail_naming_them(self) -> None:
        run = _mode(GCP_PROVIDER="", GCP_PROJECT="")
        self.assertNotEqual(run.code, 0)
        self.assertIn("IRIS_SLIDE_GCP_PROVIDER, IRIS_SLIDE_GCP_PROJECT are unset", " ".join(run.output.split()))
        self.assertEqual(run.files["GITHUB_OUTPUT"], "")

    def test_each_missing_cache_variable_fails_naming_it(self) -> None:
        for variable in ("CACHE_BUCKET", "CACHE_READER", "CACHE_WRITER"):
            with self.subTest(variable=variable):
                run = _mode(**{variable: ""})
                self.assertNotEqual(run.code, 0)
                self.assertIn(f"the repository variables IRIS_SLIDE_{variable} are unset", " ".join(run.output.split()))
                self.assertEqual(run.files["GITHUB_OUTPUT"], "")

    def test_unknown_access_fails(self) -> None:
        self.assertNotEqual(_mode(REQUESTED="everything").code, 0)


class CacheEnvironment(unittest.TestCase):
    def _env(self, mode: str, compiler_cache: str = "true", token: str = "token", bucket: str = _BUCKET) -> Run:
        return _run(_CACHES, {"STEP": "env"}, {"MODE": mode, "GCS_TOKEN": token, "COMPILER_CACHE": compiler_cache,
                                               "CACHE_BUCKET": bucket})

    def test_quarantine_reads_main_and_writes_pr(self) -> None:
        run = self._env("quarantine")
        self.assertEqual(run.code, 0, run.output)
        sources = next(line for line in run.files["GITHUB_ENV"].splitlines() if line.startswith("VCPKG_BINARY_SOURCES="))
        self.assertIn(f"/{_BUCKET}/iris-slide/vcpkg/{{triplet}}/{{name}}-{{version}}-{{sha}}.zip,read,", sources)
        self.assertIn(f"/{_BUCKET}/iris-slide/pr/vcpkg/{{triplet}}/{{name}}-{{version}}-{{sha}}.zip,readwrite,", sources)
        self.assertIn(f"SCCACHE_GCS_BUCKET={_BUCKET}\n", run.files["GITHUB_ENV"])
        self.assertIn("SCCACHE_GCS_KEY_PREFIX=iris-slide/pr/sccache\n", run.files["GITHUB_ENV"])
        self.assertIn("CMAKE_CXX_COMPILER_LAUNCHER=sccache\n", run.files["GITHUB_ENV"])

    def test_write_writes_main(self) -> None:
        run = self._env("write")
        self.assertIn("iris-slide/vcpkg/{triplet}/{name}-{version}-{sha}.zip,readwrite,", run.files["GITHUB_ENV"])
        self.assertNotIn("/pr/", run.files["GITHUB_ENV"])

    def test_read_writes_nothing(self) -> None:
        run = self._env("read")
        self.assertNotIn("readwrite,Authorization", run.files["GITHUB_ENV"])
        self.assertIn("SCCACHE_GCS_RW_MODE=READ_ONLY\n", run.files["GITHUB_ENV"])

    def test_none_uses_the_runner_folder_only(self) -> None:
        run = self._env("none", token="")
        self.assertEqual(run.code, 0, run.output)
        self.assertNotIn("storage.googleapis.com", run.files["GITHUB_ENV"])
        self.assertNotIn("sccache", run.files["GITHUB_ENV"])

    def test_no_compiler_cache_sets_no_launcher(self) -> None:
        self.assertNotIn("LAUNCHER", self._env("quarantine", compiler_cache="false").files["GITHUB_ENV"])

    def test_missing_token_fails(self) -> None:
        self.assertNotEqual(self._env("quarantine", token="").code, 0)

    def test_missing_bucket_fails_naming_it(self) -> None:
        run = self._env("quarantine", bucket="")
        self.assertNotEqual(run.code, 0)
        self.assertIn("IRIS_SLIDE_CACHE_BUCKET", run.output)


_STATS = """Compile requests                    {requests}
Cache hits                          {hits}
Cache misses                        {misses}
Cache errors                        {errors}
Cache read errors                   {reads}
Cache write errors                  {writes}
"""


class CacheReport(unittest.TestCase):
    def _report(self, log: str | None, stats: str | None = None) -> Run:
        with tempfile.TemporaryDirectory() as folder:
            defines = {"LOG": str(Path(folder) / "history.log")}
            if log is not None:
                Path(defines["LOG"]).write_text(log)
            if stats is not None:
                defines["STATS"] = str(Path(folder) / "stats.txt")
                Path(defines["STATS"]).write_text(stats)
            return _run(_REPORT, defines, {})

    _LOG = ("Installing 1/2 zlib:x64-linux@1.3.2#2...\n"
            "Installing 2/2 libavif[aom,core]:x64-linux@1.4.2#1...\n"
            "Building libavif[aom,core]:x64-linux@1.4.2#1...\n")

    def test_restored_and_built_counted(self) -> None:
        run = self._report(self._LOG)
        self.assertEqual(run.code, 0, run.output)
        self.assertIn("- vcpkg: 1 of 2 packages from the binary cache, built: libavif", run.files["GITHUB_STEP_SUMMARY"])

    def test_refused_upload_fails(self) -> None:
        run = self._report(self._LOG + "error: HTTP response code 403\n")
        self.assertNotEqual(run.code, 0)
        self.assertIn("403", run.output)

    def test_failed_configure_reads_its_own_log(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "vcpkg-manifest-install.log").write_text(self._LOG + "error: building aom:x64-linux failed\n")
            run = _run(_REPORT, {"LOG": str(Path(folder) / "vcpkg-install-history.log")}, {})
        self.assertEqual(run.code, 0, run.output)
        self.assertIn("1 of 2 packages", run.files["GITHUB_STEP_SUMMARY"])

    def test_missing_or_empty_log_fails(self) -> None:
        self.assertNotEqual(self._report(None).code, 0)
        self.assertNotEqual(self._report("nothing installed\n").code, 0)

    def test_sccache_hits_pass(self) -> None:
        stats = _STATS.format(requests=100, hits=90, misses=10, errors=2, reads=0, writes=0)
        run = self._report(self._LOG, stats)
        self.assertEqual(run.code, 0, run.output)
        self.assertIn("90 hits, 10 misses in 100 compile requests", run.files["GITHUB_STEP_SUMMARY"])

    def test_sccache_broken_fails(self) -> None:
        for case in (dict(requests=0, hits=0, misses=0, errors=0, reads=0, writes=0),
                     dict(requests=100, hits=50, misses=50, errors=0, reads=1, writes=0),
                     dict(requests=100, hits=0, misses=100, errors=3, reads=0, writes=0),
                     dict(requests=100, hits=80, misses=20, errors=6, reads=0, writes=0)):
            with self.subTest(**case):
                self.assertNotEqual(self._report(self._LOG, _STATS.format(**case)).code, 0)


if __name__ == "__main__":
    unittest.main()
