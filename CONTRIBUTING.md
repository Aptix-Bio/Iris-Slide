# Contributing to Iris-Slide

The rules a pull request to Iris-Slide is held to, stated as facts of this codebase (written
2026-11-27); a rule the code does not yet meet everywhere names the planned item that brings it
there. The design contract is [development/ARCHITECTURE.md](development/ARCHITECTURE.md),
whose decisions (D1, D2, …) the code cites; the build and CI rules are the numbered rules of
[development/PIPELINE.md](development/PIPELINE.md). The family's general conventions (style, CI
runners, planning) and Iris-Slide's plans are kept privately; the review bot reads them with this
file, and each of its findings names the rule it applies.

## Pull requests

- **One open pull request at a time** (decided 2026-11-27, in place of one branch per sprint).
  Further work is committed onto the open pull request's branch, because parallel branches of one
  repository conflict and then have to be reconciled.
  One exception (decided 2026-11-27): a pull request that only moves the review bot's pin
  (`GOVERNANCE_COMMIT` in `review.yml`) opens beside the open one and merges first, because a
  review runs `main`'s workflow, so the open pull request is judged by the new bot only once the pin
  is on `main`; it touches one line no other branch changes, so it conflicts with none.
  The branch is `feature/<SprintName>_<n>`, named before the first push, since renaming a pull
  request's head branch closes the pull request.
- **The description follows `.github/pull_request_template.md`.** Its WBS and RTM sections name
  the work items (`PascalCaseName_<number>`: `DecodeSafety_3`, `JpegSizedFromHeader_3.1`) and the
  requirement rows (`PascalCaseArea_<nn>`: `Decode_01`) the change advances; a goal is
  `Goal_<nn>`. Their ticks are kept privately, each linking a commit of the pull request.
- **A change to the public headers is coordinated.** It has a branch of the same name in
  Iris-Headers, merged first, and this pull request pins that commit (PIPELINE.md rule 4). The
  merge order across the family is Iris-Headers, then the Iris File Extension (IFE), then
  Iris-Slide, since merging a dependent first breaks its `main`.
- **Red first.** A change in behaviour comes with a test shown failing before the code that
  makes it pass; the description's "Red first" section quotes the failure.
- **Tags.** Code that implements a requirement carries `// RTM <ID>` (C++) or `# RTM <ID>`
  (Python, CMake, workflows); its Catch2 test carries the tag `[<ID>]`. Test names, tags
  included, are printable ASCII, because ctest passes the name to Catch2 as a filter and Windows
  mangles other characters, so the filter would match nothing and the test would never run.

## The public boundary

- **A boundary returns `Iris::Result`.** The boundaries are the public API in `IrisSlideCore.hpp`,
  each thread's entry, and the Python and JavaScript bindings. Inside, a failure is thrown where
  it happens and RAII unwinds it; the boundary catches by type and returns a `Result` carrying
  `Iris::Domain::IRIS_SLIDE`, an `IrisSlide::SlideError` code, the offending value, the vendor's
  code when there is one, and where it failed. Boundary functions are `noexcept`.
- **A failure IFE reports keeps IFE's domain and code** (`Iris::Domain::IRIS_FILE_EXTENSION`,
  an `Iris::File::FileError`), as IFE returned it, so its exact code reaches the caller
  (ARCHITECTURE.md D12).
- **A boundary that produces a value writes it through an output reference** and returns the
  `Result`: `get_slide_info(const Slide&, SlideInfo&)`.
- **Expected outcomes are values.** An empty tile in a sparse layer, and a tile a phantom does
  not yet hold (D13), are states the caller handles.
- **Nothing is printed** to `std::cout` or `std::cerr` in place of returning the failure
  (planned: ResultBoundaries_12, and EncoderRewrite_24 for the encoder; the codec context's
  helpers in `src/IrisSlideContext.cpp` and the earlier encoder's associated images, printed and
  skipped (ARCHITECTURE.md §3 row 9), still print).

## Types and ownership

- **Shared types take the plain name.** `Slide` is `std::shared_ptr<Slide_t>`, made only by
  `create_slide(const SlideDesc&, Slide&)`, which returns a `Result` and never a null; the `_t`
  types live in the `IrisSlide` and `Iris` namespaces. A C++ data member is `_name` (`_parser`).
- **Every library handle has an RAII owner:** TurboJPEG, libavif, libpng (until PngRemoved_4
  removes it), libdicom and OpenJPEG. A tile decode, the hot path, passes references and spans.
- **The shared vocabulary is reused.** Iris-Headers' `Result`, `Buffer`, `Iris::FIFO::Queue` and
  `Iris::Async`, and IFE's `Parser` and `Builder`. A block's layout is written only through
  `Builder` and read through `Parser`, or, for a remote slide's phantom, through IFE's installed
  generated block readers (D1, amended by D13).
- **Named values come from IFE's data.** Encodings, formats and block types are referred to by the
  names generated from IFE's `spec/*.json`, and each convention is one function over its enum
  (one `bytes_per_pixel(Format)`).
- **Python** is checked with `mypy --strict`, and the type stubs are to be generated from the
  bindings (planned: Bindings_02).

## Code

- **A walk over a DICOM sequence is a loop over an explicit queue** (planned: Dicom_02). No function calls itself,
  directly or through libdicom's `dcm_sequence_foreach` and `dcm_dataset_foreach` callbacks, so
  a deeply nested series cannot overflow the stack. Across threads, the one queue is
  `Iris::FIFO::Queue`.
- **A decode sizes its output from the stream's own header** and compares it with TILE_LENGTH or
  the associated image's or annotation's declared size, refusing a mismatch naming both (D3).
- **Patient identity is written only in the clinical stream** (D2); attributes are written as
  `METADATA_I2S` and identify no individual (D2, D8).
- **Comments state intent** where the reason is not visible: SIMD, the encoder's tile state
  machine, serialization layouts (byte order, alignment, tags).

## Tests and data

- **A test builds the slide it reads** (RTM Testing_03). No real slide file, patient data, slide
  label or binary blob enters the repository.
- **Every dependency comes through vcpkg** (PIPELINE.md rules 1-5): no FetchContent,
  ExternalProject or download in a CMake file.

## Running the checks

```sh
python3 -m unittest discover -s tools/tests       # the tools, the workflow lint, the review configuration
cmake -P cmake/check_review_configuration.cmake   # the review workflow's safety properties
cmake --workflow --preset release                 # configure and build (development/PIPELINE.md, "Building locally")
```
