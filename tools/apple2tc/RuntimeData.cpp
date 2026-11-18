/*
 * Copyright (c) Tzvetan Mikov.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Disas.h"

#include "apple2tc/support.h"

#include "nlohmann/json.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

static std::vector<uint8_t> loadLanguageCardBytes(
    const json &value, size_t expectedSize, const std::string &path) {
  if (!value.is_array() || value.size() != expectedSize)
    throw std::runtime_error("Invalid LanguageCardStart byte array in " + path);
  std::vector<uint8_t> bytes;
  bytes.reserve(expectedSize);
  for (const auto &entry : value) {
    if (!entry.is_number_integer() || entry < 0 || entry > 255)
      throw std::runtime_error("Invalid LanguageCardStart byte in " + path);
    bytes.push_back(entry.get<uint8_t>());
  }
  return bytes;
}

const Regs *RuntimeData::getStartRegs() const {
  if (baseStats)
    return &baseStats->startRegs;
  if (!generations.empty())
    return &generations[0].regs;
  return nullptr;
}

static Regs loadRegs(const json &jsonRegs) {
  return Regs{
      .pc = jsonRegs["pc"],
      .a = jsonRegs["a"],
      .x = jsonRegs["x"],
      .y = jsonRegs["y"],
      .status = jsonRegs["status"],
      .sp = jsonRegs["sp"],
  };
}

std::unique_ptr<RuntimeData> RuntimeData::load(const std::string &path) {
  json root;
  {
    std::ifstream is(path);
    is >> root;
    if (is.bad())
      throw std::runtime_error("Error reading from " + path);
  }

  auto res = std::make_unique<RuntimeData>();

  auto it = root.find("BaseStats");
  if (it != root.end()) {
    const json &bs = *it;
    auto baseStats = std::make_unique<BaseStats>();
    baseStats->limit = bs["limit"];
    baseStats->startRegs = loadRegs(bs["startRegs"]);
    baseStats->decimalSet = bs["decimalSet"];
    baseStats->decimalADC = bs["decimalADC"];
    baseStats->decimalSBC = bs["decimalSBC"];
    baseStats->stackOverflow = bs["stackOverflow"];
    baseStats->stackUnderflow = bs["stackUnderflow"];

    res->baseStats = std::move(baseStats);
  }

  it = root.find("LanguageCardStart");
  if (it != root.end()) {
    res->languageCardStart.emplace();
    auto &start = *res->languageCardStart;
    if (*it == "power-on") {
      start.powerOn = true;
    } else {
      start.bank1 = loadLanguageCardBytes(it->at("bank1"), 0x1000, path);
      start.bank2 = loadLanguageCardBytes(it->at("bank2"), 0x1000, path);
      start.high = loadLanguageCardBytes(it->at("high"), 0x2000, path);
      start.rom = loadLanguageCardBytes(it->at("rom"), 0x3000, path);
      start.mainRAM = loadLanguageCardBytes(it->at("mainRAM"), 0xC000, path);
      start.readRom = it->at("readRom").get<bool>();
      start.bank = it->at("bank").get<unsigned>();
      start.writeProtected = it->at("writeProtected").get<bool>();
      unsigned prev = it->at("prevIOAddr").get<unsigned>();
      if ((start.bank != 1 && start.bank != 2) || prev > 0xFFFF)
        throw std::runtime_error("Invalid LanguageCardStart in " + path);
      start.prevIOAddr = (uint16_t)prev;
    }
  }

  res->branchTargets = static_cast<std::vector<uint16_t>>(root["BranchTargets"]);

  it = root.find("FirstInstructions");
  if (it != root.end()) {
    if (!it->is_array())
      throw std::runtime_error("Invalid FirstInstructions in " + path);
    res->firstInstructions.emplace();
    bool missingOperandChanges = false;
    for (const auto &entry : *it) {
      if (!entry.is_array() || (entry.size() != 4 && entry.size() != 5))
        throw std::runtime_error("Invalid FirstInstructions entry in " + path);
      for (size_t i = 0; i < 4; ++i) {
        const auto &value = entry[i];
        if (!value.is_number_integer() || value < 0)
          throw std::runtime_error("Invalid FirstInstructions value in " + path);
      }
      if (entry[0] > 0xFFFF || entry[1] > 0xFF || entry[2] > 0xFF || entry[3] > 0xFF)
        throw std::runtime_error("Invalid FirstInstructions value in " + path);
      if (entry.size() == 5 && !entry[4].is_boolean())
        throw std::runtime_error("Invalid FirstInstructions operand-change flag in " + path);
      missingOperandChanges |= entry.size() == 4;
      unsigned addr = entry[0].get<unsigned>();
      unsigned op = entry[1].get<unsigned>();
      unsigned lo = entry[2].get<unsigned>();
      unsigned hi = entry[3].get<unsigned>();
      bool operandChanged = entry.size() == 5 && entry[4].get<bool>();
      if (!res->firstInstructions
               ->emplace(
                   (uint16_t)addr,
                   ObservedInstruction{
                       ThreeBytes{{(uint8_t)op, (uint8_t)lo, (uint8_t)hi}}, operandChanged})
               .second)
        throw std::runtime_error("Invalid or duplicate FirstInstructions entry in " + path);
    }
    if (missingOperandChanges)
      fprintf(stderr,
              "warning: FirstInstructions in %s lacks operand-change data; "
              "recollect to detect changes missed by static analysis\n",
              path.c_str());
  }

  it = root.find("Branches");
  if (it != root.end()) {
    for (auto &[originStr, jsonTargets] : it->items()) {
      auto origin = (uint16_t)std::stoul(originStr);
      std::vector<uint16_t> targets = jsonTargets;
      // Make sure the targets are sorted.
      std::sort(targets.begin(), targets.end());
      res->allBranches.try_emplace(origin, std::move(targets));
    }
  }

  for (const json &jgen : root["generations"]) {
    res->generations.emplace_back();

    const auto &jregs = jgen["regs"];
    res->generations.back().regs = loadRegs(jgen["regs"]);

    for (const json &jseg : jgen["code"]) {
      res->generations.back().code.push_back(Segment{.addr = jseg["addr"], .bytes = jseg["bytes"]});
    }
  }

  return res;
}

std::vector<CodeAtEdge> loadCodeAt(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rt");
  if (!f)
    throw std::runtime_error(format("%s: %s", path.c_str(), strerror(errno)));
  auto contents = readAll<std::string>(f);
  fclose(f);

  std::vector<CodeAtEdge> res{};
  unsigned lineNum = 0;
  for (size_t pos = 0; pos <= contents.size();) {
    size_t eol = contents.find('\n', pos);
    if (eol == std::string::npos)
      eol = contents.size();
    std::string line = contents.substr(pos, eol - pos);
    pos = eol + 1;
    ++lineNum;

    auto fail = [&path, lineNum](const std::string &what) {
      throw std::runtime_error(format("%s:%u: %s", path.c_str(), lineNum, what.c_str()));
    };

    if (auto comment = line.find('#'); comment != std::string::npos)
      line.erase(comment);

    // Split into whitespace separated words.
    std::vector<std::string> words{};
    for (size_t i = 0; i != line.size();) {
      if (isspace((unsigned char)line[i])) {
        ++i;
        continue;
      }
      size_t start = i;
      while (i != line.size() && !isspace((unsigned char)line[i]))
        ++i;
      words.push_back(line.substr(start, i - start));
    }

    if (words.empty())
      continue;
    if (words.size() != 2)
      fail("expected '<hex-origin> <hex-target>'");

    uint16_t addr[2] = {0, 0};
    for (unsigned i = 0; i != 2; ++i) {
      // A '$' prefix is optional, so the file can look like 6502 source.
      std::string word = words[i][0] == '$' ? words[i].substr(1) : words[i];
      char *end;
      unsigned long value = strtoul(word.c_str(), &end, 16);
      if (word.empty() || *end)
        fail(format("invalid hexadecimal address '%s'", words[i].c_str()));
      if (value > 0xFFFF)
        fail(format("address '%s' is not a 16-bit value", words[i].c_str()));
      addr[i] = (uint16_t)value;
    }

    res.push_back(CodeAtEdge{.origin = addr[0], .target = addr[1]});
  }

  return res;
}

std::vector<uint16_t> loadInlineStr(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rt");
  if (!f)
    throw std::runtime_error(format("%s: %s", path.c_str(), strerror(errno)));
  auto contents = readAll<std::string>(f);
  fclose(f);

  std::vector<uint16_t> res{};
  unsigned lineNum = 0;
  for (size_t pos = 0; pos <= contents.size();) {
    size_t eol = contents.find('\n', pos);
    if (eol == std::string::npos)
      eol = contents.size();
    std::string line = contents.substr(pos, eol - pos);
    pos = eol + 1;
    ++lineNum;

    auto fail = [&path, lineNum](const std::string &what) {
      throw std::runtime_error(format("%s:%u: %s", path.c_str(), lineNum, what.c_str()));
    };

    if (auto comment = line.find('#'); comment != std::string::npos)
      line.erase(comment);

    // Split into whitespace separated words.
    std::vector<std::string> words{};
    for (size_t i = 0; i != line.size();) {
      if (isspace((unsigned char)line[i])) {
        ++i;
        continue;
      }
      size_t start = i;
      while (i != line.size() && !isspace((unsigned char)line[i]))
        ++i;
      words.push_back(line.substr(start, i - start));
    }

    if (words.empty())
      continue;
    if (words.size() != 1)
      fail("expected a single hexadecimal routine address");

    // A '$' prefix is optional, so the file can look like 6502 source.
    std::string word = words[0][0] == '$' ? words[0].substr(1) : words[0];
    char *end;
    unsigned long value = strtoul(word.c_str(), &end, 16);
    if (word.empty() || *end)
      fail(format("invalid hexadecimal address '%s'", words[0].c_str()));
    if (value > 0xFFFF)
      fail(format("address '%s' is not a 16-bit value", words[0].c_str()));

    res.push_back((uint16_t)value);
  }

  return res;
}

std::vector<KnownDataRange> loadKnownData(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rt");
  if (!f)
    throw std::runtime_error(format("%s: %s", path.c_str(), strerror(errno)));
  auto contents = readAll<std::string>(f);
  fclose(f);

  std::vector<KnownDataRange> res{};
  unsigned lineNum = 0;
  for (size_t pos = 0; pos <= contents.size();) {
    size_t eol = contents.find('\n', pos);
    if (eol == std::string::npos)
      eol = contents.size();
    std::string line = contents.substr(pos, eol - pos);
    pos = eol + 1;
    ++lineNum;

    auto fail = [&path, lineNum](const std::string &what) {
      throw std::runtime_error(format("%s:%u: %s", path.c_str(), lineNum, what.c_str()));
    };

    if (auto comment = line.find('#'); comment != std::string::npos)
      line.erase(comment);

    std::vector<std::string> words{};
    for (size_t i = 0; i != line.size();) {
      if (isspace((unsigned char)line[i])) {
        ++i;
        continue;
      }
      size_t start = i;
      while (i != line.size() && !isspace((unsigned char)line[i]))
        ++i;
      words.push_back(line.substr(start, i - start));
    }

    if (words.empty())
      continue;
    if (words.size() < 3)
      fail("expected '<hex-from> <hex-to> <name>'");

    uint16_t addr[2] = {0, 0};
    for (unsigned i = 0; i != 2; ++i) {
      std::string word = words[i][0] == '$' ? words[i].substr(1) : words[i];
      char *end;
      unsigned long value = strtoul(word.c_str(), &end, 16);
      if (word.empty() || *end)
        fail(format("invalid hexadecimal address '%s'", words[i].c_str()));
      if (value > 0xFFFF)
        fail(format("address '%s' is not a 16-bit value", words[i].c_str()));
      addr[i] = (uint16_t)value;
    }
    if (addr[0] > addr[1])
      fail("start address is above end address");

    // Everything after the two addresses is the name, joined with spaces.
    std::string name = words[2];
    for (size_t i = 3; i != words.size(); ++i)
      name += " " + words[i];

    res.push_back(KnownDataRange{.from = addr[0], .to = addr[1], .name = std::move(name)});
  }

  return res;
}
