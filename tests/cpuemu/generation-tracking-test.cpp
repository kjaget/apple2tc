#include "apple2tc/DebugState6502.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

static void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

static void testTracking() {
  GenerationMemoryTracking tracking;
  check(tracking.size() == 1, "initial entry");
  tracking[0].written.set(0x10, true);
  tracking[0].execStart.set(0x20, true);
  tracking[0].execFull.setMulti(0x20, 0x23, true);
  tracking.appendCopy();
  check(tracking.size() == 2, "append");
  check(tracking[1].written.get(0x10), "inherited written bit");
  check(tracking[1].execStart.get(0x20), "inherited instruction start");
  check(tracking[1].execFull.get(0x22), "inherited instruction bytes");

  tracking[1].written.set(0x10, false);
  tracking[1].execStart.set(0x21, true);
  tracking[1].execFull.set(0x22, false);
  tracking[0].written.set(0x30, true);
  tracking[0].execStart.set(0x20, false);
  tracking[0].execFull.set(0x21, false);
  check(tracking[0].written.get(0x10), "later write does not affect earlier entry");
  check(!tracking[1].written.get(0x30), "earlier write does not affect later entry");
  check(tracking[1].execStart.get(0x20), "earlier clear does not affect later entry");
  check(!tracking[0].execStart.get(0x21), "later start does not affect earlier entry");
  check(tracking[0].execFull.get(0x22), "later clear does not affect earlier bytes");
  check(tracking[1].execFull.get(0x21), "earlier clear does not affect later bytes");

  tracking.appendCopy();
  check(!tracking[2].written.get(0x10) && tracking[2].execStart.get(0x21) &&
            !tracking[2].execFull.get(0x22),
        "append inherits the latest entry, including cleared bits");
  tracking[2].execFull.set(0xffff, true);
  check(!tracking[1].execFull.get(0xffff), "appended entries are independent");

  tracking.reset();
  check(tracking.size() == 1, "reset removes later entries");
  check(!tracking[0].written.get(0x10) && !tracking[0].written.get(0x30) &&
            !tracking[0].execStart.get(0x21) && !tracking[0].execFull.get(0x20),
        "reset clears the initial entry");
  tracking.appendCopy();
  check(!tracking[1].written.get(0x30) && !tracking[1].execFull.get(0xffff),
        "append after reset does not inherit old entries");
}

static std::string collectNop(DebugState6502 &debug, Emu6502 &emu) {
  emu.setRegs(Emu6502::Regs{0x0900, 0, 0, 0, Emu6502::STATUS_IGNORED, 0xff});
  emu.poke(0x0900, 0xea);
  debug.setModeCollect(&emu, 0);
  DebugState6502::debugStateCB(&debug, &emu, 0x0900);
  std::ostringstream result;
  debug.finishCollection(&emu, result);
  return result.str();
}

static void testCollectionReset() {
  Emu6502 emu(0xc000, 0xcfff);
  DebugState6502 reused;
  emu.setRegs(Emu6502::Regs{0x0300, 0, 0, 0, Emu6502::STATUS_IGNORED, 0xff});
  emu.poke(0x0300, 0x8d);
  emu.poke(0x0301, 0x00);
  emu.poke(0x0302, 0x09);
  reused.setModeCollect(&emu, 0);
  DebugState6502::debugStateCB(&reused, &emu, 0x0300);
  std::ostringstream ignored;
  reused.finishCollection(&emu, ignored);
  reused.clearCollectedData();

  const std::string repeated = collectNop(reused, emu);
  DebugState6502 fresh;
  check(repeated == collectNop(fresh, emu), "repeated collection retains no tracking state");

  DebugState6502 restarted;
  emu.setRegs(Emu6502::Regs{0x0300, 0, 0, 0, Emu6502::STATUS_IGNORED, 0xff});
  restarted.setModeCollect(&emu, 0);
  DebugState6502::debugStateCB(&restarted, &emu, 0x0300);
  emu.poke(0x0900, 0xea);
  DebugState6502::debugStateCB(&restarted, &emu, 0x0900);
  check(repeated == collectNop(restarted, emu), "restarting clears unsaved instruction tracking");
}

int main() {
  testTracking();
  testCollectionReset();
  return 0;
}
