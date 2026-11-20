#!/bin/bash
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ] || { [ "$#" -eq 2 ] && [ "$2" != "--reproduce" ]; }; then
  echo "Usage: $0 <build-dir> [--reproduce]" >&2
  exit 2
fi

build=$(cd "$1" && pwd)
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -f "$tmp/switch.b33" "$tmp/preloaded.b33" "$tmp/switch.ram" \
  "$tmp/switch.ram.regs" "$tmp/preloaded.ram" "$tmp/preloaded.ram.regs" \
  "$tmp/switch.json" "$tmp/preloaded.json" "$tmp/switch.log" "$tmp/preloaded.log" \
  "$tmp/generation-switch-generated.c" "$tmp/generate.log" "$tmp/driver.o" \
  "$tmp/generated-run" "$tmp/generated.out"; rmdir "$tmp"' EXIT

"$build/tools/a6502/a6502" generation-switch.s "$tmp/switch.b33"
"$build/tools/a6502/a6502" generation-preloaded.s "$tmp/preloaded.b33"
for name in switch preloaded; do
  if ! "$build/tools/a2run/a2run" "$tmp/$name.b33" --collect --limit=20 --frames=150 \
      --snapshot-at=0380 --snapshot-out="$tmp/$name.ram" --out="$tmp/$name.json" \
      >"$tmp/$name.log" 2>&1; then
    cat "$tmp/$name.log" >&2
    exit 1
  fi
done

python3 - "$tmp" <<'PY'
import json
import pathlib
import sys

tmp = pathlib.Path(sys.argv[1])

def read_case(name, expected_ram, expected_a):
    ram = (tmp / f"{name}.ram").read_bytes()
    assert len(ram) == 0x10000
    assert ram[0x200:0x200 + len(expected_ram)] == bytes(expected_ram), name
    fields = dict(line.split(maxsplit=1) for line in
                  (tmp / f"{name}.ram.regs").read_text().splitlines())
    for key, value in {"pc": 0x380, "a": expected_a, "x": 0x22, "y": 0,
                       "sp": 0xff, "status": 0x20}.items():
        assert int(fields[key], 16) == value, (name, key, fields[key])
    return json.loads((tmp / f"{name}.json").read_text())

def segments_at(recording, addr):
    return [(index, segment["bytes"][addr - segment["addr"]:])
            for index, generation in enumerate(recording["generations"])
            for segment in generation["code"]
            if segment["addr"] <= addr < segment["addr"] + len(segment["bytes"])]

switch = read_case("switch", [0x11, 0x22, 0x11], 0x11)
versions = segments_at(switch, 0x900)
assert [entry[1][:2] for entry in versions] == [[0xa9, 0x11], [0xa2, 0x22], [0xa9, 0x11]]
assert versions[0][1][:3] == [0xa9, 0x11, 0x60]
first = {entry[0]: entry[1:] for entry in switch["FirstInstructions"]}
assert first[0x900] == [0xa9, 0x11, 0x60, True]
assert first[0x902][0] == 0x60

preloaded = read_case("preloaded", [0x11, 0x22], 0x60)
first = {entry[0]: entry[1:] for entry in preloaded["FirstInstructions"]}
assert first[0x400][:3] == [0xa9, 0x11, 0x60]
assert first[0x402][0] == 0x60
assert first[0x403][0] == 0x60
assert any(data[:4] == [0xae, 0x10, 0x02, 0x60]
           for _, data in segments_at(preloaded, 0x400))
binary = (tmp / "preloaded.b33").read_bytes()
start = int.from_bytes(binary[:2], "little")
assert binary[4 + 0x400 - start:4 + 0x403 - start] == bytes([0xa9, 0x11, 0x60])

with open("../decoded/robotron/robotron.json") as f:
    robotron = json.load(f)
assert [data[:2] for _, data in segments_at(robotron, 0x4c6e)] == [[0x60], [0xa5, 0xca]]
print("Generation interpreter and recording checks passed")
PY

if [ "${2:-}" = "--reproduce" ]; then
  "$build/tools/apple2tc/apple2tc" "$tmp/switch.b33" --simple-c \
    --run-data="$tmp/switch.json" >"$tmp/generation-switch-generated.c" 2>"$tmp/generate.log"
  cc -std=c11 -I../include -I"$tmp" -c generation-switch-driver.c -o "$tmp/driver.o"
  c++ "$tmp/driver.o" "$build/lib/a2host/liba2host.a" "$build/lib/a2io/liba2io.a" \
    -pthread -lm -o "$tmp/generated-run"
  "$tmp/generated-run" >"$tmp/generated.out"
  python3 - "$tmp" <<'PY'
import pathlib
import sys

tmp = pathlib.Path(sys.argv[1])
ram = (tmp / "switch.ram").read_bytes()
expected = " ".join(f"${addr:04x}={ram[addr]:02x}" for addr in range(0x200, 0x203))
actual = (tmp / "generated.out").read_text().strip()
print(f"Interpreter: {expected}\nGenerated:   {actual}")
if actual != expected:
    raise SystemExit("FAIL: generated code does not execute all recorded opcode versions")
PY
fi
