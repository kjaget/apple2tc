#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"

int main(void) {
  a2host_init_emulation();
  init_emulated();
  ram_poke(0x0900, 0x02);
  s_pc = 0x0900;
  run_emulated(100);
  return 0;
}
