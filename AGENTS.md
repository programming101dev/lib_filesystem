# p101 workspace instructions

These instructions apply to work in this workspace. They adapt the useful
lessons from the Brain-first system without importing the strict security or
Marlow workstation rules.

## Classify the work, then keep the scope small

Before changing tools, libraries, scripts, examples, or templates, classify the
work using the smallest useful role:

- `architect`: choosing boundaries, formats, shared libraries, tool semantics,
  or cross-repo direction;
- `implementor`: adding a feature or new behavior;
- `fixer`: repairing a failing build, test, warning, bug, or regression;
- `judge`: auditing, reviewing, comparing, or deciding whether something is
  done.

The classification is not ceremony. Use it to decide the evidence needed:
architecture work needs a recorded tradeoff, implementation needs tests or a
smoke run, fixes need before/after evidence, and judging needs explicit limits.

## Tool design rules

For p101 tools, prefer a small deterministic tool around an explicit contract.
Every substantial tool or feature should make these visible somewhere sensible:

- admitted inputs: which files, logs, schemas, command-line flags, or wrapper
  events the tool actually consumes;
- outputs: text, JSON, Mermaid, exit status, generated files, or reports;
- blind spots: what the tool cannot see, especially direct libc calls,
  third-party code, missing wrapper events, unsupported platforms, or heuristic
  judgments;
- evidence: the command, test, regression corpus entry, playground tour, or
  smoke run that demonstrates the behavior.

Do not describe heuristic tools as proof engines. A p101 report may be useful,
repeatable, and good enough for teaching, but it is still bounded by the inputs
and wrappers that produced it.

## Keep mechanism and policy separate

Put reusable mechanics in the narrowest shared home:

- shared C/POSIX wrappers belong in the appropriate `libraries/lib_*` repo;
- event/fact parsing belongs in a shared library once more than one consumer
  needs it, as with `lib_c_facts`;
- tool-specific policy, thresholds, wording, and teaching advice belong in the
  owning `programs/p101-*` tool;
- shared build/check behavior belongs in `scripts`.

Avoid copy-pasted parsers, duplicate record formats, and private versions of a
shared protocol. If a second tool needs the same parser, format, lifecycle
model, or checker, promote the mechanism and keep each tool's judgment local.

`setjmp`/`sigsetjmp` are the exception to the "p101_x behaves like x" wrapper
intuition: the macro/function must be invoked directly in the stack frame that
will receive the matching `longjmp`. Do not use a normal wrapper call as the
jump-capture site in fuzz harnesses or teaching examples.

## Prefer executable receipts

Whenever practical, finish a change with a receipt the next person can replay:

- `./check.sh` or `./build.sh -q` for an individual C/C++ repo;
- `./test.sh` for unit tests;
- `scripts/test-cmake.sh` for shared CMake changes;
- `scripts/check-after-update-all.sh` for the stack-level smoke;
- `scripts/check-p101-regression-corpus.sh` for behavior claims.

If a useful receipt is too expensive or unavailable, say what was checked and
what remains unverified.

## Keep teaching code readable

The tools are teaching artifacts. Favor plain text formats, small functions,
clear exit statuses, visible limitations, and APIs that expose only what another
file or repo genuinely needs. Use `static` aggressively in C code, and keep
headers small.
