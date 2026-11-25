# Generation-aware Simple C: block-oriented dispatch implementation plan

Status: **Iteration 0 complete; Iteration 1 implemented and awaiting human
review at Gate 1.** Dispatch behavior is unchanged; later iterations remain
proposed only.
This plan follows the [living design](2026-11-17-code-generation-switching.md)
and the [first implementation plan](2026-11-20-code-generation-switching-implementation.md).
The current emitter is the functional baseline, not the desired output shape.

## Goal

Generate large, readable, contiguous sequences of fixed 6502 operations in
Simple C. Decide whether to use a code version at a block entry or a necessary
safe continuation, rather than emitting generation checks between every
instruction. Use write-driven metadata so normal dispatch does not repeatedly
read opcode bytes from simulated memory. Preserve the existing opt-in behavior
and fail closed when no safe generated code can be selected.

This is an iterative plan, not permission to implement all steps at once. Each
numbered iteration below ends in a **human review gate**. Stop at that gate,
present generated C and test evidence, and wait for feedback before starting
the next iteration. Record feedback and adjust this plan and the living design
before proceeding. A reviewer may request another iteration instead of the
proposed next one.

## Baseline and scope

The current `--generation-aware` emitter materializes complete low-memory
switches, computes signatures and calls a matcher when its global dirty flag
is set. Its generated code has a guard between instructions, plus duplicated
resume suffixes. The shared write helper marks the flag for every low-memory
write. The selected example
[`tests/1.c`](../../tests/1.c) illustrates the resulting shape; it is a
local generated inspection artifact, **not** a required fixture, input or
checked-in baseline. Regenerate an example from repository fixtures instead
of relying on that file or its accompanying local JSON.

Run-data contains partial generation byte segments and `FirstInstructions`,
not complete memory snapshots or a per-write event log. Materialized
candidates therefore do not prove every runtime combination or transition.
The metadata may validate only the static constraints actually represented;
unseen executable code and unsupported combinations still require explicit
failure.

The initial implementation has useful directed replay, ambiguity, timing and
yield tests. Retain them as regression evidence, not proof of complete
correctness or a prescription for continuing the current placement of
checks. Keep ordinary Simple C and unsupported-option diagnostics unchanged.
Do not extend this iteration to language-card code, ROM, IR, arbitrary host
mutations, independent-region combinations or unrecorded code.

## Proposed mechanism

The first candidate is a **write-maintained validity index**:

1. Assign stable IDs to materialized candidate generations and their emitted
   blocks or safe continuation segments. IDs are local to the generated
   program. The same address can have different layouts, lengths and block
   memberships in different candidates; do not identify a block solely by
   its PC.
2. Record each static byte on which each candidate or executable segment
   depends: opcodes and any operands embedded as literals. Dynamic operands
   need not constrain a version for that instruction, but the same address
   must still be tracked if it is static elsewhere, including an opcode in an
   overlapping instruction stream. Deduplicate identical address/expected
   byte constraints within each validity unit.
3. Use a generation-by-block/segment validity structure, or an equivalent
   compact representation, with mismatch counts and an address-to-constraint
   reverse index. A candidate is valid exactly when all of its represented
   static constraints match. Keep whole-candidate validity distinct from
   block/continuation validity; a valid segment does not establish that a
   partially written whole state is valid.
4. Initialize the index from the actual generated startup RAM *after*
   initialization, then update affected constraints from each tracked write's
   actual address, old value and new value. The index must represent unknown
   values and restoration, not merely set a sticky dirty bit. Avoid simulated
   I/O reads. Initialization and deliberate reset/reinitialization may
   inspect known low-memory RAM; normal dispatch reads metadata, not RAM
   opcode bytes.
5. At an entry or safe continuation, use the index to determine whether the
   current generated code remains safe or whether an unambiguous supported
   candidate can be selected. A complete globally matching state and a unique
   current-segment match are different results. A partial write elsewhere must
   remain unresolved even if the current segment can safely run. Reject
   unknown and ambiguous code before the affected instruction executes.
   Never assume that the next chronological recording entry is the correct
   runtime generation.

This is a reviewable starting design, not a requirement to use a dense
two-dimensional array. Size, cost and readability must be measured before
settling on its layout. A counter must never overflow or underflow; use a type
whose range is checked against the number of constraints. An ordinary write
that cannot affect any static constraint changes no validity state, and
writing the already present byte must not invalidate a matching candidate.

### Writes and safe boundaries

Generation-time analysis may elide tracking at a store only if it proves
that *every possible effective address* of that write cannot affect any
static constraint. Dynamic indexed and indirect addresses, read-modify-write
operations, and stack writes need conservative handling unless equivalently
proved safe. Update metadata in or through the common low-memory write path,
including public `ram_poke` and generated stack pushes; do not assume a
recorded store PC lists all possible writes. Preserve existing memory and I/O
semantics, and keep ordinary non-generation-aware builds unaffected. Direct
host mutation outside the supported helpers and language-card mapping changes
remain excluded unless separately designed.

An original basic-block entry alone cannot protect a store that changes a
later instruction in the same block. For example, the existing
[`generation-same-block.s`](../../tests/generation-same-block.s) writes
`$0305` at `$0302`, immediately before executing `$0305`. Conservatively
identify stores that can invalidate the unexecuted suffix. End a contiguous
segment after such a store or insert a targeted continuation check there,
before any affected instruction. Do not insert checks after stores proven
irrelevant, or between non-writing instructions. Runtime-computed stores
may require such a boundary even when a particular execution writes data.
Do not split every instruction merely to simplify metadata.

The same-block fixture proves this behavior is possible, not how common it is
in real programs. The working assumption—an educated guess, not an established
fact—is that stores which modify a later instruction in their own original
block are exceptional. Bias the normal generated path toward one block-entry
decision and intact fixed-code blocks; use targeted post-store boundaries as
an exceptional path when static analysis or conservative effective-address
handling says a write may hit the remaining suffix. Do not let the fixture
justify a guard after every instruction or every store. Keep runtime-computed
targets conservative, and revisit this assumption if broader recordings or
human review show such cases are common.

Keep the original block's `CYCLES`, branch-target/debug and probe semantics
when selecting a continuation. A synthetic continuation is not automatically
a new observable block entry. Account for the unexecuted suffix if a selected
version changes its cycles or instruction layout, including changed lengths.
If the emitter cannot establish a compatible continuation and correct cycle
accounting, retain a safe existing fallback or report an explicit unsupported
case; never silently execute the old suffix.

## Required invariants and acceptance evidence

- **Static output:** for the directed switch fixture, show a representative
  original block containing multiple translated instructions with no
  generation checks between consecutive non-writing instructions. Apart from
  a block-entry guard and justified post-store continuations, the normal
  execution path must not scan simulated code bytes or call the old
  per-instruction matcher. Report guard counts and generated source size
  relative to the baseline; do not claim a runtime speedup without measuring.
- **Writes:** direct, indexed, indirect, read-modify-write, stack and public
  low-memory write paths must either update all affected constraints or be
  explicitly rejected/excluded before affected code executes. Check writes
  outside code, unchanged writes, writes to static operands, writes to dynamic
  operands, restoration, and one address belonging to overlapping streams.
  Initialization, reset and `set_regs` must not leave stale metadata.
- **Selection:** preserve known, unchanged, delayed, replaced and restored
  execution; do not accept partial or unknown code at an affected entry.
  Exercise both complete-state and current-segment ambiguity, as well as an
  intermediate partial overlay while executing an unaffected segment. A
  matching current segment is not permission to clear unresolved global
  validity. Handle an independently changed, unrecorded combination by a
  tested explicit diagnostic unless support is deliberately designed.
- **Boundaries:** preserve the same-block fixture and add a case where an
  indirect/indexed store writes a later opcode in its own block, and a
  control case where it writes ordinary data. Cover changed instruction
  length and targets/entries inside or overlapping an old block. No transition
  may jump into a byte that is merely an operand in the chosen version.
- **Observability:** compare interpreter and generated PC, registers, flags,
  stack and relevant memory at equivalent stop points; also compare cycle
  totals, debug/probe events and one-cycle repeated calls versus a larger
  budget. Preserve the existing test's normalization for generated block
  precharging and the interpreter's ROM-entry offset. Explicitly document
  which bytes and registers were compared; do not claim full-machine or I/O
  equivalence from selected snapshots.
- **Compatibility:** run the existing focused generation-switch tests and
  ordinary Simple C regressions, including generation without the option.
  Run the existing suite when practical and report unrelated baseline failures
  accurately. The legacy output must not gain this instrumentation.

## Iteration 0: baseline and review the design

**Files:** this plan and the living design; existing fixtures and generated
output inspected but not checked in.

1. Generate the directed fixture using the existing test's assembled binary
   and freshly collected run-data. Save the example outside tracked files.
2. Record the generated source size, number of whole-state and instruction
   checks, representative `$0300` and `$0900` blocks, and any resume-case
   duplication. Identify where actual write helpers are used. Do not estimate
   runtime improvement from source size alone.
3. Run the existing focused fixture once and record the precise result and
   known constraints. Note compiler/build environment and unrelated failures.
4. Map the existing materialized candidates, block entries, instruction
   starts, static operands and dynamic operands into a small example table.
   Work through `$0302`/`$0305` and one overlapping-byte example on paper.
5. Present the proposed metadata, candidate-selection rules, boundary and
   fallback choices, sample current output and measurements to the human.

### Iteration 0 findings

The baseline was freshly assembled and collected from
[`generation-switch.s`](../../tests/generation-switch.s), then emitted with
`--generation-aware`. The focused command `bash
tests/generation-switch-test.sh build` passed, including the directed
interpreter/replay, ambiguity, same-block, yield/event, and ordinary
no-option checks. The capture used the existing `build/` binaries; no build
was performed. Environment: CMake 3.28.3, Ninja 1.11.1, GNU C++ 15.2.0,
repository revision `6b57760`.

| Baseline measure | Observed |
| --- | ---: |
| Generated C size | 343,264 bytes; 7,368 lines |
| Run-data generations / `FirstInstructions` entries | 4 / 31 |
| Distinct emitted state functions | 2 |
| Basic-block switch entries | 6 per state |
| Dirty-guard sites | 282 |
| Whole-state matcher call sites | 282 |
| Current-PC fallback matcher call sites | 282 |
| Embedded whole-state RAM-byte comparisons | 144 |
| Duplicated resume cases | 50 |

The four run-data generations have PCs `$0300`, `$031E`, `$0330`, and `$0380`.
Their recorded code segments at `$0900` are respectively absent,
`A9 11 60`, `A2 22`, and `A9 11`. Applied cumulatively to the initial
`FirstInstructions` bytes, these represent `A9 11 60`, `A9 11 60`,
`A2 22 60`, and `A9 11 60`; the unchanged first install and final restoration
coalesce, leaving two distinct emitted code candidates. At `$0900`, the first
candidate is `LDA #imm; RTS`, the second is `LDX #imm; RTS`. `$0901` is the
observed dynamic immediate operand and is read at runtime; `$0900` and `$0902`
are the static opcodes.

The generated state functions each contain these six original entries:

| PC | Emitted block range | Size |
| --- | --- | ---: |
| `$0300` | `$0300-$0318` | 25 bytes |
| `$0319` | `$0319-$032A` | 18 bytes |
| `$032B` | `$032B-$033C` | 18 bytes |
| `$033D` | `$033D-$0342` | 6 bytes |
| `$0380` | `$0380-$0382` | 3 bytes |
| `$0900` | `$0900-$0902` | 3 bytes |

In the `$0300` block, the first generated instruction follows the block-entry
guard, and another complete dirty guard is emitted before each subsequent
instruction—even instructions which do not write memory. The block at `$0900`
has the same placement. The `$0309` store to `$0900` can affect that other
block; the current output nevertheless emits guards after every instruction,
not only after this meaningful write.

The existing [`generation-same-block.s`](../../tests/generation-same-block.s)
provides the key boundary counterexample: `STA $0305` at `$0302` changes the
opcode of the very next instruction in its original block. The independent
ambiguous fixture also illustrates overlapping roles: a candidate `JMP`
starting at `$0300` embeds byte `$0302` as its high operand, while another
candidate decodes `$0302` as an instruction start. A write index must retain
both constraints at that address.

A representative excerpt from the fresh `$0300` block (selection details
elided):

```c
case 0x0300: // [$0300..$0318]   25 bytes
  if (s_generation_dirty) {
    int selected = generation_match();
    /* ambiguity and fallback selection */
  }
  CYCLES(0x0300, 34);
  /* $0300 LDX */ s_x = update_nz(0xff);
  if (s_generation_dirty) {
    int selected = generation_match();
    /* same selector repeated before the next instruction */
  }
  /* $0302 TXS */ s_sp = s_x;
```

These are source-shape measurements, not runtime performance measurements.
The exact generated file was temporary and was not checked in. The focused
suite passed; this does not establish full architectural-state or general
program equivalence.

**Review gate 0:** agree on the first implementation slice and output shape
before changing the generator. The reviewer approved Iteration 1 as drafted
and requested the same-block rarity assumption above. Continue with Iteration
1; update this plan and the living design with further feedback at each later
gate. Keep subsequent iterations provisional.

## Iteration 1: maintain write-driven validity alongside existing dispatch

**Files:** primarily [`PrintSimpleC.cpp`](../../tools/apple2tc/PrintSimpleC.cpp),
[`Disas.h`](../../tools/apple2tc/Disas.h) if needed,
[`system-inc.h`](../../include/apple2tc/system-inc.h), and focused tests.

1. Derive and deduplicate candidate/segment static constraints using the same
   decoded layouts and operand rules the emitter uses. Detect and report
   inconsistent metadata rather than silently dropping a constraint.
2. Emit a bounded address-to-constraint index and validity storage. Keep
   whole-state and executable-segment counts independently addressable.
   Choose representation and counter types based on actual generated sizes;
   reject a representation whose bounds cannot be satisfied.
3. Initialize counts after memory initialization using direct low-memory
   bytes. Ensure reset, reinitialization and public `set_regs` paths maintain
   a valid invariant even when RAM has changed. Explicitly test that startup
   data synthesized from recorded first observations is not treated as proof
   of unobserved runtime memory.
4. Update counts on the common write path using the old and new byte values.
   Cover stores, read-modify-write, stack pushes and public `ram_poke`. Avoid
   dirtying or changing counts for unrelated and unchanged writes.
5. Add focused tests for matching, mismatching, unknown and restored bytes,
   overlapping constraints, dynamic versus static operands, repeated writes,
   initialization/reset, and computed-address/stack writes. During this
   iteration keep the existing dispatch as the authority; the new bookkeeping
   must not change its behavior. Test the metadata directly or compare it
   against independently calculated expectations, not against itself.
6. Produce representative emitted declarations and write sites; report their
   size and the work done per affected and unaffected write.

### Iteration 1 findings

The emitter now builds a sparse address-to-constraint index for the
representative materialized states and each candidate's original blocks.
Whole-state validity and block validity are separate units. Initialization
reads `s_ram` directly after generated memory setup; `ram_poke_impl` supplies
old/new values to the tracker, which binary-searches the address table and
updates only affected mismatch counts. Identical writes skip the lookup.
`reset_regs` rebuilds the counts after clearing RAM; `set_regs` leaves them
valid because it changes registers, not memory. Existing dispatch remains the
authority: its dirty checks, matchers and resume cases are still emitted and
run exactly as before.

On the freshly captured switch fixture, the validity metadata has 14 units
(2 whole states plus 12 candidate blocks), 288 constraint entries and 72
indexed addresses. The maximum address fan-out is 4 units. A write to a
constrained byte therefore performs a binary search over at most 72 addresses
(at most 7 loop iterations), then checks at most 4 constraints; an unindexed
write returns after that search, and an unchanged write returns before it.
Initialization compares all 288 constraints with direct RAM bytes. Generated
output is 351,582 bytes / 7,813 lines, 8,318 bytes larger than the Iteration 0
baseline. A decrement that would violate the mismatch-count invariant reports
the address and aborts even in release builds; the generator also checks
aggregate constraint-count overflow. These are source/storage and
operation-count observations, not runtime measurements.

Added direct metadata checks recompute every unit's mismatch count from RAM
after writes, initialization, reset and generated replays. Coverage includes
unchanged and restored bytes, dynamic immediate operands, computed-address
`ram_poke`, stack pushes, `set_regs`, and the `$0302` byte that is an RTS
opcode in one state but part of an overlaid JMP operand in others. A focused
[`generation-validity-rmw.s`](../../tests/generation-validity-rmw.s) replay
checks the generated `INC` path. The metadata invariant also passed after
switch, delayed, preloaded and same-block generated replays. Command
`ninja -C build tools/apple2tc/apple2tc` and
`bash tests/generation-switch-test.sh build` passed; `bash -n
tests/generation-switch-test.sh` and `git diff --check` passed.

The same-block frequency assumption from Review Gate 0 is recorded above:
the fixture proves possibility, not prevalence, so the common path should
favor block-entry dispatch while conservatively isolating possible suffix
hazards.

**Review gate 1:** review the write-maintained representation, initialization
and update costs, generated declarations/helper, and test evidence above.
Record feedback and adjust both the living design and this plan. Do not remove
the existing per-instruction guards merely because metadata tests pass.

Review gate 1 was approved; the user directed work to continue to Iteration 2.

## Iteration 2: contiguous blocks and targeted write boundaries

**Files:** emitter, runtime metadata, and generation-switch fixtures/drivers.

1. Use the existing block and candidate layouts to determine, for each
   instruction that can write, whether its possible targets intersect the
   remaining static bytes of the current block or an overlapping stream.
   Treat unknown effective addresses conservatively. Show the proof used for
   any store for which a boundary or tracking is omitted.
2. Replace unconditional instruction-by-instruction checks with a block-entry
   metadata check and checks only at necessary post-write continuations. A
   simple initial slice may still call the existing selector at those
   boundaries; keep it as a clearly marked transitional fallback, not the
   final no-scan design. Do not remove checks on a path until every earlier
   write on that path is accounted for.
3. Reuse an existing resume mechanism only where necessary. Preserve original
   event boundaries and adjust only the cycles of the unexecuted suffix.
   Verify new and old versions can resume at the same actual instruction
   start; otherwise keep a safe fallback or explicitly reject.
4. Test entry selection, a store modifying the next instruction, a computed
   store hitting and missing the current suffix, partial code in another
   region, changed lengths, unknown bytes and ambiguity. Compare interpreter
   timing, register/memory effects, probes, debug events and repeated calls
   under the existing normalization.
5. Regenerate the directed example and show complete representative blocks,
   not only grep counts. Confirm no checks between ordinary non-writing
   instructions and no artificial probe/debug events at continuations.
   Record guard counts, source size and remaining selector scans.

**Review gate 2:** human reviews contiguous output and exceptional boundaries.
Update both plans based on feedback before attempting to remove the fallback.

### Iteration 2 findings

The emitter now keeps ordinary instructions contiguous and emits a dirty
selector site at each original block entry. Within a block, a selector site is
emitted only at the instruction following a write whose possible destination
can intersect static bytes in that block's remaining suffix. Absolute and
zero-page targets are compared exactly; indexed stores use their full address
range; indirect stores are conservative. Stack pushes are considered only
when the remaining bytes intersect page `$0100-$01FF`. Stores with dynamic or
changed target operands remain conservative. Suffix byte sets exclude known
dynamic operands unless they are static instruction bytes in another role.
Potential continuation PCs are unioned across candidate states so alternate
instruction layouts retain safe resume cases.

The common `$0300-$0318` block in the switch fixture now has only its entry
guard; its stores to `$0900-$0902` do not intersect its own static suffix.
Generated C shrank from 351,582 bytes / 7,813 lines after Iteration 1 to
41,294 bytes / 1,159 lines. The fixture has 12 dirty-guard and 12 selector
sites (two candidate states times six blocks), down from 282 sites; it has no
interior resume cases. This is a source-shape change, not a measured runtime
speedup. The same-block fixture emits 8 guards and 2 resume cases across its
states; the direct `$0302 STA $0305` and indirect `$030C STA ($10),Y` both
place the continuation check between the store and `$0305`/`$030E`. The
indirect fixture changes the next opcode and its generated result matches the
interpreter. The delayed fixture also exercises an indirect write to code
outside the current suffix.

The following Iteration 2 output, captured before Review Gate 2 and the
metadata-selector change, shows one complete same-block entry case (one of two
candidate states). It retains contiguous fixed C instructions and places the
only interior selector immediately after the hazardous store:

```c
    case 0x0300: // [$0300..$030C]   13 bytes
      if (s_generation_dirty) {
        int selected = generation_match();
        bool completeMatch = selected >= 0;
        if (selected == -2) { fprintf(stderr, "Ambiguous generation state at $%04X\n", s_pc); error_handler(s_pc); return; }
        if (selected < 0) {
          if (generation_instruction_matches(s_active_generation, s_pc)) {
            selected = (int)s_active_generation;
          } else {
            selected = generation_match_at_pc(s_pc);
            if (selected == -2) { fprintf(stderr, "Ambiguous generation instruction at $%04X\n", s_pc); error_handler(s_pc); return; }
            if (selected < 0) { fprintf(stderr, "Unknown generation instruction at $%04X\n", s_pc); error_handler(s_pc); return; }
          }
        }
        if (completeMatch) s_generation_dirty = false;
        if ((unsigned)selected != s_active_generation) {
          s_pc = 0x0300;
          s_active_generation = (unsigned)selected;
          *branchTargetRef = branchTarget;
          return;
        }
      }
      CYCLES(0x0300, 15);
      /* $0300 LDA */ s_a = update_nz(0xa2);
      // WARNING: performs self modification.
      /* $0302 STA */ poke(0x0305, s_a);
      if (s_generation_dirty) {
        int selected = generation_match();
        bool completeMatch = selected >= 0;
        if (selected == -2) { fprintf(stderr, "Ambiguous generation state at $%04X\n", s_pc); error_handler(s_pc); return; }
        if (selected < 0) {
          if (generation_instruction_matches(s_active_generation, s_pc)) {
            selected = (int)s_active_generation;
          } else {
            selected = generation_match_at_pc(s_pc);
            if (selected == -2) { fprintf(stderr, "Ambiguous generation instruction at $%04X\n", s_pc); error_handler(s_pc); return; }
            if (selected < 0) { fprintf(stderr, "Unknown generation instruction at $%04X\n", s_pc); error_handler(s_pc); return; }
          }
        }
        if (completeMatch) s_generation_dirty = false;
        if ((unsigned)selected != s_active_generation) {
          s_cycles -= 9;
          s_remaining_cycles += 9;
          s_pc = 0x0305;
          s_active_generation = (unsigned)selected;
          *branchTargetRef = branchTarget;
          return;
        }
      }
      // WARNING: opcode self modification.
      /* $0305 LDA */ s_a = update_nz(0x11);
      /* $0307 STA */ poke(0x0200, s_a);
      /* $030A JMP */ s_pc = 0x0380;
      branchTarget = true;
      break;
```

At the end of Iteration 2, the existing selector remained the transitional
fallback: each emitted site called `generation_match()` and could call
`generation_match_at_pc()`, which scanned simulated bytes. The Iteration 1
mismatch counters were maintained but were not yet the selector authority.
The `$0305` continuation's cycle
restoration and original block-level probes/debug events are covered by the
existing direct same-block repeated-call tests; the new indirect-hit fixture
checks generated versus interpreter RAM. The focused command
`bash tests/generation-switch-test.sh build`, `bash -n
tests/generation-switch-test.sh`, `ninja -C build tools/apple2tc/apple2tc`,
and `git diff --check` pass.

The static boundary analysis and tests are still a constrained first slice:
the indirect-hit fixture verifies conservative post-store selection, while
delayed indirect writes exercise a destination outside the current suffix.
At this point the selector still used full memory matching and the metadata
was not authoritative; broader state/overlap/layout combinations were also
unproved. These limits motivated Iteration 3.

Review gate 2 was approved. The user confirmed that block-oriented output is
the right direction and that line/character counts are useful evidence of
improvement, not the optimization target.

## Iteration 3: select from maintained metadata, not memory scans

**Files:** emitter and focused generated-C tests; runtime helpers if needed.

1. Replace boundary-time whole-memory/signature scans with candidate and
   block-validity mismatch counters. Index candidate block units by dispatch
   PC, prefer the active generation when its unit remains valid, and use
   complete-state counters to retain global ambiguity/fail-closed behavior.
   Do not just move the existing scans from every instruction to every block.
2. Define and test exact precedence: detect incompatible complete matches;
   accept a complete unique match when safe; while no complete match exists,
   permit an unchanged current segment without clearing unresolved state, or
   select a uniquely matching safe continuation. Reject zero or incompatible
   multiple segment matches before execution. Preserve the initial path's
   fail-closed ambiguity cases; do not treat equivalent-looking PCs as
   equivalent bodies without checking their layout and static semantics.
3. Verify caches after writes in either direction, repeated writes and
   restoration; a write in one region must not validate unrelated unknown
   bytes. Check independently changing regions with both known and unrecorded
   combinations, and explicitly reject combinations the selected
   representation cannot safely support.
4. Remove now-unused per-instruction matching and duplicated continuation
   cases only after proving no entry, overlap or alternate candidate needs
   them. Ensure unsupported entries fail with a specific PC and reason.
5. Run the acceptance evidence above. Inspect generated C to verify that
   dispatch uses maintained metadata rather than simulated opcode reads;
   dynamic operand reads remain allowed. Report code size, guard counts,
   storage cost and, separately, any measured runtime and build costs.

### Follow-up TODOs from Review Gate 2

- **Narrow the block selector further:** use the caller's generation and the
  exact instruction range represented by the current block or remaining
  suffix to avoid checking unrelated candidate/state units when the current
  block remains valid. The Iteration 3 implementation removes RAM scans and
  indexes block-validity units by dispatch PC; refine candidate work only with
  tests for partial states and ambiguous matches.
- **Specialize post-write continuation selection:** the write hook receives
  the effective address and old/new byte values. Carry the relevant write fact
  to the immediately following targeted continuation so it can reconsider
  only affected candidate constraints instead of reusing the general
  block-entry selector. Preserve conservative handling for writes that may
  hit multiple instructions, dynamic targets, or unknown code.

These are explicit follow-up TODOs, not additional Iteration 3 acceptance
criteria.

### Iteration 3 findings

The generated selector no longer reads simulated RAM. A complete-state check
reads the whole-state mismatch counters; the PC selector considers only
candidate block-validity units containing that dispatch PC, prefers the
caller-specified generation when its unit remains valid, and otherwise
requires a unique matching candidate. A unique complete state takes
precedence and clears the dirty flag; partial states keep it set. Ambiguous
complete states and ambiguous block matches fail closed with specific
diagnostics. Per-instruction RAM matching and the whole-memory signature
matcher have been removed.

The write tracker now reports whether a changed byte affected any indexed
constraint. Writes to unindexed data and dynamic-only operands leave the
dirty flag clear; writes that change static-code validity set it. Direct
metadata tests verify both behaviors, and the existing repeated-write,
restoration, reset and `set_regs` cases continue to verify counter invariants.
For the switch fixture, this retains 14 validity counters, 288 byte
constraints and 72 indexed addresses from Iteration 1; the selector uses
per-dispatch-PC candidate-unit expressions rather than a dense
generation-by-block table.

The focused command `ninja -C build tools/apple2tc/apple2tc` and
`bash tests/generation-switch-test.sh build` pass. The generated switch
fixture is 29,833 bytes / 874 lines, with 12 dirty guards, 12 block-selector
sites and 12 complete-state counter checks; it contains no interior resume
cases. The same-block fixture is 17,446 bytes / 431 lines, with 8 guards,
8 block-selector sites and 2 resume cases. These counts describe output
structure, not a runtime-performance target or measurement. Tests also
assert that generated selector helper bodies contain no `s_ram[...]` reads.
These directed tests do not establish correctness for independently changing
unrecorded regions, every partial-overlay ordering, or realistic game-sized
recordings.

The generated `generation_validity_unit_t` descriptor table was not read by
runtime code; only test drivers used it to label counters. It has been removed
from generated output. Tests now iterate the mismatch-counter array directly
and use the whole-state index array where candidate identity is required. An
output-shape assertion prevents the unused table from returning.

### Generated validity metadata layout

The generated C documents the two remaining validity structs inline:

| Type | Fields | Meaning and invariant |
| --- | --- | --- |
| `generation_validity_constraint_t` | `unit`, `expected` | For the RAM address whose index entry references it, validity unit `unit` expects byte `expected`. The unit is an index into `s_generation_validity_mismatches`. |
| `generation_validity_address_t` | `address`, `first`, `count` | `address` is a low-memory address; `[first, first + count)` selects the constraints for that address in the sorted `s_generation_validity_constraints` array. |

Each mismatch counter is the number of that unit's expected bytes that
currently differ from RAM. Initialization computes the counts from RAM; a write
updates only constraints in that address's slice, using the old and new
values. `s_generation_whole_state_units[candidate]` maps a candidate state to
its whole-state counter. Candidate block-unit indexes are embedded in the
generated dispatch-PC selector, so no emitted descriptor table is needed.

Representative generated metadata selector and same-block case:

```c
static int generation_complete_match(void) {
  int matched = -1;
  if (s_generation_validity_mismatches[0] == 0) {
    if (matched >= 0) return -2;
    matched = 0;
  }
  if (s_generation_validity_mismatches[1] == 0) {
    if (matched >= 0) return -2;
    matched = 1;
  }
  return matched;
}

    case 0x0300: // [$0300..$030C]   13 bytes
      if (s_generation_dirty) {
        int complete = generation_complete_match();
        if (complete == -2) { fprintf(stderr, "Ambiguous generation state at $%04X\n", 0x0300); error_handler(0x0300); return; }
        int selected = generation_match_at_pc(complete >= 0 ? (unsigned)complete : s_active_generation, 0x0300, complete >= 0);
        if (selected == -2) { fprintf(stderr, "Ambiguous generation block at $%04X\n", 0x0300); error_handler(0x0300); return; }
        if (selected < 0) { fprintf(stderr, "Unknown generation instruction at $%04X\n", 0x0300); error_handler(0x0300); return; }
        if (complete >= 0) s_generation_dirty = false;
        if ((unsigned)selected != s_active_generation) {
          s_pc = 0x0300;
          s_active_generation = (unsigned)selected;
          *branchTargetRef = branchTarget;
          return;
        }
      }
      CYCLES(0x0300, 15);
      /* $0300 LDA */ s_a = update_nz(0xa2);
      // WARNING: performs self modification.
      /* $0302 STA */ poke(0x0305, s_a);
      if (s_generation_dirty) {
        int complete = generation_complete_match();
        if (complete == -2) { fprintf(stderr, "Ambiguous generation state at $%04X\n", 0x0305); error_handler(0x0305); return; }
        int selected = generation_match_at_pc(complete >= 0 ? (unsigned)complete : s_active_generation, 0x0305, complete >= 0);
        if (selected == -2) { fprintf(stderr, "Ambiguous generation block at $%04X\n", 0x0305); error_handler(0x0305); return; }
        if (selected < 0) { fprintf(stderr, "Unknown generation instruction at $%04X\n", 0x0305); error_handler(0x0305); return; }
        if (complete >= 0) s_generation_dirty = false;
        if ((unsigned)selected != s_active_generation) {
          s_cycles -= 9;
          s_remaining_cycles += 9;
          s_pc = 0x0305;
          s_active_generation = (unsigned)selected;
          *branchTargetRef = branchTarget;
          return;
        }
      }
      // WARNING: opcode self modification.
      /* $0305 LDA */ s_a = update_nz(0x11);
      /* $0307 STA */ poke(0x0200, s_a);
      /* $030A JMP */ s_pc = 0x0380;
      branchTarget = true;
      break;
```

The generated selector bodies contain no opcode-memory reads; runtime reads
remain in generated instruction expressions for dynamic operands.

**Review gate 3:** show final representative generated blocks, metadata and
results. The human decides whether the design satisfies the intended style
and whether another refinement iteration is needed.

Review Gate 3 was approved by the user. The two follow-up TODOs above remain
open; no additional refinement iteration has been started.

## After review

Update the living design, this plan and
[`2026-11-23-context.md`](../../2026-11-23-context.md) after each accepted
iteration with the exact implementation, measured output and limitations.
Do not mark later steps complete without their tests and human review.
Broader state coverage, independently changing regions, shared blocks,
language-card mappings, IR/IRC1 and realistic game measurements remain
separate follow-up work.
