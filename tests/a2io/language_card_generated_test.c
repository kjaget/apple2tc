#define APPLE2TC_LANGUAGE_CARD
#include "apple2tc/system-inc.h"
#include "apple2tc/a2io.h"

#include <stdio.h>
#include <stdlib.h>

static a2_iostate_t io;

uint8_t io_peek(uint16_t addr) {
  return a2_io_peek(&io, addr, get_cycles());
}

void io_poke(uint16_t addr, uint8_t value) {
  a2_io_poke(&io, addr, value, get_cycles());
}

uint8_t io_peek_upper(uint16_t addr) {
  return a2_language_card_peek(&io.language_card, addr - 0xD000);
}

void io_refresh_main_ram_view(uint8_t *ram) {
  a2_language_card_sync_ram(&io.language_card, ram);
}

static void check(uint8_t actual, uint8_t expected, const char *description) {
  if (actual != expected) {
    fprintf(stderr, "%s: got $%02X, expected $%02X\n", description, actual, expected);
    exit(1);
  }
}

int main(void) {
  static uint8_t rom[0x3000];
  rom[0] = 0xA1;
  rom[0x2BB3] = 0xA2;
  a2_io_init(&io);
  a2_language_card_load_rom(&io.language_card, rom, sizeof(rom));
  reset_regs();

  check(peek(0xD000), 0xA1, "initial ROM bank");
  check(peek(0xFBB3), 0xA2, "initial upper ROM");

  peek(0xC083);
  peek(0xC083);
  poke(0xD000, 0x21);
  poke(0xFBB3, 0x22);
  check(peek(0xD000), 0x21, "bank 2 RAM");
  check(peek(0xFBB3), 0x22, "upper RAM");
  check(ram_peek(0xFBB3), 0x22, "modified operand read");

  peek(0xC08B);
  peek(0xC08B);
  poke(0xD000, 0x31);
  check(peek(0xD000), 0x31, "bank 1 RAM");
  check(peek(0xFBB3), 0x22, "shared upper RAM");

  peek(0xC080);
  check(peek(0xD000), 0x21, "bank 2 restored");
  poke(0xD000, 0x44);
  check(peek(0xD000), 0x21, "write protection");
  peek(0xC082);
  check(peek(0xD000), 0xA1, "ROM restored");
  check(peek(0xFBB3), 0xA2, "upper ROM restored");

  a2_io_done(&io);
  return 0;
}
