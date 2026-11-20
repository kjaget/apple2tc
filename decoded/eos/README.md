# Earth Orbit Stations

This directory contains a work-in-progress decompilation in `eos.c`.

## Build

From the repository root, configure a Ninja build directory (once) and build
the windowed executable:

```shell
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
ninja -C build eos
```

Re-run the `ninja` command after changing `eos.c`. The build needs CMake, Ninja,
a C11/C++17 compiler and the platform dependencies listed in the
[project README](../../README.md#building).

## Generate the C output

Build the decompiler with `ninja -C build apple2tc`, then run this from the
`build` directory:

```shell
tools/apple2tc/apple2tc --simple-c \
  --code-at=eos_code_at.txt --run-data=eos.json \
  --language-card \
  /home/ubuntu/eos/trace_3_no_boring_gen2_gen01_block01_0000_0001.b33 > eos.c
```

and 

```shell
tools/apple2tc/apple2tc --irc1 -O1 \
  --code-at=eos_code_at.txt --run-data=eos.json \
  --language-card \
  /home/ubuntu/eos/trace_3_no_boring_gen2_gen01_block01_0000_0001.b33 > eos.c
```

This uses `eos_code_at.txt` and `eos.json` in `build` and an external `.b33`
input; adjust the paths as needed. It writes `build/eos.c`, not the source in
this directory. Review the output before replacing `decoded/eos/eos.c`, which
may contain manual changes.

## Run

From the `build` directory:

```shell
decoded/eos/eos \
  --disk1=/home/ubuntu/eos/EOS_Crack/d1_boot.dsk \
  --disk2=/home/ubuntu/eos/EOS_Crack/d2_archive.dsk \
  --disk2-alt=/home/ubuntu/eos/EOS_Crack/d3_mission.dsk
```

Replace the paths with the locations of your disk images. The program opens a
window; drive 1 starts with the boot disk and drive 2 with the archive disk.
Press F9 to swap drive 2 between the archive and mission disks.


### Experimental language-card support

Both C backends use language-card ROM/RAM mapping. Without `--language-card`,
the initial upper-memory image becomes ROM and card RAM starts at its power-on
value; disassembly does not account for different code in the card banks.
Older generated C that copies ROM into `s_ram` must be regenerated or call
`io_load_rom(s_ram + 0xd000, 0x3000)` after initialization.
To generate code using recorded card state and upper-memory instructions, first
make a new recording with the rebuilt `a2run`. From `build`:

```shell
tools/a2run/a2run --collect --from-rom --preload --frames=5000 --out=eos.json \
  --key-file=eos.keys \
  --disk1=/home/ubuntu/eos/EOS_Crack/d1_boot.dsk \
  --disk2=/home/ubuntu/eos/EOS_Crack/d2_archive.dsk \
  --disk2-alt=/home/ubuntu/eos/EOS_Crack/d3_mission.dsk \
  /home/ubuntu/eos/trace_3_no_boring_gen2_gen01_block01_0000_0001.b33

tools/apple2tc/apple2tc --simple-c --language-card \
  --code-at=eos_code_at.txt --run-data=eos.json \
  /home/ubuntu/eos/trace_3_no_boring_gen2_gen01_block01_0000_0001.b33 > eos.c

tools/apple2tc/apple2tc --irc1 -O1 --language-card \
  --code-at=eos_code_at.txt --run-data=eos.json \
  /home/ubuntu/eos/trace_3_no_boring_gen2_gen01_block01_0000_0001.b33 > eos-irc1.c
```

For optimized IR-based C instead, use:

```shell
tools/apple2tc/apple2tc --irc1 -O3 --ret-addr --language-card \
  --code-at=eos_code_at.txt --run-data=eos.json \
  /home/ubuntu/eos/trace_3_no_boring_gen2_gen01_block01_0000_0001.b33 > eos-irc1-lc.c
```

`--ret-addr` matters for EOS: the disk boot code examines a return address on
the emulated stack to determine its slot. Without this option, recovered IR
routines may use a dummy return address and boot from the wrong slot. The
generated file is in `build`; review it before replacing the source used by
the `eos` target.

Use the same input binary and any keyboard replay for recording and execution.
When a recording is provided, it must include the startup state and
first-observed instructions. Older recordings must be recollected. Without a
recording, the generator assumes power-on state and uses the supplied or
built-in ROM; it cannot discover code loaded into language-card RAM later.
Both C backends support this mode, and a recording must cover each upper-memory
instruction address that will execute. The first recorded opcode at each
address is hard-coded; later opcode changes are not supported or detected.
Record with the current interpreter to capture control-transfer destinations,
including RTI continuations, needed as upper-memory block entries. Current
recordings also capture the first instruction bytes at every executed PC and
whether an operand was observed to change. Generated operands are read
dynamically only when static analysis detects a write to their bytes or the
recording observes a different operand on a later execution. Both C backends
use these observations even without `--language-card`. Recordings made
before operand-change tracking should be recollected; they cannot detect changes
missed by static analysis. Indirect writes or bank changes not exercised during
collection can still be missed. Other device state is not restored from the
recording, so collect from startup.

EOS remains a work in progress: in the 5,000-frame no-input recording above,
the opcode at `$A806` changes during execution. This is not yet supported,
and the first 1,500 frames also have some video-hash differences from the
interpreter (78 with Simple C and 72 with IR-C1 in one no-input comparison).
Passing the earlier `$FA59` failure is not a full verification.
