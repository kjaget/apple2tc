/*
 * Copyright (c) Tzvetan Mikov.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "apple2tc/DebugState6502.h"

#include "nlohmann/json.hpp"

#include <stdexcept>

using json = nlohmann::json;

static json saveRegs(const Emu6502::Regs &regs) {
  json jsonRegs;
  jsonRegs["pc"] = regs.pc;
  jsonRegs["a"] = regs.a;
  jsonRegs["x"] = regs.x;
  jsonRegs["y"] = regs.y;
  jsonRegs["status"] = regs.status;
  jsonRegs["sp"] = regs.sp;
  return jsonRegs;
}

void DebugState6502::finishCollection(const Emu6502 *emu, std::ostream &os) {
  if (mode_ == Mode::None)
    return;
  if (mode_ != Mode::Collect) {
    throw std::logic_error("Not currently collecting");
  }
  mode_ = Mode::None;

  saveGeneration(emu, emu->getRegs());

  json root;

  json stats;
  stats["limit"] = icount_;
  stats["startRegs"] = saveRegs(collected_.startRegs);
  stats["decimalSet"] = collected_.decimalSet;
  stats["decimalADC"] = collected_.decimalADC;
  stats["decimalSBC"] = collected_.decimalSBC;
  stats["stackOverflow"] = collected_.stackOverflow;
  stats["stackUnderflow"] = collected_.stackUnderflow;

  root["BaseStats"] = stats;
  // BranchTargets.
  {
    std::vector<uint16_t> branchTargets;
    branchTargets.reserve(branchTargets_.size());
    branchTargets.insert(branchTargets.begin(), branchTargets_.begin(), branchTargets_.end());
    std::sort(branchTargets.begin(), branchTargets.end());
    root["BranchTargets"] = json(branchTargets);
  }
  // Branches.
  {
    std::vector<BranchDesc> allBranches;
    allBranches.reserve(allBranches_.size());
    allBranches.insert(allBranches.begin(), allBranches_.begin(), allBranches_.end());
    // Sort by
    std::sort(allBranches.begin(), allBranches.end(), [](BranchDesc a, BranchDesc b) {
      /// Sorts first by origin, then by target.
      return a.origin < b.origin || a.origin == b.origin && a.target < b.target;
    });

    json branches = json::object();
    json targets{};
    bool first = true;
    BranchDesc lastDesc = {0, 0};
    for (auto bd : allBranches) {
      if (!first && bd.origin != lastDesc.origin) {
        branches[std::to_string(lastDesc.origin)] = targets;
        targets.clear();
      }
      first = false;
      targets.push_back(bd.target);
      lastDesc = bd;
    }
    if (!first)
      branches[std::to_string(lastDesc.origin)] = targets;
    root["Branches"] = branches;
  }

  json firstInstructions = json::array();
  for (const auto &[addr, instruction] : firstInstructions_) {
    const auto &bytes = instruction.bytes;
    firstInstructions.push_back(
        {addr, bytes.d[0], bytes.d[1], bytes.d[2], instruction.operandChanged});
  }
  root["FirstInstructions"] = std::move(firstInstructions);
  if (languageCardStart_) {
    const auto &card = languageCardStart_->card;
    root["LanguageCardStart"] = {
        {"bank1", std::vector<uint8_t>(card.bank1_ram, card.bank1_ram + sizeof(card.bank1_ram))},
        {"bank2", std::vector<uint8_t>(card.bank2_ram, card.bank2_ram + sizeof(card.bank2_ram))},
        {"high", std::vector<uint8_t>(card.high_ram, card.high_ram + sizeof(card.high_ram))},
        {"rom", std::vector<uint8_t>(card.rom, card.rom + sizeof(card.rom))},
        {"readRom", card.banked_memory_read_rom},
        {"bank", card.banked_memory_ram_select == A2_LC_RAM1 ? 1 : 2},
        {"writeProtected", card.banked_memory_write_protect},
        {"prevIOAddr", languageCardStart_->prevIOAddr},
        {"mainRAM", languageCardStart_->mainRAM},
    };
  }

  json jsonGens;
  for (const auto &gen : generations_) {
    json jsonGen;
    jsonGen["regs"] = saveRegs(gen.regs);

    json jsonCode = json::array();
    auto dataIt = gen.data.begin();
    for (const auto &desc : gen.descs) {
      json jsonDesc;
      jsonDesc["addr"] = desc.addr;
      jsonDesc["bytes"] = std::vector<uint8_t>(dataIt, dataIt + desc.len);
      dataIt += desc.len;

      jsonCode.push_back(std::move(jsonDesc));
    }
    jsonGen["code"] = std::move(jsonCode);

    jsonGens.push_back(std::move(jsonGen));
  }

  root["generations"] = std::move(jsonGens);

  os << root;
}