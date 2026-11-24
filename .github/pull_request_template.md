<!--
Iris-Slide pull request. The sections and their order are development/README.md's. Fill each one
from development/: the WBS (work), the RTM (requirements), ARCHITECTURE.md (decisions). Write
"None" in a section that does not apply and keep the heading. The review bot reads this description
as data and checks it against the diff, against .github/review/rubric.toml on the default branch.

Two sections at the bottom are the bot's:
  - "Agent review" is rewritten by the review bot on every run; hand-ticked boxes are overwritten.
  - "Human verification" stays clear until a required verification job exists for this repository.
Branch name: feature/<SprintName>_<n>, or the task's name, e.g. feature/ReviewBot_17.8.
A pull request from a fork is not reviewed by the bot: a maintainer pushes its commits to an
Aptix-Bio branch and opens a pull request from there (development/PIPELINE.md, rule 10).
-->

## Summary

<!-- Two or three sentences: what this delivers and which WBS sprint or task it closes. -->

## WBS

<!-- The sprint's tasks, nested as in development/WBS.md, ticked only with their evidence on the
same line and a link to the commit that did the work. -->

- **_SprintName_n_**
  - [x] **_TaskName_n.n_** — _evidence_ ([1234567](https://github.com/Aptix-Bio/Iris-Slide/commit/<full hash>))

## RTM

| Row | Change | Evidence |
|---|---|---|
| _Area_nn_ | _Planned → Implemented → Verified_ | _test tagged `[Area_nn]` or `# RTM Area_nn`, file_ |

## Decisions

<!-- Decisions made or changed in ARCHITECTURE.md, one line each, dated, each stated as a plain
requirement, with its consequence. "None" if none. -->

## Measurements

<!-- Numbers taken while doing this work (sizes, times, cache hits), with the command. "None" if none. -->

## Test results

<!-- Per cell, local and CI, from the final run of this branch. -->

| Cell | Result | Run |
|---|---|---|
| linux-x64 gcc | _n / n_ | _link_ |

## Red first

<!-- Each task's test shown failing before the code that makes it pass: the command and the failure. -->

## Failures fixed

<!-- Every test or CI failure met while doing this work that needed a change. "None" if none. -->

| Cell or step | Symptom | Root cause | Fix | Commit |
|---|---|---|---|---|

## Clinical impact

<!-- What a pathologist would see differently: decode, colour, tile geometry, or patient identity
(ARCHITECTURE.md D2, D3, D8). "None" if none. -->

## Consumer impact

<!-- What Iris-Slide's consumers must change: a moved Iris-Headers or IFE pin (from and to which
commit), a public header, exported symbol, CMake target or install path, or the Python package's
surface; and, for a coordinated change, each sibling pull request and the order they merge.
"None" if none. -->

## Security impact

- New input parsed from a file or the network? _no / yes: where, and how every length is bounded_
- New or upgraded dependency? _no / yes: name, version, licence, how vcpkg pins it_
- New permission, secret, environment or token? _no / yes: which, and why_
- Patient data, real slide files or secrets anywhere in the diff, logs or fixtures? _no_

## AI authorship

<!-- Required. Which parts an AI agent wrote, with which tool and model, and what the human did
(directed, reviewed line by line, rewrote). -->

- Written by: _the maintainer / an AI agent (tool, model) / both — which parts_

## Agent review

<!-- Owned by the review bot. Hand edits are overwritten on its next run. -->
<!-- review-bot:begin -->
- [ ] **Traceability** — WBS tasks and RTM rows named; code and tests tagged; ticks carry evidence and a link to the work commit
- [ ] **StyleGuide** — `development/style_guide.md` followed
- [ ] **MinimumSurface** — no near-duplicates, Iris-Headers and IFE types reused, nothing ahead of its sprint
- [ ] **TestsFirst** — new behaviour tested and shown red first; per-cell results; no test weakened
- [ ] **AbiAndConsumers** — consumer impact of a pin, public API or Python change stated; sibling changes coordinated
- [ ] **PublicRepository** — nothing private, proprietary or internal; no quotations; no local paths
- [ ] **SecurityImpact** — the section above filled in and matching the diff
- [ ] **Pipeline** — workflows follow `ci_runners.md` and `PIPELINE.md`
- [ ] **DataProtection** — no patient data, slide files, secrets, keys or binaries
- [ ] **Documentation** — Mermaid diagrams match the code; decisions recorded (advisory)
- [ ] **AgentDisclosure** — AI authorship disclosed and specific
- [ ] **DependenciesThroughVcpkg** — every dependency from `vcpkg.json`, pinned
- [ ] **ClinicalIntegrity** — decode, colour, tile geometry and identity changes listed under Clinical impact

_Not yet reviewed._
<!-- review-bot:end -->

## Human verification

<!-- The bot writes this section and leaves it clear: no required verification job runs on this
repository yet. -->
<!-- human-verification:begin -->
- [ ] **HumanVerification** — a code owner read the diff and the bot's findings and accepts the head commit
<!-- human-verification:end -->
