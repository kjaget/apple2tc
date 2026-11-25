#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"

int main(void) {
  a2host_init_emulation();
  init_emulated();
  ram_poke(0x0300, 0x4c);
  ram_poke(0x0301, 0x00);
  ram_poke(0x0302, 0x04);
#ifdef AMBIGUOUS_PC
  ram_poke(0x0400, 0x00);
  ram_poke(0x0500, 0x00);
#endif
  s_pc = 0x0300;
  run_emulated(100);
  return 0;
}
