#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"

int main(void) {
  a2host_init_emulation();
#ifdef APPLE2TC_GENERATION_AWARE
  init_emulated();
#endif
  s_pc = 0x0300;
  run_emulated(1000);
  printf("$0200=%02x $0201=%02x $0202=%02x\n",
         ram_peek(0x0200), ram_peek(0x0201), ram_peek(0x0202));
  return 0;
}
