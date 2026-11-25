# Code-generation switching: first implementation plan

Status: Tasks 1-5 are implemented. Task 6 now has an initial, opt-in Simple C
implementation for the directed low-memory fixtures; ambiguity, timing,
debug/probe, and repeated-call validation remain open. This is a companion to
the [living design](2026-11-17-code-generation-switching.md), primarily
addressing Question 1, bullet 1. Work through the remaining tasks in order;
each task should be independently reviewable and leave existing default
behavior and the normal test suite working. Update both documents with
findings, including findings from refining the tasks themselves.

## First product and boundaries

Produce a working **opt-in** Simple C path for directed, low-memory programs
whose code is installed, executed, overwritten and executed again. Emit one
static-opcode function containing a complete PC switch for each materialized
code state. `run_emulated()` dispatches the selected function. Copying common
cases between functions is intentional for this iteration. Unmodified
operands remain constants; operands detected as modified use the existing
dynamic-read rules. No per-instruction opcode interpreter is introduced.

The first product is not a promise that an arbitrary recording is supported.
Its supported transition patterns and recording requirements must be explicit.
Where correctness cannot be established, the opt-in path must reject at
generation time or report an explicit runtime error before executing stale
code. The legacy path, existing recordings and generated output must not
silently acquire different semantics. Do not equate an array index or recorded
transition order with a runtime clock.

Only low-memory code replacement is in scope for the first working replay.
Language-card changes, independently changing regions, code sharing, IR/IRC1,
and large EOS overlays are later iterations, not reasons to postpone the
directed milestone. Unknown behavior in those categories is not accepted
silently by the new path. This plan does not address the separate EOS `-O2`
issue.

## Invariants and terminology

- **Observed generation** is a collection-time grouping; **materialized code
  state** is a complete code view usable to disassemble one generated switch.
  Their indices need not match until the collector proves the correspondence.
  Existing `generations` JSON already stores useful byte contents as `code`
  segments with addresses, plus registers at save time. It is a sequence of
  partial segment sets, not complete state snapshots or runtime transition
  records. Reuse those bytes when supported by the evidence.
- Index 0 of the new representation is the initial state. It uses the binary,
  initial memory and first-generation observations according to the same rules
  as the current path. A recorded later `FirstInstructions` value must not
  silently replace an earlier executable version. Record the precise
  initialization and compatibility rules in the implementation.
- A bitfield records membership, not instruction bytes, execution order,
  operand history, or a clear/override operation. A missing sparse entry means
  inherit only when that is explicitly represented; it is not the same as a
  known-zero bit or an invalidated instruction. Keep written, start and full
  byte sets distinct.
- To claim an instruction version was executed, its bytes must be supported
  by evidence of what was visible **when the instruction executed**.
  Existing segment bytes can be useful candidates, but snapshot memory at a
  later write can already have changed operands. An instruction's size and
  operand-change information must be interpreted for its opcode version.
  Preserve existing static self-modification analysis.
- A transition is decided from the actual current program state, not cycles,
  frame count, input timestamps, or an unconditional `generation++`. A
  recorded transition PC is a candidate, not an unconditional switch.
- Changed code must be selected before its first affected instruction,
  including inside a previously formed basic block. Unknown or ambiguous
  matches are errors. No detection check may accidentally access I/O.
- Preserve registers, flags, stack, memory, PC, cycles, branch penalties,
  debug/probe events and yield behavior. In particular, a block's `CYCLES`
  currently charges the block and dispatches a probe at entry: cutting it
  short after that charge is not automatically correct.

## Fixtures and test strategy

Use small assembly programs under `tests/`, with focused shell checks in
`tests/run-tests.sh` or a focused helper invoked by it. Assemble using
`a6502`; collect using `a2run --collect`; run the interpreter and compiled
Simple C under equivalent initial state. The existing
[`language-card-driver.c`](../../tests/language-card-driver.c) and
[`run-tests.sh`](../../tests/run-tests.sh) demonstrate generated-C drivers
and host linking. Where an end-to-end fixture needs booting or a snapshot,
the existing `--preload`, `--from-rom`, `--snapshot-at` and `--snapshot-out`
options are starting points; confirm exact current invocation and start state
before encoding it in the test. Do not depend on session-only files.

For each behavioral case, assert hand-calculated interpreter results first,
then compare generated code with the interpreter at a defined stop PC. Check
PC, A/X/Y, status and SP, selected RAM and cycle count when both paths expose
comparable boundaries. Verify probe/debug behavior separately where appropriate.
Compare semantic JSON fields rather than incidental formatting or log text.
Use temporary test outputs with cleanup following repository practice.

1. **Installed, replaced, restored:** adapt the directed example in the
   [design](2026-11-17-code-generation-switching.md#directed-low-memory-experiment).
   Install `LDA #$11; RTS` at `$0900`, call it and store A at `$0200`.
   Replace the opcode and immediate with `LDX #$22; RTS`, clear X, call and
   store X at `$0201`. Restore `LDA #$11; RTS`, clear A, call and store A at
   `$0202`. Expect `$11,$22,$11`; assert both actual opcodes and the unchanged
   RTS are represented. This covers multiple observed states and reversion.
2. **Preloaded and lengths:** start with executable code in the input binary,
   execute it before any write, then replace it with an instruction of a
   different length and execute again. Use distinct result bytes and a return
   target that makes an incorrect length visible. Verify both old and new
   instruction bytes and starts; this catches reliance on `written` for
   the initial version.
3. **Operands and overlapping streams:** one unchanged opcode has a modified
   operand, and a byte is also an opcode when entered from an overlapping
   start. Assert operand-only change uses a dynamic read without forcing a new
   opcode version; when the overlapping opcode changes, it must be recognized
   separately. Include an unchanged operand that remains a literal in output.
4. **Unsafe boundary and unknown code:** a store modifies an opcode later in
   the same initially straight-line block; the next execution must use the new
   instruction, with correct cycles and probe events. Separately install an
   unrecorded opcode and assert a specific explicit failure before executing
   it. These are acceptance tests for the functional path, not assumptions
   about how early collector tasks already behave.
5. **Timing and order:** run the same supported transition with input-controlled
   delays and with a path that does not take the transition; verify correct
   results without a clock-based switch. Add alternate order and independent
   regions as an investigation case: support them only when proven, otherwise
   assert an explicit diagnostic rather than an incorrect whole-state match.

Introduce fixture infrastructure and collector assertions before enabling
generated-code acceptance tests in the normal suite. During development, run
the currently failing end-to-end assertion explicitly; do not commit a
permanently failing default suite or an expected failure that can mask a
regression. Add each acceptance assertion to the suite when its implementation
is ready.

## Detailed tasks

### 1. Establish the baseline and reproducible first fixture

**Files:** new focused `tests/` assembly and driver/helper; `tests/run-tests.sh`.

- [x] Implement case 1 with a bounded interpreter run and deterministic stop;
      assert the three result bytes and relevant state. Capture fresh run-data.
- [x] Inspect each existing `generations` segment and `FirstInstructions`
      entry for `$0900` and `$0902`. Document the actual snapshot boundaries,
      bytes and omissions, including whether the final save adds an empty or
      partial record. Do not assume one snapshot per installed version.
- [x] Confirm that the original two-version example's existing JSON contains
      `$A9 $11 $60` and `$A2 $22` at `$0900`; compare the recorded segments
      to the known execution. Also inspect Robotron's `$4C6E` segments as a
      real existing-recording example. Identify which facts can be reused
      without changing collection or the JSON schema.
- [x] Generate and run current Simple C as an explicit development
      reproduction; report the discrepancy, but keep the normal test suite
      green. Record the command and observed result in the living design.
- [x] Add case 2's interpreter and collection checks, explicitly showing
      whether preloaded, never-written code appears in the existing tracking.

**Done:** fixtures reproduce the interpreter's expected results and show what
the current recording can and cannot distinguish, without changing output.
The [fixture findings and commands](2026-11-17-code-generation-switching.md#current-branch-directed-fixtures)
include both snapshot sequences and the current Simple C mismatch.

### 2. Add an indexed tracking container without changing collection semantics

**Files:** [`DebugState6502.h`](../../include/apple2tc/DebugState6502.h),
[`DebugState6502.cpp`](../../lib/cpuemu/DebugState6502.cpp), focused tests.

- [x] Write a focused test for index 0, append and independent updates: setting
      a written/start/full bit in one entry must not mutate another; a newly
      appended entry must have the explicitly chosen inherited state.
- [x] Introduce the smallest representation that groups the three existing
      bitsets by index, choosing collector or decompiler ownership based on
      Task 1's findings. Begin with full copies if practical; `3 * 65536` bits
      is approximately 24 KiB per entry before container overhead. Treat this
      as a measurement baseline, not a requirement to keep every transient
      collector set indefinitely.
- [x] If the collector is changed, route its existing operations through entry
      0 first; retain current save/reset behavior and existing JSON. Otherwise
      keep its behavior intact while testing the new decompiler-side container.
      Do not label old partial snapshots as complete code states. Verify reset
      and repeated collection do not leak data between runs.
- [x] Exercise the append/copy operation in a focused test, but do not switch
      the production collector to new semantics until Task 3 defines what its
      entries mean.

**Done:** focused tests pass and the directed recordings' serialized legacy
behavior is unchanged; generation-indexed storage is tested independently. A synthetic workload
based on Bolo's actual segment ranges measured copying, lookup and peak RSS;
see the [design notes](2026-11-17-code-generation-switching.md#planning-notes)
for the method and its limitations. Measure an actual generation-aware
workload before choosing a sparse alternative. If measurements favor sparse
overrides, test explicit clears, inheritance and bounded lookup before
replacing copies.

### 3. Reuse recorded bytes and fill demonstrated gaps

**Files:** runtime-data loader; focused collection tests; living design.

- [x] Write tests that consume the existing segment bytes for the directed
      fixture and Robotron's `$4C6E` versions. Combine them with binary and
      first-instruction data where applicable. Check which versions, sizes and
      operand changes can actually be established; assert a changed operand
      alone is not treated as a new opcode version.
- [x] Add cases for preloaded code, a changed operand before a later snapshot,
      restoration and partial overwrites. Identify the precise information
      missing from the existing fields; do not infer executed bytes solely from
      `saveGeneration()`'s later `getMainRAM()` snapshot.
- [x] If the tests demonstrate a gap that cannot be closed from existing data,
      capture only the missing facts at execution or another justified point.
      Preserve the legacy `FirstInstructions` and `generations` fields; avoid
      adding a duplicate full instruction history without a demonstrated need.
      The fixtures show no missing byte fact for these cases, so no new
      collection field was added.
- [x] Use fixture traces to define what a tracking entry would mean. A saved
      segment is a chronological observation of bytes marked executable at a
      pre-write save or final collection, not a complete/selectable memory
      state. Seed known bytes from the binary or `FirstInstructions`, then
      override only bytes explicitly present in ordered segments; do not infer
      inheritance outside that tested model. Keep production collection on
      tracking entry 0 rather than equating partial JSON snapshots with
      indexed tracking entries. Reset, empty initial snapshots, restoration
      and partial overwrites are covered; entry creation for complete
      selectable states remains for Task 4.
- [x] Validate existing generation-segment structure for missing fields,
      invalid addresses/bytes, address-space overflow, duplicate and overlapping
      ranges. No schema was added, so no version marker is needed. Valid
      existing recordings remain accepted; malformed run-data now fails with
      a specific diagnostic.

**Done:** tests show what existing recordings can support, supplemented only
where necessary. The current fixtures need no additional observation field.
Legacy `FirstInstructions` and `generations` remain unchanged. The evidence
does *not* prove complete memory states, general inheritance, or runtime
transition conditions; those require Task 4's state model.

### 4. Materialize and disassemble independent code states

**Files:** [`RuntimeData.cpp`](../../tools/apple2tc/RuntimeData.cpp),
[`Disas.h`](../../tools/apple2tc/Disas.h),
[`Disas.cpp`](../../tools/apple2tc/Disas.cpp),
[`apple2tc.cpp`](../../tools/apple2tc/apple2tc.cpp), focused tests.

- [x] Write a test that loads a small, unambiguous multi-state recording and
      checks instruction starts, bytes, decoded sizes and CFG successors for
      each state. Check that unchanged code remains present and a changed
      length does not inherit stale starts or edges.
- [x] Add an opt-in `--generation-state=N` code view. Each invocation starts
      with the input memory, fills only unmapped bytes from `FirstInstructions`,
      then applies segment overrides from state 0 through N. Keep executed-byte
      observation and byte-source provenance separate from the memory image.
      Disassemble each selected view with a fresh CFG; aggregate branch targets
      and first-observed starts from other states are not imported.
- [x] Ensure state 0 preserves mapped input bytes and leaves the existing
      single-state path and old recordings alone. The new mode requires
      `FirstInstructions`, rejects conflicting first observations with the
      state and byte/PC in the diagnostic, and rejects unsupported language
      card, ROM, hand-asserted edge, and `--no-gen` combinations.
- [x] Test inherited code, preloaded bytes, changed instruction sizes, and the
      old `$0402` entry becoming an operand in the new state. Rebuilding the
      CFG removes that stale start and edge without inventing a memory clear.
      In the two-state fixture each state has four blocks; one block body
      (`$0300` JSR) is identical across states. The directed fixture also
      verifies that `$0900` code absent from the input binary is seeded from
      `FirstInstructions`. Thirty process runs averaged 1.67 ms for state 0
      and 1.63 ms for state 1; peak RSS was 4,572 KiB and 4,584 KiB,
      respectively. These are small-fixture process measurements, not
      representative game-generation costs.

**Done:** tests can inspect distinct static CFGs at the same PC; no runtime
dispatch or changed default output is needed yet. The opt-in view is still a
cumulative patch model, not proof of a complete runtime memory state. In
particular, unchanged input bytes are retained and only recorded segments
override them; transition selection, language-card mappings, and general
state completeness remain for later tasks.

### 5. Find and validate transition evidence

**Files:** focused fixtures, collector/loader if needed, living design.

- [x] For the directed case, identify the writes to `$0900-$0902`, their
      source PCs, and the next calls into `$0900`. Repeat with a delay and
      indexed/indirect stores, and with an unchanged-code path. The collector
      saves the PC that triggered a snapshot but not a per-write event/value;
      the fixtures and instruction bytes supply the missing evidence.
- [x] Define the low-memory selection contract: revalidate before every
      versioned instruction, including entries inside a block; dirty state on
      writes to bytes used as static code; compare the complete signature
      needed to distinguish candidate code, not a PC, generation ordinal, or
      one byte. Zero matches (including partial/unknown code) fail closed;
      duplicate signatures with different generated bodies are ambiguous and
      fail closed. Use direct low-memory reads, not side-effecting `peek()`.
- [x] Inventory generated Simple C writes. CPU stores, read-modify-write,
      indexed/indirect writes, and stack pushes converge on `ram_poke_impl`;
      initialization `memcpy` occurs before execution. The interpreter
      collector does not mark implicit stack writes, so stack-produced code
      is excluded until collected and tested. Upper-memory/I/O mapping and
      host mutations outside the public write helpers are also excluded.
- [x] Add delayed, unchanged, restored, and same-block evidence fixtures. The
      same-block test proves `$0305` changes after a store at `$0302` while
      remaining in the `$0300-$030C` block.
- [x] Implement and test the runtime signature matcher and invalidation
      behavior for known/unchanged/restored, partial/unknown, and ambiguous
      states. This requires the runtime selector introduced in Task 6; it is
      a hard gate before code-generation dispatch, not evidence that dispatch
      already exists.

**Evidence gate:** the fixtures establish a candidate contract and show why
block-entry-only dispatch is too late. The initial Task 6 implementation has
known/restored, delayed, same-block, and unknown-opcode replay checks. Focused
runtime tests also prove that compatible signatures from independent code
regions and multiple matching instructions at the current PC fail closed with
explicit ambiguity diagnostics. Broader independent-region combinations remain
a gate for expanding the supported subset.

### 6. Emit complete switches and safe dispatch

**Files:** [`PrintSimpleC.cpp`](../../tools/apple2tc/PrintSimpleC.cpp),
runtime helpers only if needed, generated-C tests.

- [x] Implement an on-demand signature matcher using direct low-memory reads.
      The directed, delayed, restored, and preloaded fixtures exercise complete
      matches; the installer naturally exercises partial writes. Writes through
      `ram_poke_impl`, including stack pushes, set a dirty flag. A partial
      signature is not treated as a selectable state; when the current
      instruction still matches the active state, checking remains pending
      until later writes complete.
- [x] Reject identical signatures that have different decoded instruction or
      block layouts. Unknown current instructions fail before stale code runs.
- [x] Add explicit runtime tests where two different state signatures both
      match because they constrain independent regions, and where a generated
      state is ambiguous at the current PC. Both fail closed with explicit
      state-level or instruction-level diagnostics.
- [x] Add an opt-in `--generation-aware` path. It emits one helper with a
      complete static PC switch per distinct materialized code state, persists
      the active state and `branchTarget` across helper calls, and reports
      unknown PCs. Existing initialization and the `run_emulated()` interface
      remain in place.
- [x] Detect writes before the next instruction, including inside the
      existing basic block. The initial implementation uses per-instruction
      dirty-flag guards but performs no opcode fetch unless memory has been
      written. A safe resume case re-credits the new state's remaining
      instruction cycles without repeating the original block-entry probe or
      debug event.
- [ ] Compare cycles and debug/probe events at transition boundaries, and
      test repeated `run_emulated()` calls/yields. The current generated-C
      driver checks result RAM, not all architectural state or event timing;
      cycle/event equivalence is not yet claimed.
- [x] Generated-C replay tests cover the installed/replaced/restored fixture,
      the preloaded length-changing fixture, delayed replacement, and an
      intra-block opcode replacement. An injected unknown opcode produces an
      explicit runtime diagnostic. The legacy Simple C path still generates
      successfully and retains its known first-version mismatch.
- [x] Direct output checks verify an unchanged immediate remains a literal and
      a recorded modified immediate uses a runtime read. A driver changes that
      operand after initialization and confirms the generated instruction
      observes it.
- [ ] Broaden ordinary no-option regression coverage beyond the current build
      and generation-switch tests.

**Initial result:** the opt-in path correctly replays the directed low-memory
fixtures and rejects an injected unknown opcode. It is not yet a general
generation-aware runtime: the remaining gates above must pass before widening
the documented support claim.

## Iteration after the first working product

1. Run the [investigation and validation](2026-11-17-code-generation-switching.md#investigation-and-validation)
   cases; record results and update both plans before choosing optimizations.
   Include Robotron/Bolo evidence and a representative EOS recording when the
   required coverage is available; distinguish observed replay from general
   correctness.
2. Measure duplicated switches and generated binary size, generation-time
   CPU/peak memory, selector checks and generated runtime overhead. Compare
   full-copy tracking with a sparse/delta prototype on the same fixtures and
   a realistic recording; verify equal semantics, including explicit clears.
3. Extend supported transition patterns and independent-region combinations
   using tests first. Revisit whether a global generation is an adequate
   identity; do not imply unobserved combinations are already supported.
4. Explore shared unchanged blocks/functions to reduce duplication and make a
   hand-improved replacement reusable. Preserve dispatch, PC, cycles and
   optimizer boundaries; measure rather than assume the benefit.
5. Add language-card physical backing and visibility tests from the design,
   including writes while ROM is visible and mapping-only switches. Then
   bring version-aware CFG identity to IR/IRC1 with focused optimizer tests.
   Neither is accomplished merely by adding more bitfields.

## Planning findings and open gates

- **2026-11-20:** The current collector marks an instruction only when its
  opcode address was previously written, and snapshots later memory on
  `saveGeneration()`. Thus a list of existing bitsets is useful groundwork,
  but cannot alone preserve preloaded instructions or the bytes executed.
- **2026-11-20:** Existing run-data already includes generation-specific byte
  segments. Robotron records `$60` and `$A5 $CA` at `$4C6E` in generations
  1 and 2; the directed example also records both `$0900` versions. Reuse and
  validate these bytes first, adding collection data only for demonstrated
  gaps. A saved segment is not a complete execution history.
- **2026-11-20:** `FirstInstructions` is global per PC and stores a single
  opcode and operand-change flag. Per-version observations must not overwrite
  it or turn its legacy consumers into accidental generation-aware consumers.
- **2026-11-20:** Whole-switch duplication is an initial implementation
  choice, not a complete answer to Question 2. A global state may not represent
  separately modified regions or every partial-write combination.
- **Open:** What evidence suffices to decide that an overlay is ready without
  checking every opcode or switching on each store? Which write sources can
  bypass generated helpers? How are ambiguous matches and unobserved paths
  detected before execution? Resolve in Task 5 and document the resulting
  limitations.
- **Open:** How do boundary splitting, cycle accounting, probes, tracing and
  host mutations interact in real runs? Measure before generalizing the
  directed implementation.
