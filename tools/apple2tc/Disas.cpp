/*
 * Copyright (c) Tzvetan Mikov.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Disas.h"

#include "apple2tc/a2io.h"
#include "apple2tc/apple2iodefs.h"
#include "apple2tc/apple2plus_rom.h"
#include "apple2tc/support.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

void AsmBlock::splitInto(AsmBlock *nextBlock) {
  assert(nextBlock != this);
  assert(addrInside(nextBlock->addr()) && "Block to split into must overlap this one");
  assert(nextBlock->addr() != addr() && "Block to split into must have a greater start addr");
  assert(size() && "This block must have non-zero size (otherwise it wouldn't overlap anything)");
  assert(!nextBlock->size() && "Block to split into must not have a size");

  nextBlock->size_ = size_ + addr_ - nextBlock->addr_;
  size_ = nextBlock->addr_ - addr_;

  nextBlock->successors_.moveFrom(std::move(successors_));

  // Set our only successor as the next block.
  successors_.push_back(nextBlock);
}

unsigned AsmBlock::baseCycles(const Disas *disas) const {
  unsigned total = 0;
  for (const auto &addrInst : instructions(disas)) {
    // A block may end in an invalid-instruction terminator (see the class
    // comment above): the disassembler cut it off there because the bytes
    // don't decode, and the code generated for it is an abort stub, not a
    // real 6502 instruction. cpuInstCycles() asserts against exactly this
    // case -- "there is no meaningful cost for an instruction that was never
    // really executed" -- so it contributes nothing to the block's cost.
    if (addrInst.second.kind == CPUInstKind::INVALID)
      continue;
    total += cpuInstCycles(addrInst.second.kind, addrInst.second.addrMode);
  }
  return total;
}

AsmBlock::InstIterator::InstIterator(const Disas *disas, uint32_t addr)
    : EndIterator(addr), disas_(disas) {
  inst_ = std::make_pair((uint16_t)addr, disas->decodeCode((uint16_t)addr));
}

AsmBlock::InstIterator &AsmBlock::InstIterator::operator++() {
  addr_ += inst_.second.size;
  inst_ = std::make_pair((uint16_t)addr_, disas_->decodeCode((uint16_t)addr_));
  return *this;
}

CPUInst Disas::decodeCode(uint16_t addr) const {
  auto it = observedCode_.find(addr);
  if (it != observedCode_.end())
    return decodeInst(addr, it->second);
  return decodeInst(addr, peek3(addr));
}

Range intersect(const Range &a, const Range &b) {
  return {std::max(a.from, b.from), std::min(a.to, b.to)};
}

Disas::Disas(std::string runDataPath) : runDataPath_(std::move(runDataPath)) {
  memset(memory_, 0xFF, sizeof(memory_));
}

Disas::~Disas() = default;

void Disas::loadBinary(uint16_t addr, const uint8_t *data, size_t len) {
  if (len == 0)
    return;
  if (addr + len > 0x10000)
    throw std::range_error("binary is too large");
  if (languageCard_ && addr + len > 0xC000)
    throw std::runtime_error("--language-card requires the input binary to be in main RAM");
  addMemRange(MemRange(addr, (uint16_t)(addr + len - 1), true));
  memcpy(memory_ + addr, data, len);

  printf("// Loaded binary at [$%04X..$%04X]\n", addr, (uint16_t)(addr + len - 1));
}

void Disas::loadROM(const uint8_t *data, size_t len) {
  if (len == 0)
    return;
  if (len > 0x10000)
    throw std::range_error("ROM is too large");
  const uint16_t start = 0x10000 - len;
  addMemRange(MemRange(start, 0xFFFF, false));
  memcpy(memory_ + start, data, len);
  fprintf(stderr, "Auto-loaded Apple II ROM\n");
}

void Disas::loadDisk2ROM() {
    const auto it = findMemRange(0xC600);
    if (it == memRanges_.cend()) {
      addMemRange(MemRange(0xC600, 0xC6FF, false));
      for (unsigned i = 0; i != 0x100; ++i)
        memory_[0xC600 + i] = a2_disk2_rom_peek(i);
      fprintf(stderr, "Auto-loaded Disk II ROM\n");
    }
}

void Disas::addMemRange(MemRange range) {
  if (range.from > range.to)
    return;
  // Point `it` to the first range with starting address larger than this range's ending address.
  auto it = std::upper_bound(
      memRanges_.begin(), memRanges_.end(), range.to, [](uint16_t to, const MemRange &b) {
        return to < b.from;
      });

  // There is either no next range, or it doesn't overlap us. The only possible overlap is with
  // the previous range.
  if (it != memRanges_.begin()) {
    auto r = intersect(range, *(it - 1));
    if (!r.empty())
      throw std::logic_error("MemRange overlap");
  }

  memRanges_.insert(it, range);
}

std::vector<MemRange>::const_iterator Disas::findMemRange(uint16_t addr) const {
  auto it = std::upper_bound(
      memRanges_.begin(), memRanges_.end(), addr, [](uint16_t addr, const MemRange &b) {
        return addr < b.from;
      });
  // Check if the target overlaps the previous range.
  if (it != memRanges_.begin()) {
    --it;
    if (it->inside(addr))
      return it;
  }
  return memRanges_.end();
}

AsmBlock *Disas::addWork(uint16_t addr, AsmBlock **curBlock) {
  auto res = asmBlocks_.try_emplace(addr, addr);
  AsmBlock *block = &res.first->second;
  // Already exists? We are done.
  if (!res.second)
    return block;

  // Is this a ROM call, check if we need to install Apple II ROM
  if (addr > A2_IO_RANGE_END && (memRanges_.empty() || memRanges_.back().to < addr)) {
    loadROM(apple2plus_rom, apple2plus_rom_len);
  }

  // Disk II boot ROM sits in the $C600-$C6FF soft-switch window. The live
  // emulator resolves it via the IO layer, but the disassembler needs the bytes
  // present in its own `memory_` image before `--code-at` can validate the target.
  if (addr >= 0xC600 && addr <= 0xC6FF) {
    loadDisk2ROM();
  }

  // Iterate while the previous block overlaps addr.
  for (auto it = res.first;;) {
    if (it == asmBlocks_.begin())
      break;
    --it;
    if (!it->second.addrInside(addr))
      break;

    AsmBlock *prevBlock = &it->second;
    uint32_t iaddr = prevBlock->addr();
    assert(iaddr < addr && "A previous block must start at smaller address");
    do
      iaddr += decodeCode((uint16_t)iaddr).size;
    while (iaddr < addr);

    if (iaddr == addr) {
      // addr matches an instruction start in prevBlock. So, we must split
      // prevBlock in two at that point.
      prevBlock->splitInto(block);
      if (curBlock && *curBlock == prevBlock)
        *curBlock = block;
      return block;
    }

    // addr does not correspond to an instruction start in prevBlock. Nothing
    // more we can do. Later when we finish `block`, we can try to see if they
    // get back into alignment later (they often do after one instruction).
  }

  // We created a new block and it didn't split an existing one, so enqueue it
  // for processing.
  work_.push_back(block);
  return block;
}

void Disas::identifyAsmBlocks() {
  while (!work_.empty()) {
    AsmBlock *block = work_.front();
    work_.pop_front();

    uint32_t addr = block->addr();
    for (;;) {
      if (addr < 0x10000 && recordedOnly_[block->addr()] != recordedOnly_[addr]) {
        block->setEndAddress(addr);
        AsmBlock *next = addWork((uint16_t)addr, nullptr);
        block->finish(next, nullptr);
        break;
      }
      if (addr < 0x10000 && !canDecodeCode((uint16_t)addr)) {
        // Keep a fall-through target for the CFG, but do not generate code for
        // an upper-memory address absent from the recording.
        if (addr == block->addr()) {
          block->setEndAddress(addr + 1);
          block->finish(nullptr, nullptr, true);
        } else {
          block->setEndAddress(addr);
          block->finish(addWork((uint16_t)addr, nullptr), nullptr);
        }
        break;
      }
      if (findMemRange(addr) == memRanges_.end()) {
        fprintf(stderr, "warning: accessing undefined memory at $%04X\n", addr);
      }
      // If we already started a basic block and encounter another one, we must
      // terminate the current one and "fall" into the found one.
      if (addr != block->addr()) {
        if (auto *next = findAsmBlockAt(addr)) {
          block->setEndAddress(addr);
          block->finish(next, nullptr, next->invalid());
          break;
        }
      }

      CPUInst inst = decodeCode((uint16_t)addr);
      if (addr + inst.size <= 0x10000 &&
          recordedOnly_[addr] != recordedOnly_[addr + inst.size - 1])
        throw std::runtime_error(format(
            "--language-card does not support an instruction crossing $D000 at $%04X", addr));
      const bool recordedOnly = recordedOnly_[addr];
      addr += inst.size;
      if (inst.isInvalid()) {
        fprintf(stderr, "warning: invalid instruction at $%04X\n", addr - 1);
        block->setEndAddress(addr);
        block->finish(nullptr, nullptr, true);
        break;
      }

      if (inst.kind == CPUInstKind::JMP) {
        block->setEndAddress(addr);
        AsmBlock *branch =
            inst.addrMode == CPUAddrMode::Abs ? addWork(inst.operand, &block) : nullptr;
        block->finish(nullptr, branch);
        break;
      } else if (inst.kind == CPUInstKind::JSR) {
        block->setEndAddress(addr);
        // A routine declared with --inline-str consumes the NUL-terminated
        // bytes that follow the call and returns past them, so control resumes
        // after the terminator rather than at `addr`. Tracing `addr` instead is
        // what decodes the first byte of a string as an instruction.
        AsmBlock *fall = addWork(
            inlineStr_.count(inst.operand) ? inlineStrContinuation(addr) : addr, nullptr);
        AsmBlock *branch = addWork(inst.operand, &block);
        block->finish(fall, branch);
        break;
      } else if (inst.addrMode == CPUAddrMode::Rel) {
        block->setEndAddress(addr);
        AsmBlock *fall = addWork(addr, nullptr);
        AsmBlock *branch = addWork(inst.operand, &block);
        block->finish(fall, branch);
        break;
      } else if (inst.kind == CPUInstKind::BRK) {
        block->setEndAddress(addr);
        // RTI resumes two bytes after BRK, not at the following opcode byte.
        addWork((uint16_t)(addr - inst.size + 2), nullptr);
        if (memRanges_.empty() || memRanges_.back().to < A2_IRQ_VEC + 1) {
          loadROM(apple2plus_rom, apple2plus_rom_len);
          fprintf(stderr, "Auto-loaded Apple II ROM\n");
        }
        AsmBlock *branch = recordedOnly ? nullptr : addWork(peek16(A2_IRQ_VEC), &block);
        block->finish(nullptr, branch, true);
        break;
      } else if (inst.kind == CPUInstKind::RTS || inst.kind == CPUInstKind::RTI) {
        block->setEndAddress(addr);
        block->finish(nullptr, nullptr);
        break;
      }

      // If we wrapped around.
      if (addr >= 0x10000) {
        block->setEndAddress(addr);
        AsmBlock *next = addWork((uint16_t)addr, nullptr);
        block->finish(next, nullptr);
        break;
      }
    }
  }
}

uint16_t Disas::inlineStrContinuation(uint16_t strAddr) const {
  // The string cannot run past the end of memory, and a routine declared by
  // mistake would otherwise walk the whole address space looking for a byte
  // that is not there. Stopping at the wrap point turns that into a decode
  // error at a nameable address instead of a silent scan.
  for (uint32_t a = strAddr; a < 0x10000; ++a) {
    if (peek((uint16_t)a) == 0)
      return (uint16_t)(a + 1);
  }
  throw std::runtime_error(format(
      "--inline-str: the string at $%04X has no $00 terminator before the end of memory",
      (unsigned)strAddr));
}

void Disas::applyCodeAt() {
  if (codeAt_.empty())
    return;

  // `--code-at` is meaningful even without a recording, since its edges are
  // asserted rather than observed. Synthesize an empty RuntimeData so they
  // reach the disassembler through the ordinary path.
  if (!runData_)
    runData_ = std::make_unique<RuntimeData>();

  for (const auto &edge : codeAt_) {
    // Seeds disassembly of the target.
    runData_->branchTargets.push_back(edge.target);
    // Makes the target a successor of the branch at the origin. That is what
    // turns it into a "dynamic block", which is what gets an entry in the
    // generated address-to-block map -- without it the target would be
    // decompiled but unreachable.
    auto &targets = runData_->allBranches[edge.origin];
    if (std::find(targets.begin(), targets.end(), edge.target) == targets.end()) {
      targets.push_back(edge.target);
      // RuntimeData::load() sorts each target list; keep that invariant.
      std::sort(targets.begin(), targets.end());
    }
  }

  // branchTargets is documented as sorted by address. Note this only runs when
  // there is at least one edge, so a plain --run-data build is untouched.
  auto &bt = runData_->branchTargets;
  std::sort(bt.begin(), bt.end());
  bt.erase(std::unique(bt.begin(), bt.end()), bt.end());

  printf("// --code-at: %zu asserted edges applied\n", codeAt_.size());
}

void Disas::validateCodeAt() const {
  for (const auto &edge : codeAt_) {
    const AsmBlock *targetBlock = cfindAsmBlockAt(edge.target);
    if (!targetBlock)
      throw std::runtime_error(format(
          "--code-at: target $%04X is not the start of a basic block", (unsigned)edge.target));

    // Whether untraced bytes are really code is precisely what --code-at
    // asserts, so a target pointing at data cannot be diagnosed in general --
    // the disassembler will happily decode anything. The one cheap signal is
    // the target failing to decode at all, which catches the common typo.
    //
    // Deliberately the *first* instruction and not AsmBlock::invalid(), which
    // means "ends in an invalid instruction". Blocks legitimately end that way
    // when real code runs into undecodable bytes -- past a BRK, or at the end
    // of the binary -- and a warning that fires on good input gets ignored.
    if (recordedOnly_[edge.target]) {
      if (!canDecodeCode(edge.target))
        throw std::runtime_error(format(
            "--code-at: target $%04X was not recorded", (unsigned)edge.target));
    } else {
      for (auto [addr, inst] : targetBlock->instructions(this)) {
        if (inst.kind == CPUInstKind::INVALID)
          fprintf(
              stderr,
              "warning: --code-at: target $%04X does not decode to a valid instruction, "
              "so it is probably not code\n",
              (unsigned)edge.target);
        break; // Only the first instruction is of interest.
      }
    }

    if (recordedOnly_[edge.origin]) {
      auto it = observedCode_.find(edge.origin);
      if (it != observedCode_.end()) {
        CPUOpcode inst = decodeOpcode(it->second.d[0]);
        if (instIsBranch(inst.kind, inst.addrMode) && inst.kind != CPUInstKind::BRK)
          continue;
      }
      throw std::runtime_error(format(
          "--code-at: origin $%04X has no recorded branch opcode", (unsigned)edge.origin));
    }

    std::optional<CPUInst> originInst{};
    if (const AsmBlock *block = cfindAsmBlockContaining(edge.origin)) {
      for (auto [addr, inst] : block->instructions(this)) {
        if (addr >= edge.origin) {
          if (addr == edge.origin)
            originInst = inst;
          break;
        }
      }
    }

    if (!originInst)
      throw std::runtime_error(format(
          "--code-at: origin $%04X is not the start of a disassembled instruction",
          (unsigned)edge.origin));

    // These are exactly the instructions GenIR consults branchTargetsFrom() for.
    // A JMP or JSR with a plain literal operand is accepted here but cannot
    // actually take an extra target; GenIR warns about that separately.
    if (!instIsBranch(originInst->kind, originInst->addrMode) ||
        originInst->kind == CPUInstKind::BRK)
      throw std::runtime_error(format(
          "--code-at: the instruction at origin $%04X is not a branch. Only RTS, RTI, JMP, JSR "
          "and conditional branches can be given extra targets",
          (unsigned)edge.origin));
  }
}

void Disas::run(bool noGenerations) {
  // Load the start address from runtime data, if available.
  if (!runDataPath_.empty())
    runData_ = RuntimeData::load(runDataPath_);

  const bool hasRecording = runData_ != nullptr;
  if (languageCard_ && !runData_) {
    runData_ = std::make_unique<RuntimeData>();
    runData_->languageCardStart.emplace();
    runData_->languageCardStart->powerOn = true;
  }
  if (languageCard_ &&
      (!runData_->languageCardStart || (hasRecording && !runData_->firstInstructions)))
    throw std::runtime_error(
        "--language-card requires FirstInstructions and LanguageCardStart");
  if (languageCard_) {
    if (hasRecording)
      std::fill(recordedOnly_.begin() + 0xD000, recordedOnly_.end(), true);
  }
  if (useFirstObserved_ && runData_ && runData_->firstInstructions)
    for (const auto &[addr, instruction] : *runData_->firstInstructions)
      observedCode_.emplace(addr, instruction.bytes);
  if (languageCard_ && !runData_->languageCardStart->powerOn) {
    const auto &initialRAM = runData_->languageCardStart->mainRAM;
    for (const auto &range : memRanges_)
      if (range.writable &&
          !std::equal(memory_ + range.from, memory_ + range.to + 1,
                      initialRAM.begin() + range.from))
        throw std::runtime_error(
            "--language-card input binary differs from recorded starting RAM; "
            "record with the same binary preloaded");
  }
  if (languageCard_) {
    const auto &start = *runData_->languageCardStart;
    if (memRanges_.empty() || memRanges_.back().writable || memRanges_.back().to != 0xFFFF) {
      if (start.powerOn)
        loadROM(apple2plus_rom, apple2plus_rom_len);
      else
        loadROM(start.rom.data(), start.rom.size());
    } else if (!start.powerOn &&
               (memRanges_.back().from != 0xD000 ||
                !std::equal(memory_ + 0xD000, memory_ + 0x10000, start.rom.begin()))) {
      throw std::runtime_error("--language-card ROM differs from the recorded starting ROM");
    }
  }

  // Must happen before the branch targets below are turned into work. It may
  // synthesize an empty RuntimeData, which supplies no start regs, so the block
  // just below is unaffected.
  applyCodeAt();

  if (runData_) {
    if (auto *regs = runData_->getStartRegs())
      start_ = regs->pc;
  }

  if (!start_.has_value())
    throw std::logic_error("starting address not set");
  addWork(*start_, nullptr)->setName("START");

  // Just naively load all runtime data generations.
  if (!noGenerations && runData_) {
    unsigned genIndex = 0;
    for (const auto &gen : runData_->generations) {
      // printf("// Generation %u\n", genIndex++);
      for (const auto &seg : gen.code) {
        if (languageCard_) {
          if (seg.bytes.size() > 0x10000u - seg.addr)
            throw std::runtime_error("Recorded code segment exceeds address space");
          size_t i = 0;
          while (i < seg.bytes.size()) {
            uint16_t addr = (uint16_t)(seg.addr + i);
            if (findMemRange(addr) != memRanges_.end()) {
              ++i;
              continue;
            }
            size_t begin = i++;
            while (i < seg.bytes.size() &&
                   findMemRange((uint16_t)(seg.addr + i)) == memRanges_.end())
              ++i;
            uint16_t from = (uint16_t)(seg.addr + begin);
            uint16_t to = (uint16_t)(seg.addr + i - 1);
            addMemRange(MemRange(from, to, true));
            memcpy(memory_ + from, seg.bytes.data() + begin, i - begin);
            printf("// Loaded segment [$%04X..$%04X]\n", from, to);
          }
          continue;
        }
        uint16_t from = seg.addr;
        uint16_t to = seg.addr + seg.bytes.size() - 1;
        try {
          addMemRange(MemRange(from, to, true));
        } catch (std::logic_error &e) {
          // printf( "// SKIPPED segment [$%04X..$%04X]\n", from, to);
          continue;
        }

        printf("// Loaded segment [$%04X..$%04X]\n", from, to);
        memcpy(memory_ + seg.addr, seg.bytes.data(), seg.bytes.size());
      }
    }
  }

  identifyAsmBlocks();

  if (runData_) {
    unsigned newBlocks = 0;
    for (auto addr : runData_->branchTargets) {
      AsmBlock *block = addWork(addr, nullptr);
      // Count how many unfinished blocks are created.
      newBlocks += !block->size();
    }
    if (newBlocks)
      printf("// %u new runtime blocks added\n", newBlocks);
    // Identify additional asm blocks.
    identifyAsmBlocks();
  }

  if (!observedCode_.empty()) {
    for (const auto &branch : runData_->allBranches)
      for (uint16_t target : branch.second)
        if (recordedOnly_[target] || observedCode_.count(target))
          addWork(target, nullptr);

    // Overlapping instruction streams can have multiple fall-through edges
    // into the same address, which must remain a block entry.
    std::map<uint16_t, unsigned> predecessors;
    for (const auto &entry : observedCode_) {
      uint16_t pc = entry.first;
      CPUInst inst = decodeCode(pc);
      uint32_t next = (uint32_t)pc + inst.size;
      if (!inst.isInvalid() && !instIsBranch(inst.kind, inst.addrMode) && next < 0x10000 &&
          observedCode_.count((uint16_t)next))
        ++predecessors[(uint16_t)next];
    }
    for (const auto &[pc, count] : predecessors)
      if (count > 1)
        addWork(pc, nullptr);
    identifyAsmBlocks();

    // Seed any recorded instruction not reached from the existing CFG,
    // without splitting blocks at every sequential instruction.
    std::vector<bool> decodedStarts(0x10000, false);
    for (const auto &[addr, block] : asmBlocks_)
      if (observedCode_.count(addr))
        for (const auto &entry : block.instructions(this))
          decodedStarts[entry.first] = true;

    for (const auto &entry : observedCode_) {
      uint16_t pc = entry.first;
      if (decodedStarts[pc])
        continue;
      AsmBlock *block = addWork(pc, nullptr);
      identifyAsmBlocks();
      for (const auto &inst : block->instructions(this))
        decodedStarts[inst.first] = true;
    }
  }

  validateCodeAt();

  // TODO: identify and coalesce misaligned blocks which come back in sync after
  //       one instruction.

  const auto covered = codeCoverage();

  // Collect all data labels.
  for (const auto &block : asmBlocks_) {
    if (!canDecodeCode(block.first))
      continue;
    for (auto [addr, inst] : block.second.instructions(this)) {
      if (instAccessesData(inst.addrMode)) {
        // Can we reason about the operand at all?
        if (operandIsEA(inst.addrMode)) {
          // Check for self-modifying code.
          if (instWritesMemNormal(inst.kind, inst.addrMode)) {
            if (covered[inst.operand]) {
              selfModifers_.insert(addr);

              auto smKind = operandIsIndexed(inst.addrMode) ? SelfModifiedKind::Indexed
                                                            : SelfModifiedKind::Certain;
              auto res = selfModified_.try_emplace(inst.operand, smKind);
              if (!res.second && smKind == SelfModifiedKind::Certain)
                res.first->second = SelfModifiedKind::Certain;
            }
          }
        }

        if (!(inst.operand >= A2_IO_RANGE_START && inst.operand <= A2_IO_RANGE_END) &&
            !asmBlocks_.count(inst.operand)) {
          dataLabels_[inst.operand].comingFrom.insert(addr);
        }
      }
    }
  }

  fflush(stderr);
  printf("// code labels: %zu\n", asmBlocks_.size());
  printf("// data labels: %zu\n", dataLabels_.size());
}

std::vector<bool> Disas::codeCoverage() const {
  std::vector<bool> covered(0x10000, false);
  for (const auto &[addr, block] : asmBlocks_) {
    if (!canDecodeCode(addr))
      continue;
    for (uint32_t a = block.addr(); a < block.endAddr(); ++a)
      covered[(uint16_t)a] = true;
  }
  return covered;
}

void Disas::printCoverage(FILE *f, const std::vector<KnownDataRange> &knownData) {
  // What the disassembler reached, not what survives into the generated C:
  // this runs before IR generation, so passes that delete blocks later (see
  // --extern-routines) are not reflected. It answers "how much of this binary
  // has been classified as code", which is the number that bounds the work.
  const auto covered = codeCoverage();

  std::vector<bool> known(0x10000, false);
  for (const auto &kd : knownData)
    for (uint32_t a = kd.from; a <= kd.to; ++a)
      known[a] = true;

  auto nonzeroIn = [this](uint32_t from, uint32_t to) {
    uint32_t n = 0;
    for (uint32_t a = from; a <= to; ++a)
      if (memory_[a])
        ++n;
    return n;
  };

  if (!knownData.empty()) {
    fprintf(f, "declared data:\n");
    for (const auto &kd : knownData) {
      uint32_t len = (uint32_t)kd.to - kd.from + 1;
      uint32_t overlap = 0;
      for (uint32_t a = kd.from; a <= kd.to; ++a)
        if (covered[a])
          ++overlap;
      fprintf(f, "  $%04X-$%04X  %5u bytes  %s", kd.from, kd.to, len, kd.name.c_str());
      // Worth shouting about: the disassembler reached bytes declared as data,
      // so one of the two is wrong.
      if (overlap)
        fprintf(f, "  *** %u bytes also decompiled as code", overlap);
      fprintf(f, "\n");
    }
    fprintf(f, "\n");
  }

  for (const auto &range : memRanges_) {
    uint32_t total = range.byte_length();
    uint32_t code = 0, dataBytes = 0, unknownZero = 0, unknownNonzero = 0;
    for (uint32_t a = range.from; a <= range.to; ++a) {
      if (covered[a])
        ++code;
      else if (known[a])
        ++dataBytes;
      else if (memory_[a])
        ++unknownNonzero;
      else
        ++unknownZero;
    }

    fprintf(f, "range $%04X-$%04X  %u bytes\n", range.from, range.to, total);
    auto pct = [total](uint32_t n) { return total ? 100.0 * n / total : 0.0; };
    fprintf(f, "  code            %6u  %5.1f%%\n", code, pct(code));
    fprintf(f, "  declared data   %6u  %5.1f%%\n", dataBytes, pct(dataBytes));
    fprintf(f, "  unknown zero    %6u  %5.1f%%\n", unknownZero, pct(unknownZero));
    fprintf(
        f,
        "  unknown nonzero %6u  %5.1f%%   <- what is left to identify\n",
        unknownNonzero,
        pct(unknownNonzero));

    // Runs of unaccounted-for bytes, most nonzero first. Runs that are entirely
    // zero are buffers; listing them as work badly overstates what remains.
    struct Gap {
      uint32_t from, to, nonzero;
    };
    std::vector<Gap> gaps{};
    for (uint32_t a = range.from; a <= range.to;) {
      if (covered[a] || known[a]) {
        ++a;
        continue;
      }
      uint32_t start = a;
      while (a <= range.to && !covered[a] && !known[a])
        ++a;
      gaps.push_back({start, a - 1, nonzeroIn(start, a - 1)});
    }

    std::sort(gaps.begin(), gaps.end(), [](const Gap &a, const Gap &b) {
      return a.nonzero > b.nonzero;
    });
    unsigned shown = 0;
    for (const Gap &g : gaps) {
      if (!g.nonzero)
        continue;
      fprintf(
          f,
          "    $%04X-$%04X  %5u bytes, %u nonzero\n",
          g.from,
          g.to,
          g.to - g.from + 1,
          g.nonzero);
      ++shown;
    }
    if (!shown)
      fprintf(f, "    (no unidentified nonzero bytes)\n");
  }
}

void Disas::printAsmListing() {
  // Name all labels by address.
  unsigned labelNumber = 0;
  for (auto &block : asmBlocks_) {
    if (block.second.name()[0])
      continue;

    char buf[AsmBlock::kNameSize];
    if (labelsByAddr_)
      snprintf(buf, sizeof(buf), "L_%04X", block.first);
    else if (!block.second.implicit())
      snprintf(buf, sizeof(buf), "L_%04u", ++labelNumber);
    block.second.setName(buf);
  }
  labelNumber = 0;
  for (auto &l : dataLabels_) {
    if (l.second.name[0])
      continue;
    if (labelsByAddr_) {
      if (l.first < 0x100)
        snprintf(l.second.name, sizeof(l.second.name), "M_%02X", l.first);
      else
        snprintf(l.second.name, sizeof(l.second.name), "M_%04X", l.first);
    } else if (!l.second.implicit())
      snprintf(l.second.name, sizeof(l.second.name), "M_%04u", ++labelNumber);
  }

  for (auto &l : dataLabels_) {
    if (findMemRange(l.first) == memRanges_.end()) {
      printf("%s          EQU    $%04X", l.second.name, l.first);
      if (!l.second.comingFrom.empty()) {
        printf(" ; xref");
        unsigned xrc = 0;
        for (auto from : l.second.comingFrom) {
          if (++xrc == 9) {
            printf("...");
            break;
          }
          printf(" $%04X", from);
        }
      }
      putchar('\n');
    }
  }

  uint32_t nextData = 0;
  std::optional<uint32_t> lastAddr;
  for (const auto &[_, block] : asmBlocks_) {
    if (nextData < block.addr())
      printAsmDataRange(Range(nextData, block.addr() - 1));
    printAsmCodeRange(block, lastAddr);
    nextData = block.endAddr();
  }
  if (nextData != 0x10000)
    printAsmDataRange(Range(nextData, 0xFFFF));
}

void Disas::printAsmCodeRange(const AsmBlock &block, std::optional<uint32_t> &lastAddr) {
  if (!lastAddr || *lastAddr != block.addr()) {
    printf("; Code range [$%04X-$%04X]\n", block.addr(), block.endAddr() - 1);
    printf("/*%04X*/ ", block.addr());
    printf("            org    $%04X\n", block.addr());
  }
  lastAddr = block.endAddr();
  if (block.misaligned())
    printf("; WARNING: misaligned\n");

  if (block.name()[0]) {
    printf("/*%04X*/ ", block.addr());
    printf("%s:                             ; xref", block.name());
    for (const auto &node : block.userList()) {
      printf(" %s", node.owner()->name());
    }
    printf("\n");
  }

  for (auto const [addr, inst] : block.instructions(this)) {
    if (inst.isInvalid())
      printf("; ERROR: Invalid instruction\n");

    if (selfModifers_.count(addr))
      printf("; WARNING: Instruction performs code modification\n");
    for (unsigned i = 0, e = cpuInstSize(inst.addrMode); i != e; ++i) {
      auto it = selfModified_.find((uint16_t)(addr + i));
      if (it != selfModified_.end())
        printf(
            "; WARNING: Instruction byte %u %s modified\n",
            i,
            it->second == SelfModifiedKind::Certain ? "is" : "may be");
    }

    printf("/*%04X*/ ", addr);

    // Prepare the disassembled instruction string.
    FormattedInst fmt = formatInst(inst, peek3(addr), [this](CPUInst inst) -> std::string {
      if (inst.addrMode != CPUAddrMode::Imm) {
        if (AsmBlock *block = findAsmBlockAt(inst.operand))
          return block->name();
      }
      auto it = dataLabels_.find(inst.operand);
      if (it != dataLabels_.end())
        return it->second.name;
      return {};
    });

    printf("            %-3s", fmt.inst);
    if (!fmt.operand.empty()) {
      printf("    %-16s ; $%04X", fmt.operand.c_str(), inst.operand);
    }
    putchar('\n');
  }
}

void Disas::printAsmDataRange(Range r) {
  for (const auto &mr : memRanges_) {
    auto cross = intersect(mr, r);
    if (!cross.empty()) {
      printf("; Data range [$%04x-$%04X]\n", cross.from, cross.to);

      for (uint16_t addr = cross.from; (uint16_t)(addr - 1) != cross.to; ++addr) {
        printf("/*%04X*/ ", addr);

        auto label = dataLabels_.find(addr);
        printf(
            "%-11s DFB    $%02X", label != dataLabels_.end() ? label->second.name : "", peek(addr));
        if (label != dataLabels_.end() && !label->second.comingFrom.empty()) {
          printf(" ; xref");
          unsigned xrc = 0;
          for (auto from : label->second.comingFrom) {
            if (++xrc == 9) {
              printf("...");
              break;
            }
            printf(" $%04X", from);
          }
        }

        putchar('\n');
      }
    }
  }
}
