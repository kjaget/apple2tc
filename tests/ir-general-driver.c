#include "ir-general-generated.c"
#include "apple2tc/a2host_api.h"
#include <stdlib.h>

int main(int argc, char **argv) {
  a2host_init_emulation();
  run_emulated(3000);
  for (int i = 1; i < argc; ++i) {
    unsigned expected = (unsigned)strtoul(argv[i], NULL, 16);
    unsigned actual = ram_peek(0x0200 + i - 1);
    if (actual != expected) {
      fprintf(stderr, "$%04X: got $%02X, expected $%02X\n", 0x0200 + i - 1, actual, expected);
      return 1;
    }
  }
#ifdef TEST_INTERRUPT
  if (ram_peek16(0xfffe) != 0x0340 || get_ram()[0xffff] != 0x03) {
    fprintf(stderr, "incorrect initialized ROM vector\n");
    return 1;
  }
  if ((ram_peek(0x01fd) & 0x30) != 0x30 ||
      ram_peek(0x01fe) != 0x02 || ram_peek(0x01ff) != 0x03) {
    fprintf(stderr, "incorrect BRK stack frame\n");
    return 1;
  }
#endif
  shutdown_emulated();
  return 0;
}
