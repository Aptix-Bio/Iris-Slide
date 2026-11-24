# Style Guide

Binding conventions for Iris-Slide's code and plans: C++20 (the library, the
encoder and the bindings), Python (the Python package and tools) and
JavaScript (the WASM module). Project specifics live in
[ARCHITECTURE.md](ARCHITECTURE.md); where they are more specific than this
guide, they win. Adopted 2026-11-24 from the style guide the maintainer keeps for the
Iris repositories, with the references to other projects removed.

## Principles

- **Minimum code surface.** Fewest functions, variables, branches and
  dependencies: each one is a risk vector. If two routines move similar data in
  similar ways, merge them into one with an enum or optional argument, and
  delete code to do it.
- **Concise over clever; explicit over implicit.** The obvious loop beats the
  one-liner. Comments explain *why*, never *what*.
- **Strongly typed, everywhere.** Types state the contract so the compiler, not
  the reader, enforces it (Typing, below).
- **Fail loudly.** A failing subsystem says so, with the offending value; it
  never returns empty and hopes the caller notices.
- **Self-cleaning.** Resources release themselves at scope exit.
- **Test first.** The test is shown red before the code that makes it green.
- **Measure before deciding.** Numbers, not assumptions; anything unmeasured
  is marked *(to measure)*.

## Planning

An agent's knowledge is a prior, not evidence. Versions, APIs, product plans
and standards change after a model's training, and a confident recollection
can be wrong.

After drafting a plan, and before presenting it as ready or starting work on
it, verify every external fact it depends on with an internet search against
primary sources (vendor documentation, release notes, the package registry,
the standard itself): versions, pins, commit SHAs, API shapes and flags; what a
product provides and its limits; prices, quotas and licences; the clauses of
standards cited (DICOM PS3, HL7, FHIR, ICC.1).

Record each check beside the claim it supports: the source URL and the date it
was read. A claim that could not be verified stays marked *(to confirm)* and is
never presented as fact. When a search contradicts the plan, fix the plan and
say what changed and why. Facts internal to the workspace (code, history,
documents) are verified by reading them, not by searching.

## Modules and Reuse

Modules are organized by domain, not layer. A module's public API is a small
surface; everything else is file-`static`, in an anonymous namespace, or
`_private`. Two modules that share no dependency graph share no file or
library target, and nothing links what it does not use (no umbrella targets).

Repeated code is extracted, to a place set by its dependencies:

| Scenario | Destination |
|---|---|
| Dependency-free, trivially small | a `static` helper / local `_helper()` |
| Dependency-free, non-trivial | a domain-grouped utility file |
| External dependencies (SIMD, codec libraries, OpenSlide, libdicom) | its own implementation file and build target, so dependencies do not bleed |

Reuse before writing: Iris-Headers' `Result`, `Buffer`, `Iris::FIFO::Queue`
and `Iris::Async`, and Iris-File-Extension's `Parser` and `Builder`, are used,
never copied or re-implemented.

## Cross-Platform Code

Platform branches live side by side in one function body behind `#if`: one
audit point, one signature, no duplication. Separate per-platform files,
chosen by the build system, only when a whole algorithm or module diverges,
not a few intrinsics.

## State

Use the fewest variables possible: compute in place unless the value is
expensive and reused, outlives its scope, or naming it fixes a real
readability problem. Declare at first use. No dead writes: a value set but
never read is deleted; a single-use intermediate is folded inline. No members
or globals that could be locals.

## Resources

- **C++:** RAII always. No raw `new`/`delete`; every handle (a TurboJPEG,
  libavif, libpng, OpenSlide or libdicom handle), file descriptor and mapping
  is owned by an RAII type or a `unique_ptr` with a custom deleter. Move over
  copy. `std::array`/`std::vector` over C arrays; `std::span` and
  `std::string_view` where ownership is not needed.
- **Ownership webs (C++):** an object that depends on another holds a
  `std::shared_ptr` to it, so nothing can be destroyed while something still
  needs it. The web is acyclic: an owner never holds shared ownership of what
  depends on it; it refers back by ID or `std::weak_ptr`. A shared object
  never holds a callback, because a callback may capture a handle that holds
  the shared object. A test drops every owner in each order and checks with a
  `std::weak_ptr` that everything was freed. Hot paths (a tile decode) pass
  references and spans, never shared ownership.
- **Python:** `with` for files, locks and subprocesses; a context manager on
  any class that owns a resource; generators where the consumer streams.

## Control Flow

- **Early return.** Check preconditions at the top and leave; never wrap a
  whole body in `if (valid)`. Preconditions are stated in a comment at the top
  of the body (non-null, aligned, initialized, …).
- **Linear over branchy.** Lookup tables and state-machine dispatch over
  `if`/`else if` cascades. Nesting at most 3 levels; deeper becomes a helper.
  No `goto`.
- **Never recurse; queue the calls.** No function calls itself, directly or
  through another (including through a callback or a function pointer, such as
  a library's element iterator). A walk over a tree (a DICOM sequence), a retry,
  a "repeat until settled" is a loop over an explicit queue: push the pending
  call and run the front entry until the queue is empty. FIFO (breadth first)
  by default; a stack only when the order demands it. Pending work is then
  data, not stack depth: deep input cannot overflow the stack, and a cycle
  shows as a queue that never drains, which a bound turns into a loud error.
  Within a thread, a plain container serves (`std::deque`,
  `collections.deque`); across threads, Iris-Headers' `Iris::FIFO::Queue`.
- **No side effects in conditions.** `x = foo(); if (x)`, never
  `if (x = foo())`.

## Errors

Runtime-hardened code uses both mechanisms, each at its own level: exceptions
to capture every kind of failure safely inside a module, `Result` to carry the
debugging detail across its boundary.

- **Inside a module, throw.** Anything the local code cannot handle (a failed
  constructor, a broken invariant, a vendor or library error, an exhausted
  resource) is thrown at the point of failure. RAII unwinds it cleanly.
- **At every API boundary, return a `Result`.** A boundary is the public API in
  `IrisSlideCore.hpp`, a thread's entry, and the Python and JavaScript
  bindings. No exception crosses one: the boundary catches by type and
  translates into a `Result` carrying `Iris::Domain::IRIS_SLIDE`, an
  `IrisSlide::SlideError` code, the offending value, the vendor's code when
  there is one, and where it failed. Boundary functions are `noexcept`, so an
  exception that escapes terminates loudly in a test instead of unwinding into
  foreign code.
- **Every error carries the offending value:** `"unknown encoding: " +
  std::to_string(e)`, never `"bad encoding"`.
- **Expected outcomes are values, not errors.** An empty tile in a sparse
  layer is a state the caller handles, never an exception on the read path.
- **Catch specific types.** `catch (...)` and bare `except:` appear only as the
  last handler at a boundary, where they become a `Result` saying an unknown
  exception was caught and where; never silent. Nothing is printed to
  `std::cout` or `std::cerr` in place of returning the failure.
- **Python** raises specific exceptions and chains them (`raise ... from e`).
  A C++ `Result` becomes a Python exception in the bindings; an exception
  becomes a message and a non-zero exit at a command line's `main`.
- **C++:** `noexcept` on moves, swaps, boundary functions, and leaf helpers
  that cannot fail. An `assert` documents an invariant for debug builds; it is
  never the only guard on a path a release build can reach.

## Typing

- **C++:** warnings as errors; `concepts` state every template's contract;
  exhaustive `switch` with no `default:`; signed and unsigned mixed only with
  an explicit conversion; `constexpr`, `inline` or templates instead of macros;
  non-`const` reference parameters only for outputs, so call sites show
  mutation.
- **Python:** `mypy --strict` with `disallow_any_explicit`; no `Any`; frozen
  dataclasses and `StrEnum`; annotated signatures everywhere; no
  `import *`, no mutable defaults. Stubs are generated from the bindings, not
  written by hand.
- **Define conventions once.** Reused constants and behaviours become enums
  early and are dispatched on, not copied into a family of functions (one
  `bytes_per_pixel(Format)`, not seven switches).
- **Named values for named things.** A value with a meaning is referred to by
  a scoped enumeration or a named constant in code and tests, defined in one
  place. A bare number ties nothing at one call site to the same number
  elsewhere, so its uses cannot be searched for, and a change of meaning
  ripples silently through every copy. When the values live in data (IFE's
  `spec/*.json`), the names come from that data, so the data stays the one
  source. A name becomes its raw number only at the boundary that needs it
  (the file, the wire).

## Naming

Files, modules and functions `snake_case`; classes and structs `PascalCase`;
private `_leading_underscore` (Python) or file-`static` (C++); constants
`UPPER_SNAKE_CASE` or `kCamelCase` (C++); macros `UPPER_SNAKE_CASE`; template
parameters short (`T`, `Key`), descriptive where public. JavaScript follows its
language's conventions (`camelCase` members, `PascalCase` types). A C++ data
member is `_leading_underscore`, the underscore followed by a lowercase letter
(`_parser`); the C++ standard reserves a leading underscore followed by an
uppercase letter, and any name with two underscores in a row.

Accessors name their kind. A predicate returning `bool` is `is_*`. A getter is
the bare noun (`size`, `path`), never `get_*`. A setter is `set_*` taking the
value, so mutation is visible at the call site.

**Shared types take the plain name.** A type whose objects are owned by
`std::shared_ptr` is known by the pointer: the plain name is the alias, and
the class it owns is that name with the suffix `_t`, so the shared ownership
is visible at every use and a caller cannot make an unshared one by accident.

```cpp
class Slide_t;                               // the object
using Slide = std::shared_ptr<Slide_t>;      // what everyone holds
[[nodiscard]] Result create_slide(const SlideDesc& desc, Slide& slide) noexcept;
```

The object is made only by `create_<name>`, which takes a `<Name>Desc` and
writes the shared object to its output parameter, returning a `Result`; it
never returns null (Errors). Members are reached through `->`. The `_t` types
live in a namespace (`Iris`, `IrisSlide`), outside the global names POSIX
reserves for its own `_t` types.

Plan and trace identifiers are spelled out, never abbreviated:
`SlideTestHarness_1.1`, `DecodeBounds_01`.

## Writing

Prose (comments, documents, commit messages, pull request descriptions) names
the exact mechanism and states why it was chosen.

- **The precise term.** Use the technical name of what was built ("scoped
  enumeration", "FIFO queue", "RAII handle", "content hash") instead of a
  loose stand-in ("by name", "a list", "cleaned up", "a fingerprint") that
  fits several implementations.
- **The reason.** State the risk removed or the requirement met. A change
  described without its reason cannot be checked against it.
- **No contrast pairs for emphasis.** A claim restated as its own negation
  ("X, never Y", "X, not Y") adds rhythm and no information. State the
  positive claim once; mention a replaced approach only to say what changed,
  plainly ("instead of by number").

## Comments and Documentation

Block comments state the intent wherever the *why* is not visible in the code:
SIMD, the encoder's tile state machine, serialization layouts (byte order,
alignment, tags). A comment that restates the code is replaced by clearer code.
Dead code (commented-out blocks, unreachable branches, orphaned functions,
declarations with no definition) is deleted; git keeps the history.

Documents describing more than one type or component carry **Mermaid
diagrams**, reviewed like code, kept true to the source, and placed beside the
prose they illustrate. Every diagram must render.

- `classDiagram` for any set of collaborating types: key members and the
  relationships (`<|--` inheritance, `..|>` implementation, `*--`
  composition, `-->` association).
- `flowchart` for dependencies and control or data flow; `sequenceDiagram`
  for multi-actor lifecycles.
