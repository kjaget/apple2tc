#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void assert_validity_matches_ram(void) {
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

int main(void) {
  a2host_init_emulation();
  init_emulated();

  assert(s_generation_validity_initialized);
  assert_validity_matches_ram();
  assert(!s_generation_dirty);
  size_t whole_initial = s_generation_whole_state_units[0];
  size_t whole_replaced = s_generation_whole_state_units[1];
  assert(s_generation_validity_mismatches[whole_initial] == 0);
  assert(s_generation_validity_mismatches[whole_replaced] == 1);

  ram_poke(0x0200, 0x55);
  assert(!s_generation_dirty);
  assert_validity_matches_ram();
  ram_poke(0x0901, 0x33);
  assert(!s_generation_dirty);
  assert(s_generation_validity_mismatches[whole_initial] == 0);
  assert(s_generation_validity_mismatches[whole_replaced] == 1);

  uint16_t computed_address = 0x0900;
  ram_poke(computed_address, 0xa2);
  assert(s_generation_dirty);
  assert(s_generation_validity_mismatches[whole_initial] == 1);
  assert(s_generation_validity_mismatches[whole_replaced] == 0);
  assert_validity_matches_ram();
  ram_poke(computed_address, 0xa2);
  assert_validity_matches_ram();
  ram_poke(computed_address, 0xa9);
  assert(s_generation_validity_mismatches[whole_initial] == 0);
  assert(s_generation_validity_mismatches[whole_replaced] == 1);
  assert_validity_matches_ram();

  set_regs(get_regs());
  s_sp = 0xff;
  push8(0x42);
  assert_validity_matches_ram();

  init_emulated();
  run_emulated(1000);
  assert_validity_matches_ram();
  assert(s_generation_validity_mismatches[whole_initial] == 0);
  assert(s_generation_validity_mismatches[whole_replaced] == 1);

  reset_regs();
  assert_validity_matches_ram();
  init_emulated();
  assert_validity_matches_ram();
  puts("Generation validity metadata updates and reinitializes correctly");
  return 0;
}
