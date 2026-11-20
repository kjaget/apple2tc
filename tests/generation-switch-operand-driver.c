#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"

int main(void) {
  a2host_init_emulation();
  init_emulated();
  ram_poke(0x0901, 0x22);
  s_pc = 0x0900;
  run_emulated(1);
  printf("A=%02x\n", s_a);
  return 0;
}
