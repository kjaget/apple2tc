#include "language-card-ir-generated.c"
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
  check(io_peek_upper(0xD123), 0x71, "restored bank 1");
  check(io->language_card.bank2_ram[0x123], 0x72, "restored bank 2");
  check(io_peek_upper(0xF123), 0x73, "restored shared RAM");
  check(io->language_card.rom[0x1234], 0x75, "restored ROM");
  check(ram_peek(0x0207), 0x74, "restored main RAM");
  io_peek(0xC081);
  check(io->language_card.banked_memory_write_protect, 0, "restored switch latch");
#else
  run_emulated(3000);
#ifdef TEST_OBSERVED
  const uint8_t expected[] = {0x11, 0x22, 0x11, 0x11, 0x22, 0x45, 0x67};
#elif defined(TEST_INTERRUPT)
  const uint8_t expected[] = {0x11, 0x33, 0xea, 0x60, 0x55, 0x22};
#else
  const uint8_t expected[] = {0x22, 0x11, 0x22, a2host_io()->language_card.rom[0x100],
                              0x33, 0x33, 0x51, 0x52, 0x53};
#endif
  for (unsigned i = 0; i < sizeof(expected); ++i)
    check(ram_peek(0x0200 + i), expected[i], "generated result");
#ifdef TEST_INTERRUPT
  check(ram_peek(0x01fd) & 0x30, 0x30, "BRK stacked status");
#endif
#if !defined(TEST_OBSERVED) && !defined(TEST_INTERRUPT)
  check(get_ram()[0xD100], 0x33, "visible upper memory view");
  if (argc > 1) {
    FILE *snapshot = fopen(argv[1], "rb");
    if (!snapshot || fseek(snapshot, 0x0200, SEEK_SET) != 0) {
      perror("interpreter snapshot");
      exit(1);
    }
    for (unsigned i = 0; i < sizeof(expected); ++i) {
      int value = fgetc(snapshot);
      if (value == EOF) {
        fprintf(stderr, "interpreter snapshot is truncated\n");
        exit(1);
      }
      check(ram_peek(0x0200 + i), (unsigned)value, "interpreter comparison");
    }
    fclose(snapshot);
  }
#else
  (void)argc;
  (void)argv;
#endif
  shutdown_emulated();
#endif
#ifdef TEST_LC_RESTORE
  (void)argc;
  (void)argv;
#endif
  return 0;
}
