/*
 * Copyright (c) Tzvetan Mikov.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/// This module generates a straight-forward 1-to1 mapping from Asm to C, without
/// any optimizations.

#include "Disas.h"

#include "apple2tc/apple2iodefs.h"
#include "apple2tc/apple2plus_rom.h"
#include "apple2tc/support.h"

namespace {

using SimpleCCode = std::map<uint16_t, CPUInst>;

static bool sameCode(const SimpleCCode &a, const SimpleCCode &b) {
  if (a.size() != b.size())
    return false;
  auto ia = a.begin();
  auto ib = b.begin();
  for (; ia != a.end(); ++ia, ++ib) {
    if (ia->first != ib->first || ia->second.kind != ib->second.kind ||
        ia->second.addrMode != ib->second.addrMode || ia->second.size != ib->second.size)
      return false;
  }
  return true;
}

static void printGenerationCheck(FILE *f, uint16_t pc, unsigned cycleAdjustment) {
  fprintf(f, "      if (s_generation_dirty) {\n");
  fprintf(f, "        int selected = generation_match();\n");
  fprintf(f,
          "        bool completeMatch = selected >= 0;\n"
          "        if (selected == -2) { fprintf(stderr, \"Ambiguous generation state at "
          "$%%04X\\n\", s_pc); error_handler(s_pc); return; }\n"
          "        if (selected < 0) {\n"
          "          if (generation_instruction_matches(s_active_generation, s_pc)) {\n"
          "            selected = (int)s_active_generation;\n"
          "          } else {\n"
          "            selected = generation_match_at_pc(s_pc);\n"
          "            if (selected == -2) { fprintf(stderr, \"Ambiguous generation instruction "
          "at $%%04X\\n\", s_pc); error_handler(s_pc); return; }\n"
          "            if (selected < 0) { fprintf(stderr, \"Unknown generation instruction at "
          "$%%04X\\n\", s_pc); error_handler(s_pc); return; }\n"
          "          }\n"
          "        }\n"
          "        if (completeMatch) s_generation_dirty = false;\n");
  fprintf(f, "        if ((unsigned)selected != s_active_generation) {\n");
  if (cycleAdjustment) {
    fprintf(f, "          s_cycles -= %u;\n", cycleAdjustment);
    fprintf(f, "          s_remaining_cycles += %u;\n", cycleAdjustment);
  }
  fprintf(f, "          s_pc = 0x%04x;\n", pc);
  fprintf(f, "          s_active_generation = (unsigned)selected;\n");
  fprintf(f, "          *branchTargetRef = branchTarget;\n");
  fprintf(f, "          return;\n");
  fprintf(f, "        }\n");
  fprintf(f, "      }\n");
}

} // namespace

void Disas::printSimpleCPrologue(FILE *f, bool generationAware) {
  if (generationAware)
    fprintf(f, "#define APPLE2TC_GENERATION_AWARE 1\n");
  fprintf(f, "\n#include \"apple2tc/system-inc.h\"\n\n");

  printMemoryDeclarations(f);
  fprintf(f, "\nvoid init_emulated(void) {\n");
  printMemoryInitialization(f);

  // Generation 0 saves the registers on start. If it is present, use it.
  // FIXME: use startup regs.
  const Regs *regs = runData_ ? runData_->getStartRegs() : nullptr;
  if (regs) {
    fprintf(f, "  s_pc = 0x%04x;\n", regs->pc);
    fprintf(f, "  s_a = 0x%02x;\n", regs->a);
    fprintf(f, "  s_x = 0x%02x;\n", regs->x);
    fprintf(f, "  s_y = 0x%02x;\n", regs->y);
    fprintf(f, "  s_status = 0x%02x;\n", regs->status);
    fprintf(f, "  s_sp = 0x%02x;\n", regs->sp);
  } else {
    fprintf(f, "  s_pc = 0x%04x;\n", *start_);
  }
  if (generationAware)
    fprintf(f, "  s_active_generation = 0;\n  s_generation_dirty = false;\n");
  fprintf(f, "}\n");
}

void Disas::printMemoryDeclarations(FILE *f) {
  if (languageCard_) {
    if (runData_->languageCardStart->powerOn)
      fprintf(f, "static const uint8_t s_language_card_rom[0x3000];\n");
    else {
      fprintf(f, "static const uint8_t s_language_card_state[0x7000];\n");
      fprintf(f, "static const uint8_t s_initial_ram[0xc000];\n");
    }
  }
  for (auto const &mr : memRanges_) {
    if (languageCard_ && !runData_->languageCardStart->powerOn)
      continue;
    unsigned len = mr.to - mr.from + 1;
    fprintf(f, "static const uint8_t s_mem_%04x[0x%04x];\n", mr.from, len);
  }
}

void Disas::printMemoryInitialization(FILE *f) {
  if (languageCard_) {
    const auto &start = *runData_->languageCardStart;
    if (start.powerOn)
      fprintf(f, "  io_load_rom(s_language_card_rom, 0x3000);\n");
    else
      fprintf(f, "  io_restore_language_card(s_language_card_state, %u, %u, %u, 0x%04x);\n",
              (unsigned)start.readRom, start.bank, (unsigned)start.writeProtected, start.prevIOAddr);
    if (!start.powerOn)
      fprintf(f, "  memcpy(s_ram, s_initial_ram, 0xc000);\n");
  }
  for (auto const &mr : memRanges_) {
    unsigned len = mr.to - mr.from + 1;
    if (languageCard_ &&
        (!runData_->languageCardStart->powerOn || mr.from >= 0xD000))
      continue;
    fprintf(f, "  memcpy(s_ram + 0x%04x, s_mem_%04x, 0x%04x);\n", mr.from, mr.from, len);
  }
  if (!languageCard_)
    fprintf(f, "  io_load_rom(s_ram + 0xd000, 0x3000);\n");
}

void Disas::printSimpleCEpilogue(FILE *f) {
  printMemoryDefinitions(f);
}

void Disas::printMemoryDefinitions(FILE *f) {
  if (languageCard_ && runData_->languageCardStart->powerOn) {
    fprintf(f, "static const uint8_t s_language_card_rom[0x3000] = {");
    const uint8_t *rom = apple2plus_rom;
    for (const auto &mr : memRanges_)
      if (!mr.writable && mr.from >= 0xD000 && mr.to == 0xFFFF)
        rom = memory_ + 0xD000;
    for (unsigned i = 0; i != 0x3000; ++i) {
      if (i % 16 == 0)
        fprintf(f, "\n  ");
      fprintf(f, "0x%02X%s", rom[i], i == 0x2FFF ? "" : ", ");
    }
    fprintf(f, "\n};\n");
  } else if (languageCard_) {
    const auto &start = *runData_->languageCardStart;
    fprintf(f, "static const uint8_t s_language_card_state[0x7000] = {");
    unsigned i = 0;
    for (const auto *bytes : {&start.bank1, &start.bank2, &start.high, &start.rom}) {
      for (uint8_t byte : *bytes) {
        if (i % 16 == 0)
          fprintf(f, "\n  ");
        fprintf(f, "0x%02X%s", byte, ++i == 0x7000 ? "" : ", ");
      }
    }
    fprintf(f, "\n};\n");
    fprintf(f, "static const uint8_t s_initial_ram[0xc000] = {");
    for (unsigned j = 0; j != start.mainRAM.size(); ++j) {
      if (j % 16 == 0)
        fprintf(f, "\n  ");
      fprintf(f, "0x%02X%s", start.mainRAM[j], j + 1 == start.mainRAM.size() ? "" : ", ");
    }
    fprintf(f, "\n};\n");
  }
  for (auto const &mr : memRanges_) {
    if (languageCard_ && !runData_->languageCardStart->powerOn)
      continue;
    unsigned len = mr.to - mr.from + 1;
    fprintf(f, "static const uint8_t s_mem_%04x[0x%04x] = {", mr.from, len);
    for (unsigned i = 0; i != len; ++i) {
      if (i % 16 == 0)
        fprintf(f, "\n  ");
      else
        fputc(' ', f);
      fprintf(f, "0x%02X", peek(mr.from + i));
      if (i != len - 1)
        fputc(',', f);
    }
    fputc('\n', f);
    fprintf(f, "};\n");
  }
}

void Disas::printSimpleC(FILE *f) {
  printSimpleCPrologue(f);

  fprintf(f, "\n");
  fprintf(f, "void run_emulated(unsigned run_cycles) {\n");
  fprintf(f, "  bool branchTarget = true;\n");
  fprintf(f, "  s_remaining_cycles += (int)run_cycles;\n");
  fprintf(f, "  while (s_remaining_cycles > 0) {\n");
  fprintf(f, "    uint16_t tmp16;\n");
  fprintf(f, "    uint8_t tmp;\n");
  fprintf(f, "    switch (s_pc) {\n");

  for (auto it = asmBlocks_.begin(), end = asmBlocks_.end(); it != end;) {
    const AsmBlock &block = it->second;
    ++it;
    if (!canDecodeCode(block.addr()))
      continue;
    const AsmBlock *nextBlock = it != end ? &it->second : nullptr;
    printSimpleCRange(f, block, nextBlock);
  }

  fprintf(f, "    default:\n");
  fprintf(f, "      fprintf(stderr, \"Unknown code address: $%%04X\\n\", s_pc);\n");
  fprintf(f, "      error_handler(s_pc);\n");
  fprintf(f, "    }\n");
  fprintf(f, "  }\n");
  fprintf(f, "}\n");

  printSimpleCEpilogue(f);
}

void Disas::printSimpleCGenerations(FILE *f, const std::vector<Disas *> &states) {
  if (states.empty())
    throw std::runtime_error("--generation-aware requires at least one materialized state");

  std::vector<SimpleCCode> stateCode(states.size());
  std::vector<std::map<uint16_t, uint32_t>> stateBlocks(states.size());
  std::set<uint16_t> codeBytes;
  std::set<uint16_t> dynamicOperands;
  for (size_t stateIndex = 0; stateIndex < states.size(); ++stateIndex) {
    const Disas &state = *states[stateIndex];
    for (const auto &[blockAddr, block] : state.asmBlocks()) {
      (void)blockAddr;
      if (!state.canDecodeCode((uint16_t)block.addr()))
        continue;
      stateBlocks[stateIndex].emplace((uint16_t)block.addr(), block.size());
      for (const auto [pc, inst] : block.instructions(&state)) {
        if ((uint32_t)pc + inst.size > 0xC000)
          throw std::runtime_error(format(
              "--generation-aware does not support code at or above $C000 (instruction $%04X)",
              pc));
        auto [it, inserted] = stateCode[stateIndex].emplace(pc, inst);
        if (!inserted &&
            (it->second.kind != inst.kind || it->second.addrMode != inst.addrMode ||
             it->second.size != inst.size))
          throw std::runtime_error(format(
              "--generation-aware found conflicting instruction decodes at $%04X", pc));
        for (unsigned i = 0; i < inst.size; ++i)
          codeBytes.insert((uint16_t)(pc + i));
        if (state.recordedOperandChanged(pc))
          dynamicOperands.insert(pc);
      }
    }
  }
  if (codeBytes.empty())
    throw std::runtime_error("--generation-aware found no low-memory code to emit");

  std::set<uint16_t> instructionPCs;
  for (const auto &code : stateCode)
    for (const auto &[pc, inst] : code) {
      (void)inst;
      instructionPCs.insert(pc);
    }
  for (uint16_t pc : instructionPCs) {
    const CPUInst *referenceInst = nullptr;
    size_t referenceState = 0;
    for (size_t stateIndex = 0; stateIndex < stateCode.size(); ++stateIndex) {
      auto inst = stateCode[stateIndex].find(pc);
      if (inst != stateCode[stateIndex].end()) {
        referenceInst = &inst->second;
        referenceState = stateIndex;
        break;
      }
    }
    if (!referenceInst || referenceInst->size <= 1 || dynamicOperands.count(pc))
      continue;
    for (size_t stateIndex = referenceState + 1; stateIndex < stateCode.size(); ++stateIndex) {
      auto other = stateCode[stateIndex].find(pc);
      if (other == stateCode[stateIndex].end() || other->second.size != referenceInst->size)
        continue;
      for (unsigned i = 1; i < referenceInst->size; ++i) {
        if (states[referenceState]->peek((uint16_t)(pc + i)) !=
            states[stateIndex]->peek((uint16_t)(pc + i))) {
          dynamicOperands.insert(pc);
          break;
        }
      }
      if (dynamicOperands.count(pc))
        break;
    }
  }

  using Signature = std::vector<std::pair<uint16_t, uint8_t>>;
  std::vector<Signature> signatures(states.size());
  for (size_t stateIndex = 0; stateIndex < states.size(); ++stateIndex) {
    for (const auto &[pc, inst] : stateCode[stateIndex]) {
      for (unsigned i = 0; i < inst.size; ++i) {
        if (i && dynamicOperands.count(pc))
          continue;
        uint16_t addr = (uint16_t)(pc + i);
        signatures[stateIndex].emplace_back(addr, states[stateIndex]->peek(addr));
      }
    }
  }

  std::vector<size_t> representativeStates;
  for (size_t stateIndex = 0; stateIndex < states.size(); ++stateIndex) {
    size_t representative = 0;
    while (representative < representativeStates.size() &&
           signatures[stateIndex] != signatures[representativeStates[representative]])
      ++representative;
    if (representative == representativeStates.size()) {
      representativeStates.push_back(stateIndex);
    } else if (!sameCode(stateCode[stateIndex], stateCode[representativeStates[representative]]) ||
               stateBlocks[stateIndex] != stateBlocks[representativeStates[representative]]) {
      throw std::runtime_error(format(
          "--generation-aware states %zu and %zu have identical code bytes but different "
          "disassemblies",
          representativeStates[representative],
          stateIndex));
    }
  }

  printSimpleCPrologue(f, true);
  fprintf(f, "\nstatic int generation_match(void) {\n");
  fprintf(f, "  int matched = -1;\n");
  for (size_t candidate = 0; candidate < representativeStates.size(); ++candidate) {
    size_t stateIndex = representativeStates[candidate];
    fprintf(f, "  {\n    bool matches = true;\n");
    for (const auto &[addr, byte] : signatures[stateIndex]) {
      fprintf(f, "    if (s_ram[0x%04x] != 0x%02x) matches = false;\n", addr, byte);
    }
    fprintf(f,
            "    if (matches) { if (matched >= 0) return -2; matched = %zu; }\n",
            candidate);
    fprintf(f, "  }\n");
  }
  fprintf(f, "  return matched;\n}\n");

  fprintf(f,
          "\nstatic bool generation_instruction_matches(unsigned generation, uint16_t pc) {\n"
          "  switch (generation) {\n");
  for (size_t candidate = 0; candidate < representativeStates.size(); ++candidate) {
    const auto &code = stateCode[representativeStates[candidate]];
    fprintf(f, "  case %zu:\n    switch (pc) {\n", candidate);
    for (const auto &[pc, inst] : code) {
      fprintf(f, "    case 0x%04x: return ", pc);
      bool firstByte = true;
      for (unsigned i = 0; i < inst.size; ++i) {
        if (i && dynamicOperands.count(pc))
          continue;
        if (!firstByte)
          fprintf(f, " && ");
        fprintf(f, "s_ram[0x%04x] == 0x%02x", (unsigned)pc + i,
                states[representativeStates[candidate]]->peek((uint16_t)(pc + i)));
        firstByte = false;
      }
      fprintf(f, ";\n");
    }
    fprintf(f, "    default: return false;\n    }\n");
  }
  fprintf(f, "  default: return false;\n  }\n}\n");

  fprintf(f, "\nstatic int generation_match_at_pc(uint16_t pc) {\n  int matched = -1;\n");
  for (size_t candidate = 0; candidate < representativeStates.size(); ++candidate) {
    fprintf(f, "  if (generation_instruction_matches(%zu, pc)) {\n", candidate);
    fprintf(f, "    if (matched >= 0) return -2;\n    matched = %zu;\n  }\n", candidate);
  }
  fprintf(f, "  return matched;\n}\n");

  for (size_t candidate = 0; candidate < representativeStates.size(); ++candidate) {
    Disas &state = *states[representativeStates[candidate]];
    state.simpleCGenerationAware_ = true;
    state.simpleCDynamicOperands_ = dynamicOperands;
    fprintf(f, "\nstatic void run_generation_%zu(bool *branchTargetRef) {\n", candidate);
    fprintf(f, "  bool branchTarget = *branchTargetRef;\n");
    fprintf(f, "  uint16_t tmp16;\n");
    fprintf(f, "  uint8_t tmp;\n");
    fprintf(f, "  switch (s_pc) {\n");
    for (auto it = state.asmBlocks_.begin(), end = state.asmBlocks_.end(); it != end;) {
      const AsmBlock &block = it->second;
      ++it;
      if (!state.canDecodeCode((uint16_t)block.addr()))
        continue;
      const AsmBlock *nextBlock = it != end ? &it->second : nullptr;
      state.printSimpleCRange(f, block, nextBlock);
    }
    fprintf(f, "  default:\n");
    fprintf(f,
            "    fprintf(stderr, \"Unknown code address in generation %%u: $%%04X\\n\", "
            "s_active_generation, s_pc);\n");
    fprintf(f, "    error_handler(s_pc);\n");
    fprintf(f, "  }\n");
    fprintf(f, "  *branchTargetRef = branchTarget;\n");
    fprintf(f, "}\n");
  }

  fprintf(f, "\nvoid run_emulated(unsigned run_cycles) {\n");
  fprintf(f, "  bool branchTarget = true;\n");
  fprintf(f, "  s_remaining_cycles += (int)run_cycles;\n");
  fprintf(f, "  while (s_remaining_cycles > 0) {\n");
  fprintf(f, "    switch (s_active_generation) {\n");
  for (size_t candidate = 0; candidate < representativeStates.size(); ++candidate)
    fprintf(f, "    case %zu: run_generation_%zu(&branchTarget); break;\n", candidate, candidate);
  fprintf(f, "    default: fprintf(stderr, \"Invalid generation state %%u\\n\", "
              "s_active_generation); error_handler(s_pc);\n");
  fprintf(f, "    }\n");
  fprintf(f, "  }\n");
  fprintf(f, "}\n");

  printSimpleCEpilogue(f);
}

void Disas::printSimpleCRange(FILE *f, const AsmBlock &block, const AsmBlock * /*nextBlock*/) {
  if (block.misaligned())
    fprintf(f, "    // WARNING: misaligned block\n");
  fprintf(
      f,
      "    case 0x%04x: // [$%04X..$%04X] %4u bytes\n",
      block.addr(),
      block.addr(),
      block.endAddr() - 1,
      block.size());
  if (simpleCGenerationAware_)
    printGenerationCheck(f, (uint16_t)block.addr(), 0);
  fprintf(f, "      CYCLES(0x%04x, %u);\n", block.addr(), block.baseCycles(this));

  std::vector<std::pair<uint16_t, CPUInst>> instructions;
  for (const auto &instruction : block.instructions(this))
    instructions.push_back(instruction);
  auto emitInstructions = [&](size_t first, unsigned remainingCycles) {
    bool fall = false;
    size_t index = first;
    for (auto it = instructions.begin() + first; it != instructions.end(); ++it, ++index) {
      const auto [addr, inst] = *it;
      unsigned instCycles = simpleCGenerationAware_ && !inst.isInvalid()
                               ? cpuInstCycles(inst.kind, inst.addrMode)
                               : 0;
      if (simpleCGenerationAware_ && index > first)
        printGenerationCheck(f, addr, remainingCycles);
      fall = printSimpleCInst(
          f,
          addr,
          inst,
          simpleCDynamicOperands_.count(addr) ||
              languageCard_ || (runData_ && runData_->firstInstructions));
      remainingCycles -= instCycles;
    }

    if (fall) {
      const AsmBlock *fallBlock = block.getFallBlock();
      assert(fallBlock && "we fell, so there must be a fall block");
      fprintf(f, "      s_pc = 0x%04x;\n", fallBlock->addr());
      fprintf(f, "      break;\n");
    }
  };
  emitInstructions(0, simpleCGenerationAware_ ? block.baseCycles(this) : 0);

  if (!simpleCGenerationAware_)
    return;

  unsigned suffixCycles = block.baseCycles(this);
  for (size_t i = 0; i < instructions.size(); ++i) {
    const auto [addr, inst] = instructions[i];
    if (i != 0 && !cfindAsmBlockAt(addr)) {
      fprintf(f, "    case 0x%04x: // resume inside block $%04X\n", addr, block.addr());
      printGenerationCheck(f, addr, 0);
      fprintf(f, "      s_cycles += %u;\n", suffixCycles);
      fprintf(f, "      s_remaining_cycles -= %u;\n", suffixCycles);
      emitInstructions(i, suffixCycles);
    }
    if (!inst.isInvalid())
      suffixCycles -= cpuInstCycles(inst.kind, inst.addrMode);
  }
}

bool Disas::printSimpleCInst(FILE *f, uint16_t pc, CPUInst inst, bool dynamicOperand) {
  bool fall = true;
  scSelfModOperand_ = false;
  scDynamicOperand_ = dynamicOperand;
  scPC_ = pc;

  if (simpleCGenerationAware_ && simpleCDynamicOperands_.count(pc)) {
    fprintf(f, "      // WARNING: operand self modification.\n");
    scSelfModOperand_ = true;
  }

  if (selfModifers_.count(pc))
    fprintf(f, "      // WARNING: performs self modification.\n");

  if (observedOperandChanged(pc)) {
    fprintf(f, "      // WARNING: operand self modification.\n");
    scSelfModOperand_ = true;
  }
  for (unsigned i = 0, e = inst.size; i != e; ++i) {
    if (!checkSelfModified((uint16_t)(pc + i)))
      continue;
    if (i == 0) {
      fprintf(f, "      // WARNING: opcode self modification.\n");
    } else {
      if (!scSelfModOperand_)
        fprintf(f, "      // WARNING: operand self modification.\n");
      scSelfModOperand_ = true;
      break;
    }
  }

  fprintf(f, "      /* $%04X %s */ ", pc, cpuInstName(inst.kind));
  switch (inst.kind) {
  case CPUInstKind::ADC:
    fprintf(
        f,
        "tmp = %s, s_a = "
        "s_status & STATUS_D"
        " ? adc_decimal(s_a, tmp)"
        " : update_nzvc(s_a + tmp + (s_status & STATUS_C), s_a, tmp);",
        simpleCRead(inst).c_str());
    break;
  case CPUInstKind::SBC:
    fprintf(
        f,
        "tmp = %s, s_a = "
        "s_status & STATUS_D"
        " ? sbc_decimal(s_a, tmp)"
        " : update_nzv_inv_c(s_a - tmp - (~s_status & STATUS_C), s_a, ~tmp);",
        simpleCRead(inst).c_str());
    break;
  case CPUInstKind::AND:
    fprintf(f, "s_a = update_nz(s_a & %s);", simpleCRead(inst).c_str());
    break;
  case CPUInstKind::ORA:
    fprintf(f, "s_a = update_nz(s_a | %s);", simpleCRead(inst).c_str());
    break;
  case CPUInstKind::EOR:
    fprintf(f, "s_a = update_nz(s_a ^ %s);", simpleCRead(inst).c_str());
    break;
  // cpuInstCycles() gives the base (not-taken) branch cost, already charged by
  // the CYCLES() call at the top of this block. The extra +1 a taken branch
  // costs belongs only on the taken path, so it is folded directly into the
  // taken arm of the ternary that already decides s_pc -- a compile-time
  // constant added where the branch outcome is already being tested, not a
  // new runtime test. This mirrors BR_COND in lib/cpuemu/emu6502.cpp, which
  // adds its +1 only when the branch actually takes.
  case CPUInstKind::BCC:
    fprintf(
        f,
        "s_pc = !(s_status & STATUS_C) ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BCS:
    fprintf(
        f,
        "s_pc = s_status & STATUS_C ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BEQ:
    fprintf(
        f,
        "s_pc = s_status & STATUS_Z ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BNE:
    fprintf(
        f,
        "s_pc = !(s_status & STATUS_Z) ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BMI:
    fprintf(
        f,
        "s_pc = s_status & STATUS_N ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BPL:
    fprintf(
        f,
        "s_pc = !(s_status & STATUS_N) ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BVC:
    fprintf(
        f,
        "s_pc = !(s_status & STATUS_V) ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BVS:
    fprintf(
        f,
        "s_pc = s_status & STATUS_V ? (s_cycles++, s_remaining_cycles--, %s) : 0x%04x;\n",
        simpleCAddr(inst).c_str(),
        pc + 2);
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::BIT:
    fprintf(
        f,
        "tmp = %s, "
        "s_status = (s_status & ~(0xC0 | STATUS_Z)) | (tmp & 0xC0) | (s_a & tmp ? 0 : STATUS_Z);",
        simpleCRead(inst).c_str());
    break;
  case CPUInstKind::BRK:
    fprintf(
        f,
        R"(fprintf(stderr, "Warning: BRK at $%%04X\n", 0x%04x);)"
        "\n",
        pc);
    fprintf(f, "%21s push16(0x%04x);\n", "", (uint16_t)(pc + 2));
    fprintf(f, "%21s push8(s_status | STATUS_B);\n", "");
    if (languageCard_)
      fprintf(f, "%21s s_pc = peek16(0xfffe);\n", "");
    else
      fprintf(f, "%21s s_pc = 0x%04x;\n", "", peek16(A2_IRQ_VEC));
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;

  case CPUInstKind::CLC:
    fprintf(f, "s_status &= ~STATUS_C;");
    break;
  case CPUInstKind::CLD:
    fprintf(f, "s_status &= ~STATUS_D;");
    break;
  case CPUInstKind::CLI:
    fprintf(f, "s_status &= ~STATUS_I;");
    break;
  case CPUInstKind::CLV:
    fprintf(f, "s_status &= ~STATUS_V;");
    break;
  case CPUInstKind::SEC:
    fprintf(f, "s_status |= STATUS_C;");
    break;
  case CPUInstKind::SED:
    fprintf(f, "s_status |= STATUS_D;");
    break;
  case CPUInstKind::SEI:
    fprintf(f, "s_status |= STATUS_I;");
    break;

  case CPUInstKind::CMP:
    fprintf(f, "update_nz_inv_c(s_a - %s);", simpleCRead(inst).c_str());
    break;
  case CPUInstKind::CPX:
    fprintf(f, "update_nz_inv_c(s_x - %s);", simpleCRead(inst).c_str());
    break;
  case CPUInstKind::CPY:
    fprintf(f, "update_nz_inv_c(s_y - %s);", simpleCRead(inst).c_str());
    break;
  case CPUInstKind::DEC:
    fprintf(
        f,
        "tmp16 = %s, %s(tmp16, update_nz(%s(tmp16) - 1));",
        simpleCAddr(inst).c_str(),
        simpleCWriteOp(inst),
        simpleCReadOp(inst));
    break;
  case CPUInstKind::INC:
    fprintf(
        f,
        "tmp16 = %s, %s(tmp16, update_nz(%s(tmp16) + 1));",
        simpleCAddr(inst).c_str(),
        simpleCWriteOp(inst),
        simpleCReadOp(inst));
    break;
  case CPUInstKind::DEX:
    fprintf(f, "s_x = update_nz(s_x - 1);");
    break;
  case CPUInstKind::DEY:
    fprintf(f, "s_y = update_nz(s_y - 1);");
    break;
  case CPUInstKind::INX:
    fprintf(f, "s_x = update_nz(s_x + 1);");
    break;
  case CPUInstKind::INY:
    fprintf(f, "s_y = update_nz(s_y + 1);");
    break;

  case CPUInstKind::JMP:
    fprintf(f, "s_pc = %s;\n", simpleCAddr(inst).c_str());
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::JSR:
    fprintf(f, "push16(0x%04x), s_pc = %s;\n", pc + 2, simpleCAddr(inst).c_str());
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;

  case CPUInstKind::LDA:
    fprintf(f, "s_a = update_nz(%s);", simpleCRead(inst).c_str());
    break;
  case CPUInstKind::LDX:
    fprintf(f, "s_x = update_nz(%s);", simpleCRead(inst).c_str());
    break;
  case CPUInstKind::LDY:
    fprintf(f, "s_y = update_nz(%s);", simpleCRead(inst).c_str());
    break;

  case CPUInstKind::ASL:
    if (inst.addrMode == CPUAddrMode::A) {
      fprintf(f, "s_a = update_nzc(s_a << 1);");
    } else {
      fprintf(
          f,
          "tmp16 = %s, %s(tmp16, update_nzc(%s(tmp16) << 1));",
          simpleCAddr(inst).c_str(),
          simpleCWriteOp(inst),
          simpleCReadOp(inst));
    }
    break;
  case CPUInstKind::LSR:
    if (inst.addrMode == CPUAddrMode::A) {
      fprintf(f, "set_c_to_bit0(s_a), s_a = update_nz(s_a >> 1);");
    } else {
      fprintf(
          f,
          "tmp16 = %s, tmp = %s(tmp16), set_c_to_bit0(tmp), %s(tmp16, update_nz(tmp >> 1));",
          simpleCAddr(inst).c_str(),
          simpleCReadOp(inst),
          simpleCWriteOp(inst));
    }
    break;
  case CPUInstKind::ROL:
    if (inst.addrMode == CPUAddrMode::A) {
      fprintf(
          f,
          "tmp = s_a, "
          "s_a = update_nz((tmp << 1) | (s_status & STATUS_C)), "
          "set_c_to_bit0(tmp >> 7);");
    } else {
      fprintf(
          f,
          "tmp16 = %s, "
          "tmp = %s(tmp16), "
          "%s(tmp16, update_nz((tmp << 1) | (s_status & STATUS_C))), "
          "set_c_to_bit0(tmp >> 7);",
          simpleCAddr(inst).c_str(),
          simpleCReadOp(inst),
          simpleCWriteOp(inst));
    }
    break;
  case CPUInstKind::ROR:
    if (inst.addrMode == CPUAddrMode::A) {
      fprintf(
          f,
          "tmp = s_a, "
          "s_a = update_nz((tmp >> 1) | ((s_status & STATUS_C) << 7)), "
          "set_c_to_bit0(tmp);");
    } else {
      fprintf(
          f,
          "tmp16 = %s, "
          "tmp = %s(tmp16), "
          "%s(tmp16, update_nz((tmp >> 1) | ((s_status & STATUS_C) << 7))), "
          "set_c_to_bit0(tmp);",
          simpleCAddr(inst).c_str(),
          simpleCReadOp(inst),
          simpleCWriteOp(inst));
    }
    break;

  case CPUInstKind::NOP:
    fprintf(f, "(void)0;");
    break;

  case CPUInstKind::PHA:
    fprintf(f, "push8(s_a);");
    break;
  case CPUInstKind::PHP:
    fprintf(f, "push8(s_status | STATUS_B);");
    break;
  case CPUInstKind::PLA:
    fprintf(f, "s_a = update_nz(pop8());");
    break;
  case CPUInstKind::PLP:
    fprintf(f, "s_status = pop8() & ~STATUS_B;");
    break;

  case CPUInstKind::RTI:
    fprintf(f, "s_status = pop8() & ~STATUS_B, s_pc = pop16();\n");
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;
  case CPUInstKind::RTS:
    fprintf(f, "s_pc = pop16() + 1;\n");
    fprintf(f, "      branchTarget = true;\n");
    fprintf(f, "      break;");
    fall = false;
    break;

  case CPUInstKind::STA:
    fprintf(f, "%s(%s, s_a);", simpleCWriteOp(inst), simpleCAddr(inst).c_str());
    break;
  case CPUInstKind::STX:
    fprintf(f, "%s(%s, s_x);", simpleCWriteOp(inst), simpleCAddr(inst).c_str());
    break;
  case CPUInstKind::STY:
    fprintf(f, "%s(%s, s_y);", simpleCWriteOp(inst), simpleCAddr(inst).c_str());
    break;
  case CPUInstKind::TAX:
    fprintf(f, "s_x = update_nz(s_a);");
    break;
  case CPUInstKind::TAY:
    fprintf(f, "s_y = update_nz(s_a);");
    break;
  case CPUInstKind::TSX:
    fprintf(f, "s_x = update_nz(s_sp);");
    break;
  case CPUInstKind::TXA:
    fprintf(f, "s_a = update_nz(s_x);");
    break;
  case CPUInstKind::TXS:
    fprintf(f, "s_sp = s_x;");
    break;
  case CPUInstKind::TYA:
    fprintf(f, "s_a = update_nz(s_y);");
    break;

  case CPUInstKind::INVALID:
    fprintf(f, "fprintf(stderr, \"Warning: INVALID at $%%04X\\n\", 0x%04x);\n", pc);
    fprintf(f, "%21s error_handler(s_pc);\n", "");
    fprintf(f, "      break;");
    fall = false;
    break;
  }
  fputc('\n', f);
  return fall;
}

std::string Disas::simpleCRead(CPUInst inst) const {
  switch (inst.addrMode) {
  case CPUAddrMode::A:
  case CPUAddrMode::Imm:
    return simpleCAddr(inst);
  case CPUAddrMode::Rel:
  case CPUAddrMode::Implied:
    abort();

  case CPUAddrMode::Abs:
  case CPUAddrMode::Abs_X:
  case CPUAddrMode::Abs_Y:
  case CPUAddrMode::Ind:
  case CPUAddrMode::X_Ind:
  case CPUAddrMode::Ind_Y:
    return "peek(" + simpleCAddr(inst) + ")";

  case CPUAddrMode::Zpg:
  case CPUAddrMode::Zpg_X:
  case CPUAddrMode::Zpg_Y:
    return "peek_zpg(" + simpleCAddr(inst) + ")";

  case CPUAddrMode::_last:
  case CPUAddrMode::_invalid:
  default:
    abort();
  }
}

const char *Disas::simpleCReadOp(CPUInst inst) {
  switch (inst.addrMode) {
  case CPUAddrMode::A:
    return "s_a";

  case CPUAddrMode::Imm:
  case CPUAddrMode::Rel:
  case CPUAddrMode::Implied:
    abort();

  case CPUAddrMode::Abs:
  case CPUAddrMode::Abs_X:
  case CPUAddrMode::Abs_Y:
  case CPUAddrMode::Ind:
  case CPUAddrMode::X_Ind:
  case CPUAddrMode::Ind_Y:
    return "peek";

  case CPUAddrMode::Zpg:
  case CPUAddrMode::Zpg_X:
  case CPUAddrMode::Zpg_Y:
    return "peek_zpg";

  case CPUAddrMode::_last:
  case CPUAddrMode::_invalid:
  default:
    abort();
  }
}

const char *Disas::simpleCWriteOp(CPUInst inst) {
  switch (inst.addrMode) {
  case CPUAddrMode::A:
    return "s_a = ";

  case CPUAddrMode::Imm:
  case CPUAddrMode::Rel:
  case CPUAddrMode::Implied:
    abort();

  case CPUAddrMode::Abs:
  case CPUAddrMode::Abs_X:
  case CPUAddrMode::Abs_Y:
  case CPUAddrMode::Ind:
  case CPUAddrMode::X_Ind:
  case CPUAddrMode::Ind_Y:
    return "poke";

  case CPUAddrMode::Zpg:
  case CPUAddrMode::Zpg_X:
  case CPUAddrMode::Zpg_Y:
    return "poke_zpg";

  case CPUAddrMode::_last:
  case CPUAddrMode::_invalid:
  default:
    abort();
  }
}

std::string Disas::simpleCAddr(CPUInst inst) const {
  char buf[64];
  const char *read8 = scDynamicOperand_ ? "peek" : "ram_peek";
  const char *read16 = scDynamicOperand_ ? "peek16" : "ram_peek16";
  if (!scSelfModOperand_) {
    switch (inst.addrMode) {
    case CPUAddrMode::A:
      return "s_a";
    case CPUAddrMode::Abs:
      snprintf(buf, sizeof(buf), "0x%04x", inst.operand);
      break;
    case CPUAddrMode::Abs_X:
      snprintf(buf, sizeof(buf), "0x%04x + s_x", inst.operand);
      break;
    case CPUAddrMode::Abs_Y:
      snprintf(buf, sizeof(buf), "0x%04x + s_y", inst.operand);
      break;
    case CPUAddrMode::Imm:
      snprintf(buf, sizeof(buf), "0x%02x", inst.operand);
      break;
    case CPUAddrMode::Implied:
      return "";
    case CPUAddrMode::Ind:
      snprintf(buf, sizeof(buf), "peek16(0x%04x)", inst.operand);
      break;
    case CPUAddrMode::X_Ind:
      snprintf(buf, sizeof(buf), "peek16_zpg(0x%02x + s_x)", inst.operand);
      break;
    case CPUAddrMode::Ind_Y:
      snprintf(buf, sizeof(buf), "peek16_zpg(0x%02x) + s_y", inst.operand);
      break;
    case CPUAddrMode::Rel:
      snprintf(buf, sizeof(buf), "0x%04x", inst.operand);
      break;
    case CPUAddrMode::Zpg:
      snprintf(buf, sizeof(buf), "0x%02x", inst.operand);
      break;
    case CPUAddrMode::Zpg_X:
      snprintf(buf, sizeof(buf), "(uint8_t)(0x%02x + s_x)", inst.operand);
      break;
    case CPUAddrMode::Zpg_Y:
      snprintf(buf, sizeof(buf), "(uint8_t)(0x%02x + s_y)", inst.operand);
      break;
    case CPUAddrMode::_last:
    case CPUAddrMode::_invalid:
    default:
      abort();
    }
  } else {
    switch (inst.addrMode) {
    case CPUAddrMode::A:
      return "s_a";
    case CPUAddrMode::Abs:
      snprintf(buf, sizeof(buf), "%s(0x%04x)", read16, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Abs_X:
      snprintf(buf, sizeof(buf), "%s(0x%04x) + s_x", read16, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Abs_Y:
      snprintf(buf, sizeof(buf), "%s(0x%04x) + s_y", read16, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Imm:
      snprintf(buf, sizeof(buf), "%s(0x%04x)", read8, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Implied:
      return "";
    case CPUAddrMode::Ind:
      snprintf(buf, sizeof(buf), "peek16(%s(0x%04x))", read16, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::X_Ind:
      snprintf(buf, sizeof(buf), "peek16_zpg(%s(0x%04x) + s_x)", read8, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Ind_Y:
      snprintf(buf, sizeof(buf), "peek16_zpg(%s(0x%04x)) + s_y", read8, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Rel:
      snprintf(
          buf,
          sizeof(buf),
          "(uint16_t)(0x%04x + (int8_t)%s(0x%04x))",
          (uint16_t)(scPC_ + 2),
          read8,
          (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Zpg:
      snprintf(buf, sizeof(buf), "%s(0x%04x)", read8, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Zpg_X:
      snprintf(buf, sizeof(buf), "(uint8_t)(%s(0x%04x) + s_x)", read8, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::Zpg_Y:
      snprintf(buf, sizeof(buf), "(uint8_t)(%s(0x%04x) + s_y)", read8, (uint16_t)(scPC_ + 1));
      break;
    case CPUAddrMode::_last:
    case CPUAddrMode::_invalid:
    default:
      abort();
    }
  }
  return buf;
}
