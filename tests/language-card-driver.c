#include "language-card-generated.c"
#include "apple2tc/a2host_api.h"
#include <stdlib.h>

static void check(unsigned actual, unsigned expected, const char *description) {
  if (actual != expected) {
    fprintf(stderr, "%s: got $%X, expected $%X\n", description, actual, expected);
    exit(1);
  }
}

int main(int argc, char **argv) {
  a2host_init_emulation();
#ifdef TEST_LC_RESTORE
  const a2_iostate_t *io = a2host_io();
  check(io->language_card.banked_memory_read_rom, 0, "restored RAM read mapping");
  check(io->language_card.banked_memory_ram_select, A2_LC_RAM1, "restored bank selection");
  check(io->language_card.banked_memory_write_protect, 1, "restored write protection");
  check(io->prev_io_addr, 0xC081, "restored switch latch");
  check(io_peek_upper(0xD123), 0x71, "restored bank 1 contents");
  check(io->language_card.bank2_ram[0x123], 0x72, "restored bank 2 contents");
  check(io_peek_upper(0xF123), 0x73, "restored shared RAM contents");
  check(io->language_card.rom[0x1234], 0x75, "restored ROM contents");
  check(ram_peek(0x0207), 0x74, "restored starting main RAM");
  io_peek(0xC081);
  check(io->language_card.banked_memory_write_protect, 0, "restored latch enables writes");
#endif
  if (argc > 1) {
    if (strcmp(argv[1], "changed-main-opcode") == 0) {
      poke(0x0300, 0xFF);
    } else if (strcmp(argv[1], "observed-operands") == 0) {
      s_pc = 0x0300;
      s_sp = 0xFF;
      run_emulated(1000);
      check(ram_peek(0x0200), 0x11, "initial low operand");
      check(ram_peek(0x0201), 0x22, "indirectly modified low operand");
      check(ram_peek(0x0202), 0x11, "restored low operand");
      check(ram_peek(0x0203), 0x11, "bank 2 operand");
      check(ram_peek(0x0204), 0x22, "bank 1 operand");
      check(ram_peek(0x0205), 0x45, "initial absolute operand");
      check(ram_peek(0x0206), 0x67, "modified high operand byte");
      return 0;
    } else if (strcmp(argv[1], "first-main-opcode") == 0) {
      ram_poke(0x0301, 0x57);
      s_x = 0;
      s_pc = 0x0300;
      run_emulated(2);
      check(s_x, 0x41, "first observed lower-memory opcode and operand");
      check(s_pc, 0x0302, "first observed lower-memory instruction size");
      return 0;
    } else if (strcmp(argv[1], "cross-region-operand") == 0) {
      io_peek(0xC083);
      io_peek(0xC083);
      poke(0xD101, 0x01);
      poke(0xD102, 0x03);
      s_a = 0x80;
      s_sp = 0xFF;
      push16(0x4566);
      s_pc = 0xD100;
      run_emulated(10);
      check(ram_peek(0x0301), 0x80, "upper code changed lower operand");
      poke(0x0300, 0xFF);
      s_pc = 0x0300;
      s_remaining_cycles = 0;
      run_emulated(4);
      check(a2host_io()->prev_io_addr, 0xC080, "lower instruction uses changed operand");
      return 0;
    } else if (strcmp(argv[1], "branch-entry") == 0) {
      io_peek(0xC083);
      io_peek(0xC083);
      poke(0xD003, 0x08);
      poke(0xD004, 0x02);
      s_a = 0x79;
      s_sp = 0xFF;
      push16(0x4566);
      s_pc = 0xD002;
      run_emulated(10);
      check(ram_peek(0x0208), 0x79, "recorded branch into upper block");
      check(s_pc, 0x4567, "upper block return");
      return 0;
    } else if (strcmp(argv[1], "conditional-branch") == 0 ||
               strcmp(argv[1], "changed-branch") == 0) {
      bool changed = strcmp(argv[1], "changed-branch") == 0;
      io_peek(0xC083);
      io_peek(0xC083);
      poke(0xD101, changed ? 4 : 2);
      s_pc = 0xD100;
      s_status &= ~STATUS_Z;
      run_emulated(2);
      check(s_pc, changed ? 0xD106 : 0xD104, "taken upper-memory branch target");
      s_remaining_cycles = 0;
      s_pc = 0xD100;
      s_status |= STATUS_Z;
      run_emulated(2);
      check(s_pc, 0xD102, "untaken upper-memory branch fallthrough");
      return 0;
    } else if (strcmp(argv[1], "wrap") == 0) {
      ram_poke(0, 0x42);
      s_pc = 0xFFFF;
      run_emulated(2);
      check(s_a, 0x42, "operand crossing the address-space boundary");
      check(s_pc, 1, "upper instruction wraps into lower memory");
      return 0;
    } else {
      s_pc = 0xD100;
      run_emulated(100);
      fprintf(stderr, "Unrecorded upper-memory code was accepted\n");
      return 1;
    }
  }
  run_emulated(1000);
  check(ram_peek(0x0200), 0x11, "bank 2 routine");
  check(ram_peek(0x0201), 0x33, "changed operand in bank 1 routine");
  check(ram_peek(0x0205), 0x22, "bank 1 routine");
  check(ram_peek(0x0202), 0xEA, "ROM remains readable while writing RAM");
  check(ram_peek(0x0203), 0x60, "RAM becomes readable after switch");
  check(ram_peek(0x0204), 0x55, "BRK uses the selected RAM interrupt vector");

  poke(0xD000, 0xFF);
  s_pc = 0xD000;
  s_remaining_cycles = 0;
  s_a = 0;
  unsigned before = get_cycles();
  run_emulated(2);
  check(s_a, 0x33, "first recorded opcode with changed upper-memory opcode");
  check(get_cycles() - before, 12, "grouped upper block cycle cost");

  io_peek(0xC082);
  io_peek(0xC081);
  s_sp = 0xFF;
  push16(0x4566);
  s_pc = 0xFBB3;
  s_remaining_cycles = 0;
  run_emulated(6);
  check(s_pc, 0x4567, "first recorded opcode with ROM visible");
  check(a2host_io()->prev_io_addr, 0xC081, "upper opcode does not perform an I/O read");
  return 0;
}
