#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"

#ifdef CHECK_GENERATION_VALIDITY
static void assert_generation_validity_matches_ram(void) {
  for (size_t unit = 0; unit < sizeof(s_generation_validity_mismatches) /
                                    sizeof(s_generation_validity_mismatches[0]); ++unit) {
    size_t expected_mismatches = 0;
    for (size_t i = 0; i < sizeof(s_generation_validity_addresses) /
                                sizeof(s_generation_validity_addresses[0]); ++i) {
      const generation_validity_address_t *address = &s_generation_validity_addresses[i];
      for (size_t j = 0; j < address->count; ++j) {
        const generation_validity_constraint_t *constraint =
            &s_generation_validity_constraints[address->first + j];
        if (constraint->unit == unit && s_ram[address->address] != constraint->expected)
          ++expected_mismatches;
      }
    }
    assert(s_generation_validity_mismatches[unit] == expected_mismatches);
  }
}
#endif

int main(void) {
  a2host_init_emulation();
#ifdef APPLE2TC_GENERATION_AWARE
  init_emulated();
#endif
#ifndef PRESERVE_START_PC
  s_pc = 0x0300;
#endif
  run_emulated(1000);
#ifdef CHECK_GENERATION_VALIDITY
  assert_generation_validity_matches_ram();
#endif
  printf("$0200=%02x $0201=%02x $0202=%02x\n",
         ram_peek(0x0200), ram_peek(0x0201), ram_peek(0x0202));
  return 0;
}
