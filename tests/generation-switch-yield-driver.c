#include "generation-switch-generated.c"
#include "apple2tc/a2host_api.h"
#include "apple2tc/probe.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 5)
    return 2;

  a2host_init_emulation();
  init_emulated();
  probe_load_script(argv[1]);
  probe_set_output_path(argv[2]);

  char regs_path[1024];
  if (snprintf(regs_path, sizeof(regs_path), "%s.regs", argv[3]) >=
      (int)sizeof(regs_path)) {
    fprintf(stderr, "Snapshot path is too long\n");
    return 2;
  }
  probe_snapshot_at(0x0380, argv[3], regs_path);

  g_debug = DebugASM;
  unsigned budget = (unsigned)strtoul(argv[4], NULL, 10);
  if (!budget) {
    fprintf(stderr, "Cycle budget must be positive\n");
    return 2;
  }
  for (unsigned calls = 0; calls != 20000 && probe_snapshot_missed(); ++calls)
    run_emulated(budget);

  if (probe_snapshot_missed()) {
    fprintf(stderr, "Generated program did not reach $0380\n");
    return 1;
  }
  probe_close_output();
  probe_report_unfired();
  return 0;
}
