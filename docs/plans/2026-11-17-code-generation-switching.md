# Code generations in generated C

Status: living design document. This collects goals, evidence, and
open questions; it does not select an implementation or authorize changing
current behavior.

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

The current [collector](../../lib/cpuemu/DebugState6502.cpp) uses:

- `memWritten_` for addresses observed written; saved executable ranges are
  cleared from it, while other written addresses can remain marked.
- `memExecStart_` for instruction starts executed after their opcode address
  was written; `memExecFull_` marks the corresponding instruction bytes.
- A later write to a marked instruction start triggers `saveGeneration()`
  before the write. It saves the marked bytes from the then-visible memory
  view; collection also saves a final generation at the end.
- `firstInstructions_` separately records the first observed instruction bytes
  at each collected PC and whether its operand was observed to change. The
  bitsets do not store those values; `memExecStart_` and `memExecFull_` are
  reset during generation saves.

These are collection-time observations, not a runtime switch mechanism.
The existing trigger is specifically a write to an instruction **start** that
was previously written and then executed. It does not by itself prove that
every earlier opcode version, operand update, or physical bank is represented.
In particular, a snapshot taken later under a different mapping can differ
from the bytes seen when the instruction executed.

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

## Investigation and validation

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

[blog-dynamic]: https://tmikov.blogspot.com/2021/12/apple2tc-apple-ii-binary-to-c_24.html
[blog-ir]: https://tmikov.blogspot.com/2022/01/apple2tc-apple-ii-binary-to-c_23.html
[blog-opt]: https://tmikov.blogspot.com/2022/01/apple2tc-apple-ii-binary-to-c_62.html
