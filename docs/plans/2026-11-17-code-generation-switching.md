# Code generations in generated C

Status: living design document. The first low-memory iteration and its
test-driven steps are described in the [implementation plan](2026-11-20-code-generation-switching-implementation.md).
Later questions and optimizations remain open. Record implementation findings
and changes of direction here as the work progresses.

## Goal

Support programs that replace executable instructions at addresses they have
already used, while retaining static, optimizable basic blocks. A generated
program must select the appropriate previously generated code as a consequence
of the program's actual memory and control-flow behavior, not at a recorded
cycle count. Different user input can change both when and whether a replacement
occurs.

This must work in ordinary low memory. The design should also extend to code
whose visible bytes change when the language card selects ROM or a RAM bank,
without requiring a separate decompiler architecture for upper memory.
Language-card behavior need not be the first implementation step.

### Why static code matters

The desired output is not an interpreter that fetches and dynamically decodes
each opcode. Fixed-opcode basic blocks expose operations and control flow to
Apple2TC's IR and optimization passes. [Part 6 of the Apple2TC blog][blog-ir]
explains why decomposing 6502 instructions into explicit IR operations makes
their data flow and side effects analyzable. [Part 7][blog-opt] demonstrates
local SSA conversion, dead-code elimination, constant folding, and a
conditional branch becoming unconditional when its condition is proven.

The longer-term goal is readable, maintainable C as well as runnable C.
[The Snake Byte design](../superpowers/specs/2026-08-02-snake-byte-proper-c-design.md)
aims for real functions, structured control flow, and meaningful names rather
than an emulator expressed in C. Requiring opcode reads and dynamic decoding
at every PC would make both optimization and that transformation harder.

An instruction's **opcode** determines its static operation and instruction
size. Its **operand** may still need a runtime memory read if modified or if a
different mapping exposes a different value. Unchanged operands should remain
constants where sound. Operand changes alone should not automatically require
a new opcode generation. However a single isolated operand could be a candidate
for a new generation if hard-coding that value would lead to meaningful optimization
opportunities. That last bit would be a later goal rather than part of the 
initial implementaion.

### Constraints

- Preserve the semantics of registers, flags, stack, memory, I/O, control flow,
  and timing. A transition is not just a change to the next PC.
- Keep unaffected code shared where possible, so a hand-improved function or
  basic block does not have to be maintained once per generation.
- Prefer large stable optimization regions, but never execute stale
  instructions merely to avoid a transition.
- Avoid switching generations on every store to an instruction. It may still
  be necessary to track writes cheaply; transition only when needed and at a
  correctness-preserving boundary. The goal here is to ideally have large
  blocks of code swapped out at each such boundary rather than uncountable
  numbers of single instruction changes.
- Do not assume that a recording covers every possible input, code version,
  bank mapping, or control-flow edge. State explicitly what is supported and
  report unsupported execution rather than silently running stale code.
- The runtime cost of detecting changes and dispatching versions matters.
  Measure it separately from the size, readability, and optimizability of the
  generated code.

## What exists today

[Part 4 of the blog][blog-dynamic] introduces the original memory-generation
collector using BASIC's dynamically installed zero-page `CHRGET` routine. It
describes recording written-then-executed code and closing a generation before
an executed address is overwritten. This is useful prior art, not a complete
runtime generation-switching design.

The current [collector](../../lib/cpuemu/DebugState6502.cpp) uses entry 0 of
`GenerationMemoryTracking`:

- `written` (formerly `memWritten_`) for addresses observed written; saved
  executable ranges are cleared from it, while other written addresses can
  remain marked.
- `execStart` (formerly `memExecStart_`) for instruction starts executed after
  their opcode address was written; `execFull` (formerly `memExecFull_`)
  marks the corresponding instruction bytes.
- A later write to a marked instruction start triggers `saveGeneration()`
  before the write. It saves the marked bytes from the then-visible memory
  view; collection also saves a final generation at the end.
- `firstInstructions_` separately records the first observed instruction bytes
  at each collected PC and whether its operand was observed to change. The
  bitsets do not store those values; `execStart` and `execFull` are
  reset during generation saves.

These are collection-time observations, not a runtime switch mechanism.
The existing trigger is specifically a write to an instruction **start** that
was previously written and then executed. It does not by itself prove that
every earlier opcode version, operand update, or physical bank is represented.
In particular, a snapshot taken later under a different mapping can differ
from the bytes seen when the instruction executed.

### Existing recorded generation bytes

The run-data JSON already contains useful byte contents for multiple
generations, not just bitfields. Its `generations` array has an object per
saved generation, each containing `regs` and a `code` array. Each code entry
is a segment with an `addr` and a `bytes` array. The registers are captured
when that generation is saved, not necessarily when its code first executed.
For example, the committed [Robotron recording](../../decoded/robotron/robotron.json)
contains these two segments at the same decimal address, 19566 (`$4C6E`):

```json
{"addr": 19566, "bytes": [96]}
{"addr": 19566, "bytes": [165, 202]}
```

They occur in generations 1 and 2, respectively: `$60` (`RTS`) and `$A5 $CA`
(`LDA $CA`). The recorded segments in the directed experiment likewise
contain both `$A9 $11 $60` and `$A2 $22` at `$0900`. These bytes should be
used as evidence and tested as input to generation-aware disassembly rather
than discarded or recollected by default.

The qualification is that each `code` array is a **partial saved segment
set**, not a full memory snapshot or a complete instruction-by-instruction
execution trace. Segment membership derives from the bitfields above, and
the bytes are copied from memory at save time. Thus absence from a segment
does not prove a byte was unchanged, unexecuted or inherited, and the segment
does not by itself identify all instruction starts, operand changes or
runtime transition conditions. Preloaded code and the original binary provide
additional initial-state evidence; `FirstInstructions`, when present, provides
the first observed bytes per PC, but not every later version. A generation-aware
consumer should reuse these sources where sufficient, detect ambiguities, and
require additional observations or recollection only when needed.

[Disas::run](../../tools/apple2tc/Disas.cpp) currently loads recorded segments
into one address space; in the non-language-card path, an overlapping segment is
skipped. The language-card path fills previously unmapped portions, but does
not create independently executable generations. Simple C emits fixed
instructions and uses runtime operand reads where modification is detected.
The [EOS README](../../decoded/eos/README.md#experimental-language-card-support)
documents the current first-opcode limitation and incomplete EOS validation.

An earlier experimental implementation in commit `4684ad6` recorded upper
memory opcode variants and generated a nested opcode switch at each recorded
PC. It rejected unrecorded variants and guarded changed low-memory opcodes
rather than supporting them. This is useful prior art for diagnostics and
variant collection, but it dispatched per instruction, made all upper operands
dynamic, and did not provide general generation-aware CFGs or shared basic
blocks. It was superseded by the current first-observed-opcode approach.

## Concrete cases

### Directed low-memory experiment

This is the small experiment run against `origin/master` at `e095c14`. The
loaded binary does not include `$0900`; it installs and executes two routines
at that address:

```asm
        org     $300
        lda     #$a9            ; LDA #$11
        sta     $0900
        lda     #$11
        sta     $0901
        lda     #$60            ; RTS
        sta     $0902
        jsr     $0900
        sta     $0200

        lda     #$a2            ; LDX #$22
        sta     $0900
        lda     #$22
        sta     $0901
        ldx     #$00
        jsr     $0900
        stx     $0201
        jmp     $0340

        org     $340
done:   jmp     done
```

The interpreter reaches `done` with `$0200 = $11` and `$0201 = $22`. Its
recording contains `$A9 $11 $60` in one generation and `$A2 $22` in a later
one. The Simple C generator loads the first segment and skips the overlapping
second segment. It emits `LDA` at `$0900` with a dynamic operand, so the
generated run instead leaves `$0201 = $00`. A future directed regression
should compare both results and fail on this mismatch; it should not encode the
current incorrect result as the desired behavior.

### Current-branch directed fixtures

The first implementation task adds two reproducible fixtures. From `tests/`,
run `bash generation-switch-test.sh ../build` after building the tools. It
assembles the programs, collects with `a2run --collect --limit=20`, checks
interpreter snapshots and inspects fresh run-data. Run the optional
`bash generation-switch-test.sh ../build --reproduce` to compare current
Simple C with the interpreter; this currently exits with an error:

```text
Interpreter: $0200=11 $0201=22 $0202=11
Generated:   $0200=11 $0201=00 $0202=11
```

In [`generation-switch.s`](../../tests/generation-switch.s), `$0900` executes
`LDA #$11`, then `LDX #$22`, then the restored `LDA #$11`. At `$0380` the
interpreter has A=`$11`, X=`$22`, Y=`$00`, SP=`$FF`, status=`$20`, and the
three result bytes above. The recording contains:

| Index | Saved PC | Segment at `$0900` | Reason |
|---|---:|---|---|
| 0 | `$0300` | none | Empty initial entry created when collection starts |
| 1 | `$031E` | `$A9 $11 $60` | Saved before the opcode is replaced |
| 2 | `$0330` | `$A2 $22` | Saved before the opcode is restored |
| 3 | `$0380` | `$A9 $11` | Final save when the bounded collection stops |

`$0902` remains the original `RTS`; it was executed each time but is absent
from the later partial segments. `FirstInstructions` has `$A9 $11 $60` and
`operandChanged=true` at `$0900`, and records the `RTS` at `$0902`. This
flag compares later operand bytes with the first instruction's operand bytes,
even when the opcode itself has changed; it is not a per-version operand
history. The saved PCs identify collector events, not sufficient conditions
for switching at runtime.

[`generation-preloaded.s`](../../tests/generation-preloaded.s) first executes
preloaded `$A9 $11 $60` at `$0400`, then replaces it with the longer
`$AE $10 $02 $60` (`LDX $0210; RTS`). At `$0380` the interpreter has
`$0200=$11`, `$0201=$22`, A=`$60`, X=`$22`, Y=`$00`, SP=`$FF`, and
status=`$20`. Its initial generation is empty; the final saved segment
contains only the replacement bytes. The original bytes are available in the
input binary and `FirstInstructions`, not a saved code segment. This
demonstrates both the usefulness and the incompleteness of the existing
segments without establishing that every recording needs new fields.

### Existing programs

- [Robotron's recording](../../decoded/robotron/robotron.json) contains `$60`
  (`RTS`) and later `$A5 $CA` (`LDA $CA`) at `$4C6E`. Its
  [generated Simple C](../../decoded/robotron/robotron.c) writes those opcode
  bytes but hard-codes the `LDA` at `$4C6E`. The recording demonstrates the
  versions occur; it does not establish that an existing automated replay
  checks the affected path.
- [Bolo's recording](../../decoded/bolo/bolo.json) contains many overlapping
  snapshots; for example, `$0D4E` appears as `RTS` and as the start of
  `STA abs,X`. [Bolo](../../decoded/bolo/README.md) remains a work in progress.
  These small, repeated changes are a useful contrast with large overlays.
- [Snake Byte's README](../../decoded/snake-byte/README.md#regenerating-the-run-data)
  discusses its recorded generations; the
  [decompilation playbook](../decompiling/playbook.md#self-modification-is-rarer-than-the-warnings-suggest)
  explains why its warnings mostly describe startup relocation or false
  positives, not steady-state opcode replacement.
- EOS appears to load large code and data overlays from disk, potentially
  replacing intro code with gameplay code. This is a working hypothesis to
  verify, not an assumed property of every transition. Its README records a
  changed opcode at `$A806` and known interpreter/generated video differences.

## Question 1: detecting and selecting changed code

The desired event is **execution reaching code whose current visible
instruction version differs from the static version about to execute**. Stores
can prepare that change over many writes; a store itself need not switch the
active code. A transition must nevertheless occur before the first affected
instruction executes, including when a write changes an instruction later in
the *currently executing* basic block.

The existing collection bitsets are a starting point for investigating
written-then-executed code, but they are not currently present as a complete
runtime invalidation mechanism. Questions to resolve:

1. What runtime metadata identifies potentially affected instruction starts or
   blocks, including overlapping/misaligned instruction streams? A write to a
   byte can be an operand in one stream and an opcode in another.
2. How can writes mark a region dirty cheaply, without immediately switching on
   every store? Consider direct, indexed, indirect, read-modify-write, and
   other paths that actually change executable memory. Ensure host or probe
   memory modifications cannot silently bypass any required tracking.
3. Where are safe revalidation and dispatch boundaries? Existing block entries
   alone are insufficient if a store within a block can modify a later
   instruction in that same block. Possible approaches include splitting at
   relevant writes or adding a targeted exit/check; their costs and correctness
   need investigation.
4. How is the correct version selected after writes, partial writes, or
   restoration of an earlier version? The order in a recording is evidence,
   not a predetermined runtime sequence. Two independently modified regions
   may have combinations that never appeared together in one recording.
5. What happens when visible code matches no collected version, or control
   reaches an unrecorded instruction or target? Choose an explicit diagnostic
   or another deliberately designed fallback; do not silently select a
   plausible but incorrect version. Supporting completely unseen code would
   require additional discovery or translation beyond replaying known versions.

An observational check must not accidentally perform an emulated I/O access:
ordinary mapped `peek()` can change I/O state. On the other hand, the existing
a2io inspect function is probably overkill - there are very few accesses to
the soft-switch memory region which matter, mainly the language card range
$C080-$C08F which maps RAM or ROM into high memory and switches between
RAM banks at the low end of the upper range. The collector's inspection
path and the host's memory mapping should be considered when designing such
checks. A blanket runtime opcode fetch at every instruction is contrary to the
optimization goal even if it is functionally tempting.

### First iteration: tracking and transitions

Start with a few directed, low-memory tests and generation-indexed tracking.
An append-only list of bitfields is a reasonable baseline to measure: index 0
represents the initial state, and each later index represents a subsequent
observed code state. This is a proposed representation, not a claim that the
collector's existing snapshot number already has that meaning. Its snapshots
already contain byte segments and can provide versions of overwritten code,
but are closed by a particular write-after-execution heuristic, can omit
preloaded instructions, and carry neither a complete memory image nor
per-generation control flow. First test how much can be reconstructed from
these segments before adding new recorded data.

Separate the questions of **observed facts** and **inherited state**. A set bit
can mean written, executed, or present, but not the byte value or why it is
present. Generation 0 must preserve the existing no-generation behavior;
later states may copy the previous state or use explicit sparse overrides.
For sparse lookup, distinguish an absent override (inherit) from an explicitly
cleared bit or replaced byte. Compare correctness, construction time, lookup
cost, and peak memory before choosing a more elaborate representation. No
per-generation bitfield alone captures opcode bytes, instruction sizes, or
per-version operand observations.

Start with recorded low-memory transitions and investigate whether a
statically identifiable instruction or short sequence can delimit each
transition. A recorded PC is evidence of a possible boundary, not by itself
proof that the same write occurred or that a whole overlay is ready. Test
varying input and partial or repeated writes. If static evidence is
insufficient, extend collection and run-data with the minimum facts needed;
do not use collection time or the order of recorded generations as a runtime
clock. Runtime selection must occur before affected code executes and reject
unknown or ambiguous states, including changes within the current block.

The first functional milestone may deliberately support a documented subset:
known low-memory versions and transitions in directed fixtures. It must
explicitly reject unsupported situations instead of executing stale code; it
is not yet a general solution for independently changing regions or the
language card. See the [implementation plan](2026-11-20-code-generation-switching-implementation.md)
for the proposed tests, staged checks, and gates.

## Question 2: representing and sharing versions

Here, **generation** is a useful conceptual name for a code version, not yet
a decision that the entire 64 KB address space has one globally synchronized
generation number. A solution must account for both whole overlays and small
local patches, as well as previously generated code becoming visible again.

Alternatives to evaluate:

1. **Whole-program versions.** Each version has an independent complete
   switch; switching replaces the active dispatcher. This is straightforward
   to picture for a large overlay but duplicates retained code, increases
   generated size, and forces equivalent hand refinements to be repeated.
2. **Shared stable code plus versioned regions.** Keep unaffected blocks in one
   representation and dispatch only changed regions to version-specific blocks.
   A shared function with its own switch is one possible shape. This avoids
   copying unchanged code, but needs a clear contract for PC, registers,
   cycles, yielding, calls/returns, and transitions between shared and
   version-specific code.
3. **Another representation derived from evidence.** The relevant unit might
   be an instruction, basic block, routine, region, or an overlay composed of
   shared blocks. Select granularity based on correctness and measured
   optimization/readability trade-offs, not just snapshot boundaries.

For each approach, work through small patches, large overlays, a return to a
previous version, and independent changes in separate regions. Investigate how
IR and CFG identities distinguish versions of the same PC, while allowing
unchanged blocks to be reused. Edges, instruction sizes, branch targets,
overlapping entries, and block cycle totals can all differ between versions.
An IR optimization or idiomatic replacement must not move effects across a
transition or assume a version remains active beyond its validity.

For the first functional iteration, use alternative 1 deliberately: one
function and complete PC switch per materialized generation, with
`run_emulated()` dispatching the active function. Retained blocks may be
duplicated initially; measuring and removing this duplication is a later
iteration, not a prerequisite for the first correct directed replay. The
functions must preserve the existing PC, register, stack, cycle, debug, probe,
and yield contracts. Whole-program snapshots cannot automatically represent
unseen combinations of independently changing regions; diagnose those rather
than treating a chronological index as a universal code identity.

The record format may need more than the current byte segments and first
instruction map: it must preserve enough information to generate the
observed executable versions and distinguish operand variation from opcode
variation. Determine what must be captured at execution time versus at a later
snapshot; do not assume the existing snapshots encode a complete history.
Compatibility with existing recordings, and when recollection is necessary,
remain open.

## Language-card integration

The same logical PC can be backed by ROM, one of two `$D000-$DFFF` RAM banks,
or shared `$E000-$FFFF` RAM. Reads and writes have different mappings:
enabled writes target RAM even when ROM is visible, and write-protected writes
must not be treated as successful modifications. Switching the read mapping can
expose different code without a store.

The host/I/O layer should remain responsible for mapping and side effects;
the decompiler and generation representation should work in terms of a
consistent code/memory identity rather than duplicating soft-switch logic.
One possible investigation is extending address-indexed bitsets with unique
indexes for the physical backing regions. This distinguishes writes to
different banks but, by itself, does not retain instruction bytes or decide
which version should run. Also consider instructions spanning region or
address-space boundaries and changes in visibility while a block is executing.

Open question: should mapping changes select a version, invalidate affected
regions for later selection, or be represented in some other way? Keep this
compatible with the low-memory solution rather than treating bank switching as
a special substitute for general opcode replacement.

## A Note About ROM space

There are a few ranges of memory which are a special case due to the specifics
of Apple 2 hardware.  

The region from $C000-$C0FF is the so-called soft switch range. This soft switch
range is for memory mapped IO and is simply the source or destination of load and
store addresess.  The PC will never execute from there, so there's no concept of 
opcodes or operands being in this range. This also implies there's no way for
opcodes or operands in the range to be set from run-data .json or to be changes
via stores during program execution.

The region from $C100-$CFFF is reserved for peripheral ROMs.  For example,
the disk2 ROM loaded into $C600.  Code can be loaded here using special paths
outside the emulated function of the CPU - in real life ROMs are a fixed 
part of the hardware.  To emulate this there are special cases in the code
to initialize their values at startup. But normal code execution (from PCs 
either inside or outside the ROM range itself) can never modifiy opcodes
or operands in this memory range.

## Investigation and validation

Keep this section and the implementation plan updated after each experiment:
record the fixture, command or measurement, outcome, limitations, and the
decision or new question it prompted. A result from breaking a task down is
also a learning, even before code is written.

1. Turn the directed low-memory example into a reproducible repository test.
   Verify the collector's snapshots as well as interpreter versus generated
   registers and memory; cover both initial and replacement opcodes and
   operands.
2. Characterize existing Robotron and Bolo recordings and determine which
   transitions actual replays exercise. Use EOS as a larger overlay case, but
   do not claim correctness merely because execution gets farther.
3. Prototype and compare transition detection and code representations using
   the directed example first. Include a store changing a later instruction in
   the same block, a return to an earlier version, independent regions,
   changed instruction lengths, overlapping entries, and operand-only updates.
4. Exercise different inputs and transition orderings, including transitions
   at different emulated times. Test unknown versions explicitly. A trace
   demonstrates observed behavior, not completeness over all possible input.
5. Add ROM/RAM, both bank selections, shared upper RAM, write protection,
   writes while ROM is visible, mapping-only changes, and address-boundary
   cases. Check state and I/O side effects against the interpreter.
6. Verify existing Simple C behavior and language-card regressions, and assess
   the path to version-aware IR/optimized output. Demonstrate that stable
   blocks remain optimizable and shared code can be replaced once. This need
   not mean implementing every IR backend in the first step.
7. Compare generated and interpreted state at meaningful execution points;
   include PC, registers, flags, stack, relevant memory and device state.
   Preserve block timing and yielding. The
   [cycle-count design](2026-08-12-accurate-cycles-design.md) explains why
   timing differences can change program state and why per-instruction runtime
   work has a cost. Frame hashes are useful additional evidence, not a
   substitute for state comparisons.
8. Measure code size, shared versus duplicated blocks, dispatch/check frequency,
   and runtime overhead on a small patch and a larger overlay. Record the
   limitations of the recordings and any remaining EOS differences.

## Open decisions

- What is the smallest reliable event and metadata needed to mark changed
  executable code without paying for a generation switch on every store?
- Which execution boundaries guarantee correctness for self-modifying blocks
  without reducing all generated code to single-instruction blocks?
- What granularity shares retained code while preserving separate static CFGs
  and optimization opportunities for changed code?
- How should versions be matched when regions change independently, old code
  returns, operands vary, or language-card mappings change?
- What recording coverage is required, how should old recordings be handled,
  and what diagnostic is produced when runtime execution exceeds that coverage?
- Which parts can be introduced first in Simple C, and what must the shared
  representation expose for subsequent IR and readable-C work?

## Planning notes

- **2026-11-20, Task 2:** The collector now holds three bitsets per indexed
  entry. Appending copies the latest entry, including explicitly cleared
  bits, and later edits are independent. Production still uses only entry 0;
  saved JSON generations are not equated with these entries. Starting a new
  collection resets the container to one empty entry. `BitSet` previously
  owned a raw array with unsafe implicit copying; its storage now has deep
  copy semantics. The directed recordings' JSON was identical before and
  after this refactor.
- **2026-11-20, Task 2 measurement:** In a one-off optimized C++ experiment
  using the 596 generation segment ranges from
  [Bolo's recording](../../decoded/bolo/bolo.json), an artificial model copied
  the preceding three bitsets for every entry, marked each segment range as
  written and executable, and marked each segment start as an instruction
  start. Construction including reading those ranges took 5.2 ms; 39,059,456
  `execFull` lookups took 14.3 ms; peak process RSS was 17,132 KiB on this
  machine. The bit storage alone is about 13.97 MiB. Segment starts are only
  proxies, not observed instruction starts, and this is not a measurement
  of real collector or decompiler runtime. The current collector appends no
  copies. Retain the simple representation for now; measure real
  generation-aware workloads before choosing sparse storage.
- **2026-11-20:** Selected a test-driven, low-memory first iteration with
  generation-indexed tracking and deliberately duplicated whole-generation
  switches. Looking at the current collector showed that retaining its
  bitfields per snapshot is not sufficient to recover preloaded code or
  guarantee instruction bytes as actually executed. Existing saved segments
  do already contain multiple byte versions; tests must establish when those,
  initial-state information and existing observations suffice, and where
  additional per-version or transition evidence is needed.
- **2026-11-20, Task 3:** Tests combining input binaries, `FirstInstructions`,
  and ordered generation segments establish the directed low-memory versions,
  the preloaded-to-longer-instruction replacement, and Robotron's `$4C6E`
  opcode/size change. The directed trace's partial segments preserve the
  unchanged `$0902` RTS only when overlaid on bytes already known from
  `FirstInstructions`; the preloaded trace needs its input binary to establish
  the initial `$0400` instruction. Operand-only changes at `$0380` and `$03C0`
  set `operandChanged` but produce no executable segments at those PCs. These
  examples need no new recording field, but do not prove complete memory
  states or general inheritance: absence from a segment remains unknown.
  Accordingly, production collection still uses tracking entry 0; a
  chronological partial segment is not assigned to a complete indexed state.
  Entry creation for selectable states is deferred until Task 4 defines and
  tests materialization semantics.
- **2026-11-20, Task 3 validation:** `RuntimeData::load` now rejects missing
  generation/segment fields, invalid addresses or bytes, empty or
  address-space-crossing segments, and duplicate/overlapping ranges within a
  generation. Overlap across generations remains valid and is required by
  existing recordings. No run-data fields were added or versioned.
- **2026-11-20, Task 4:** `apple2tc --generation-state=N --asm` is an opt-in
  state inspector. It preserves input-binary bytes, fills only unmapped bytes
  from `FirstInstructions`, then applies segments cumulatively through index N.
  Executable observation bits and the byte's latest source state are kept
  separately from the memory contents. Each invocation constructs a fresh CFG;
  it does not import the recording's aggregate branch-target list or seed every
  `FirstInstructions` PC as a block, which avoids stale starts when a longer
  instruction covers an old entry. State 0 retains mapped input bytes.
  Conflicting first observations on unmapped bytes are rejected with state and
  PC diagnostics. The mode currently requires `FirstInstructions` and supports
  low-memory ordinary-binary assembly output. First-instruction observations
  at `$C000+` are ignored so normal ROM mapping can handle reachable code; a
  saved segment in that range is rejected. ROM input, language-card mapping,
  `--code-at`, `--inline-str`, `--no-gen`, and runtime dispatch are not
  supported.
- **2026-11-20, Task 4 fixture and measurement:** In
  [`generation-state.s`](../../tests/generation-state.s), state 0 decodes
  `$0400` as two-byte `LDA #$11` followed by `RTS` at `$0402`; state 1 overlays
  a three-byte `LDA $0210` and `RTS` at `$0403`, and changes the `$0303` JMP
  target from `$0402` to `$0403`. `$0402` is an aggregated recorded target but
  is inside the new operand, so the state-1 CFG must not retain it. Both states
  have four blocks; only the `$0300` JSR block has an identical body across
  states. State 0 also seeds `$0900` code absent from the directed fixture's
  input binary using `FirstInstructions`. Thirty process runs averaged 1.67 ms
  and 1.63 ms for states 0 and 1; GNU `time` measured peak RSS of 4,572 KiB and
  4,584 KiB. These process measurements include startup and are only a
  small-fixture baseline.
  Materialization is a cumulative patch view, not a complete snapshot: absence
  from a segment still cannot establish that a byte was unchanged, and runtime
  transition selection remains unresolved.
- **2026-11-20, Task 5 transition evidence:** In
  [`generation-switch.s`](../../tests/generation-switch.s), the initial stores
  are `$0309 STA $0900 = $A9`, `$030E STA $0901 = $11`, and
  `$0313 STA $0902 = $60`; `$0316 JSR $0900` then enters `$0900`, executes the
  installed `LDA #$11`, and returns through `$0902`. Replacement stores are
  `$031E STA $0900 = $A2` and `$0323 STA $0901 = $22`; the next
  `$0328 JSR $0900` enters the replacement `LDX #$22`. Restoration stores
  `$0330 STA $0900 = $A9` and `$0335 STA $0901 = $11`; `$033A JSR $0900`
  enters the restored version. The collector saves before the two opcode
  stores, with saved PCs `$031E` and `$0330`, and records later call targets,
  but does not serialize individual write events or their values.
- **2026-11-20, Task 5 timing/addressing fixtures:** The delayed variant waits
  in `$0500` before replacing code, writes the opcode with `STA $08FF,X` at
  `$0323` (`X=$01`, so the destination is `$0900`), and writes the operand
  with `STA ($10),Y` at `$0332` (`$0010/$0011=$0900`, `Y=$01`, destination
  `$0901`). Calls at `$0316`, `$0336`, and `$0348` enter `$0900`; saved PCs
  `$0323` and `$033E` occur before the replacement and restoration opcode
  writes. This demonstrates that actual address calculation and current bytes,
  not elapsed time or the recorded generation index, identify the code used.
  The no-transition fixture executes the unchanged `$A9 $11 $60` routine
  twice, with no writes to it between executions.
- **2026-11-20, Task 5 same-block and write-path findings:** In
  [`generation-same-block.s`](../../tests/generation-same-block.s), the
  `$0302 STA $0305` changes the next instruction from `LDA #$11` to
  `LDX #$11`; both instructions remain in the `$0300-$030C` basic block.
  Therefore a check only at the next block entry would execute the wrong
  instruction. The initial emitter keeps the original block's cycle/probe
  boundary and adds a resume case before `$0305`; if a transition is detected,
  it re-credits the unexecuted suffix and resumes in the selected switch. The
  collector's effective-address helper resolves direct, indexed, and indirect
  writes using runtime registers/pointers, so a static guess about the stored
  value is unnecessary for these observations. It does not record every write
  event.
- **2026-11-20, Task 5 stack-write gap:** The stack fixture uses `PHA` to write
  `$A9 $11 $4C $80 $03` at `$013C`, then executes that routine. The bytes appear
  in `FirstInstructions` but not in `generations[].code`: the collector handles
  explicit stores and read-modify-write instructions through
  `instWritesMemNormal`, which intentionally excludes implicit stack writes.
  Generated Simple C routes CPU stack writes through `ram_poke_impl`, but that
  runtime write visibility cannot repair missing recorded candidate code.
  Until collection covers stack-written code, such code is outside the first
  supported subset and must fail closed if encountered. Generated
  initialization uses `memcpy` before execution; ordinary generated CPU
  stores, read-modify-write, indexed/indirect stores, and `push8` converge on
  `ram_poke_impl`. `get_ram()` is const; host/I/O writes outside the shared
  low-memory helper and upper-memory mapping changes remain unsupported.
- **2026-11-20, Task 5 provisional selection contract:** For low-memory Simple
  C, mark a candidate code signature dirty on any write to a byte used as a
  compile-time instruction or operand. Revalidate before executing each
  versioned instruction, not merely at a later basic-block entry; a changed
  instruction inside a block requires a boundary at its PC. Match the complete
  bytes needed to distinguish the generated bodies, including all relevant
  instruction bytes, using direct low-memory storage reads rather than
  side-effecting `peek()`. A store's value and timing are not transition
  evidence. Zero matches, including partial/unknown code, must error; multiple
  matches with different bodies must error; equivalent candidates can be
  coalesced. Task 6 now implements an on-demand low-memory selector and
  unknown-instruction failure. During a partial write, if the current
  instruction still matches the active state, the dirty flag remains set and
  selection is retried before later instructions. Runtime ambiguity and
  independent-region tests remain open.
- **2026-11-20:** `CYCLES` currently charges a whole block and dispatches
  debug/probe events at its entry. Splitting blocks or exiting mid-block for
  a transition needs explicit cycle and event tests, not just equivalent final
  RAM. Moving dispatch into helper functions also needs to preserve
  `branchTarget` and remaining-cycle state across calls.
- **2026-11-20, Task 6 initial implementation:** `apple2tc --generation-aware
  --run-data=recording.json input.b33` emits one static PC switch per distinct
  materialized low-memory code state. It requires `FirstInstructions` and at
  least one `generations[]` entry; the recording's partial segments are
  materialized cumulatively as in `--generation-state=N`. This is an opt-in
  Simple C mode, not a change to ordinary `--simple-c`. It rejects ROM,
  language-card, `--code-at`, `--inline-str`, and `--no-gen` combinations.
  A shared low-memory write helper sets a dirty flag. Generated checks compare
  candidate instruction bytes through direct `s_ram` reads, excluding operands
  known to be dynamic. The dirty fast path checks before each instruction but
  fetches no opcode when memory is clean. Unmatched whole-state signatures are
  tolerated only while the current instruction still matches the active
  static code; otherwise a unique candidate at that PC is required, and
  unknown/ambiguous code fails before execution.
- **2026-11-20, Task 6 block/cycle handling:** To support a change within an
  original basic block without repeating its `CYCLES` debug/probe dispatch,
  otherwise non-entry instructions can be emitted as resume switch cases. The
  original block's cycles are charged once; a transition returns after
  restoring the not-yet-executed cycle suffix, and the selected state's resume
  case charges its own suffix without a second block-entry event. The directed
  same-block fixture passes, but cycle totals and exact debug/probe event
  equivalence still need dedicated assertions.
- **2026-11-20, Task 6 replay validation:** `bash
  tests/generation-switch-test.sh build` compiles and runs generation-aware
  Simple C for installed/replaced/restored, delayed, preloaded
  length-changing, and same-block fixtures. Their result RAM matches `a2run`;
  the generated path also dynamically reads a modified immediate operand and
  rejects an injected unknown opcode at `$0900`. The default Simple C
  reproduction still yields `$0201=$00` instead of `$22`, confirming the
  original mismatch remains isolated to the opt-in implementation. These tests
  do not yet compare all registers, cycles, probes, repeated calls, or
  ambiguous independent code regions.

[blog-dynamic]: https://tmikov.blogspot.com/2021/12/apple2tc-apple-ii-binary-to-c_24.html
[blog-ir]: https://tmikov.blogspot.com/2022/01/apple2tc-apple-ii-binary-to-c_23.html
[blog-opt]: https://tmikov.blogspot.com/2022/01/apple2tc-apple-ii-binary-to-c_62.html
