#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static size_t find_whole_state_expecting(uint16_t address, uint8_t expected) {
  for (size_t state = 0; state < sizeof(s_generation_whole_state_units) /
                                     sizeof(s_generation_whole_state_units[0]); ++state) {
    size_t unit = s_generation_whole_state_units[state];
    for (size_t i = 0; i < sizeof(s_generation_validity_addresses) /
                                sizeof(s_generation_validity_addresses[0]); ++i) {
      const generation_validity_address_t *entry = &s_generation_validity_addresses[i];
      if (entry->address != address)
        continue;
      for (size_t j = 0; j < entry->count; ++j) {
        const generation_validity_constraint_t *constraint =
            &s_generation_validity_constraints[entry->first + j];
        if (constraint->unit == unit && constraint->expected == expected)
          return unit;
      }
    }
  }
  assert(false && "missing whole-state byte constraint");
  return 0;
}

int main(void) {
  a2host_init_emulation();
  init_emulated();

  size_t original = find_whole_state_expecting(0x0302, 0x60);
  assert(s_generation_validity_mismatches[original] == 0);

  ram_poke(0x0302, 0x04);
  assert(s_generation_validity_mismatches[original] == 1);
  puts("Overlapping opcode/operand byte remains tracked where it is static");
  return 0;
}
