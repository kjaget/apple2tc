#!/bin/bash
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ] || { [ "$#" -eq 2 ] && [ "$2" != "--reproduce" ]; }; then
  echo "Usage: $0 <build-dir> [--reproduce]" >&2
  exit 2
fi

build=$(cd "$1" && pwd)
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -f "$tmp/switch.b33" "$tmp/preloaded.b33" "$tmp/states.b33" "$tmp/switch.ram" \
  "$tmp/switch.ram.regs" "$tmp/preloaded.ram" "$tmp/preloaded.ram.regs" \
  "$tmp/switch.json" "$tmp/preloaded.json" "$tmp/states.json" \
  "$tmp/switch-state0.asm" "$tmp/state0.asm" "$tmp/state1.asm" "$tmp/state2.asm" \
  "$tmp/state-error.json" \
  "$tmp/switch.log" "$tmp/preloaded.log" \
  "$tmp/generation-switch-generated.c" "$tmp/generate.log" "$tmp/driver.o" \
  "$tmp/generated-run" "$tmp/generated.out"; rmdir "$tmp"' EXIT

"$build/tools/a6502/a6502" generation-switch.s "$tmp/switch.b33"
"$build/tools/a6502/a6502" generation-preloaded.s "$tmp/preloaded.b33"
"$build/tools/a6502/a6502" generation-state.s "$tmp/states.b33"
for name in switch preloaded; do
  if ! "$build/tools/a2run/a2run" "$tmp/$name.b33" --collect --limit=20 --frames=150 \
      --snapshot-at=0380 --snapshot-out="$tmp/$name.ram" --out="$tmp/$name.json" \
      >"$tmp/$name.log" 2>&1; then
    cat "$tmp/$name.log" >&2
    exit 1
  fi
done

python3 - "$tmp" "$build/tools/apple2tc/apple2tc" <<'PY'
import json
import pathlib
import re
import subprocess
import sys
import tempfile

tmp = pathlib.Path(sys.argv[1])
apple2tc = pathlib.Path(sys.argv[2])

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

memory = bytearray(0x10000)
memory[0x900:0x903] = bytes(first[0x900][:3])
switch_states = []
for generation in switch["generations"]:
    for segment in generation["code"]:
        addr = segment["addr"]
        memory[addr:addr + len(segment["bytes"])] = bytes(segment["bytes"])
    switch_states.append(bytes(memory[0x900:0x903]))
assert switch_states == [
    bytes([0xa9, 0x11, 0x60]),
    bytes([0xa9, 0x11, 0x60]),
    bytes([0xa2, 0x22, 0x60]),
    bytes([0xa9, 0x11, 0x60]),
], "partial segments must retain unchanged instruction bytes"

preloaded = read_case("preloaded", [0x11, 0x22], 0x60)
first = {entry[0]: entry[1:] for entry in preloaded["FirstInstructions"]}
assert first[0x400][:3] == [0xa9, 0x11, 0x60]
assert first[0x402][0] == 0x60
assert first[0x403][0] == 0x60
assert any(data[:4] == [0xae, 0x10, 0x02, 0x60]
           for _, data in segments_at(preloaded, 0x400))
binary = (tmp / "preloaded.b33").read_bytes()
start = int.from_bytes(binary[:2], "little")
initial_code = binary[4 + 0x400 - start:4 + 0x403 - start]
assert initial_code == bytes([0xa9, 0x11, 0x60])
assert initial_code == bytes(first[0x400][:3])
memory = bytearray(0x10000)
memory[start:start + len(binary[4:])] = binary[4:]
assert memory[0x400:0x403] == initial_code
for generation in preloaded["generations"]:
    for segment in generation["code"]:
        addr = segment["addr"]
        memory[addr:addr + len(segment["bytes"])] = bytes(segment["bytes"])
assert memory[0x400:0x404] == bytes([0xae, 0x10, 0x02, 0x60])

with open("../decoded/robotron/robotron.json") as f:
    robotron = json.load(f)
robotron_versions = segments_at(robotron, 0x4c6e)
assert [data for _, data in robotron_versions] == [[0x60], [0xa5, 0xca]]
valid = subprocess.run(
    [str(apple2tc), "../decoded/robotron/robotron2084.b33", "--simple-c",
     "--run-data=../decoded/robotron/robotron.json"],
    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
assert valid.returncode == 0, valid.stderr

valid = subprocess.run(
    [str(apple2tc), str(tmp / "switch.b33"), "--simple-c",
     f"--run-data={tmp / 'switch.json'}"],
    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
assert valid.returncode == 0, valid.stderr

regs = switch["generations"][0]["regs"]
bad_cases = {
    "missing-code": ([{"regs": regs}], "Invalid generation"),
    "missing-address": ([{"regs": regs, "code": [{"bytes": [0xea]}]}],
                        "Invalid generation code segment"),
    "missing-bytes": ([{"regs": regs, "code": [{"addr": 0x900}]}],
                      "Invalid generation code segment"),
    "address-out-of-range": (
        [{"regs": regs, "code": [{"addr": 0x10000, "bytes": [0xea]}]}],
        "Invalid generation segment address"),
    "segment-crosses-address-space": (
        [{"regs": regs, "code": [{"addr": 0xffff, "bytes": [0xea, 0x60]}]}],
        "Generation segment exceeds address space"),
    "overlapping-segments": (
        [{"regs": regs, "code": [{"addr": 0x900, "bytes": [0xea, 0xea]},
                                 {"addr": 0x901, "bytes": [0x60]}]}],
        "Overlapping generation code segments"),
    "duplicate-segment": (
        [{"regs": regs, "code": [{"addr": 0x900, "bytes": [0xea]},
                                 {"addr": 0x900, "bytes": [0xea]}]}],
        "Overlapping generation code segments"),
    "invalid-byte": (
        [{"regs": regs, "code": [{"addr": 0x900, "bytes": [256]}]}],
        "Invalid generation segment byte"),
}
with tempfile.TemporaryDirectory(dir=tmp) as invalid_dir:
    invalid_dir = pathlib.Path(invalid_dir)
    for name, (generations, message) in bad_cases.items():
        recording = dict(switch)
        recording["generations"] = generations
        path = invalid_dir / f"{name}.json"
        path.write_text(json.dumps(recording))
        result = subprocess.run(
            [str(apple2tc), str(tmp / "switch.b33"), "--simple-c",
             f"--run-data={path}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        assert result.returncode != 0 and message in result.stderr, (name, result.stderr)

regs = dict(pc=0x300, a=0, x=0, y=0, status=0x20, sp=0xff)
states = {
    "BranchTargets": [0x402],
    "Branches": {"771": [0x402]},
    "FirstInstructions": [
        [0x300, 0x20, 0x00, 0x04, False],
        [0x303, 0x4c, 0x02, 0x04, False],
        [0x400, 0xa9, 0x11, 0x60, False],
        [0x402, 0x60, 0x00, 0x00, False],
    ],
    "generations": [
        {"regs": regs, "code": []},
        {"regs": regs, "code": [
            {"addr": 0x303, "bytes": [0x4c, 0x03, 0x04]},
            {"addr": 0x400, "bytes": [0xad, 0x10, 0x02, 0x60]},
        ]},
    ],
}
states_path = tmp / "states.json"
states_path.write_text(json.dumps(states))

def disassemble_state(index, data_path=states_path):
    output = tmp / f"state{index}.asm"
    with output.open("w") as stream:
        result = subprocess.run(
            [str(apple2tc), str(tmp / "states.b33"), "--asm",
             f"--run-data={data_path}", f"--generation-state={index}"],
            stdout=stream, stderr=subprocess.PIPE, text=True)
    return result, output.read_text()

state0_result, state0 = disassemble_state(0)
state1_result, state1 = disassemble_state(1)
assert state0_result.returncode == 0, state0_result.stderr
assert state1_result.returncode == 0, state1_result.stderr

def has_instruction(listing, addr, mnemonic, operand=None):
    for line in listing.splitlines():
        if not line.startswith(f"/*{addr:04X}*/"):
            continue
        if not re.search(rf"\s{mnemonic}\b", line):
            continue
        if operand is None or operand in line:
            return True
    return False

def expect_instruction(listing, addr, mnemonic, operand=None):
    if not has_instruction(listing, addr, mnemonic, operand):
        matching = [line for line in listing.splitlines()
                    if line.startswith(f"/*{addr:04X}*/")]
        raise AssertionError(
            f"missing ${addr:04X} {mnemonic} {operand or ''}; lines={matching}")

switch_state0_path = tmp / "switch-state0.asm"
with switch_state0_path.open("w") as stream:
    switch_state0_result = subprocess.run(
        [str(apple2tc), str(tmp / "switch.b33"), "--asm",
         f"--run-data={tmp / 'switch.json'}", "--generation-state=0"],
        stdout=stream, stderr=subprocess.PIPE, text=True)
switch_state0 = switch_state0_path.read_text()
assert switch_state0_result.returncode == 0, switch_state0_result.stderr
expect_instruction(switch_state0, 0x900, "LDA", "#$11")

expect_instruction(state0, 0x300, "JSR", "L_0400")
expect_instruction(state0, 0x303, "JMP", "L_0402")
expect_instruction(state0, 0x400, "LDA", "#$11")
expect_instruction(state0, 0x402, "RTS")
expect_instruction(state1, 0x300, "JSR", "L_0400")
expect_instruction(state1, 0x303, "JMP", "L_0403")
expect_instruction(state1, 0x400, "LDA", "M_0210")
expect_instruction(state1, 0x403, "RTS")
assert not has_instruction(state1, 0x402, "RTS"), [
    line for line in state1.splitlines() if line.startswith("/*0402*/")]

unavailable, _ = disassemble_state(2)
assert (unavailable.returncode != 0 and
        "--generation-state 2 is unavailable" in unavailable.stderr), \
    (unavailable.returncode, unavailable.stderr)

incomplete = dict(states)
del incomplete["FirstInstructions"]
incomplete_path = tmp / "state-error.json"
incomplete_path.write_text(json.dumps(incomplete))
missing_observations, _ = disassemble_state(0, incomplete_path)
assert missing_observations.returncode != 0 and \
    "requires non-empty FirstInstructions" in missing_observations.stderr

ambiguous = dict(states)
ambiguous["FirstInstructions"] = states["FirstInstructions"] + [
    [0x500, 0xad, 0x34, 0x12, False],
    [0x501, 0xea, 0x00, 0x00, False],
]
incomplete_path.write_text(json.dumps(ambiguous))
conflicting_observations, _ = disassemble_state(0, incomplete_path)
assert conflicting_observations.returncode != 0 and \
    "--generation-state 0 has conflicting first-observed bytes at $0501" in \
    conflicting_observations.stderr

upper_memory = dict(states)
upper_memory["generations"] = [dict(generation) for generation in states["generations"]]
upper_memory["generations"][1]["code"] = [
    {"addr": 0xd000, "bytes": [0xea]},
]
incomplete_path.write_text(json.dumps(upper_memory))
unsupported_upper_memory, _ = disassemble_state(1, incomplete_path)
assert unsupported_upper_memory.returncode != 0 and \
    "cannot materialize upper-memory segment at $D000" in unsupported_upper_memory.stderr

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
