#!/bin/bash
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ] || { [ "$#" -eq 2 ] && [ "$2" != "--reproduce" ]; }; then
  echo "Usage: $0 <build-dir> [--reproduce]" >&2
  exit 2
fi

build=$(cd "$1" && pwd)
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -f "$tmp/switch.b33" "$tmp/preloaded.b33" "$tmp/delayed.b33" \
  "$tmp/no-transition.b33" "$tmp/same-block.b33" "$tmp/states.b33" "$tmp/switch.ram" \
  "$tmp/stack-written-code.b33" "$tmp/stack-written-code.ram" \
  "$tmp/stack-written-code.ram.regs" "$tmp/stack-written-code.json" \
  "$tmp/ambiguous.b33" "$tmp/ambiguous.json" \
  "$tmp/switch.ram.regs" "$tmp/preloaded.ram" "$tmp/preloaded.ram.regs" \
  "$tmp/delayed.ram" "$tmp/delayed.ram.regs" "$tmp/no-transition.ram" \
  "$tmp/no-transition.ram.regs" "$tmp/same-block.ram" "$tmp/same-block.ram.regs" \
  "$tmp/switch.json" "$tmp/preloaded.json" "$tmp/delayed.json" \
  "$tmp/no-transition.json" "$tmp/same-block.json" "$tmp/states.json" \
  "$tmp/switch-state0.asm" "$tmp/state0.asm" "$tmp/state1.asm" "$tmp/state2.asm" \
  "$tmp/same-block-state0.asm" "$tmp/same-block-state1.asm" \
  "$tmp/stack-written-code.log" \
  "$tmp/state-error.json" \
  "$tmp/switch.log" "$tmp/preloaded.log" "$tmp/delayed.log" \
  "$tmp/no-transition.log" "$tmp/same-block.log" \
  "$tmp/generation-switch-generated.c" "$tmp/generate.log" "$tmp/driver.o" \
  "$tmp/generated-run" "$tmp/generated.out" "$tmp/generation-aware-generate.log" \
  "$tmp/generation-switch.probe" "$tmp/interpreter.trace" "$tmp/interpreter.log" \
  "$tmp/interpreter.probes" "$tmp/interpreter-snapshot.ram" \
  "$tmp/interpreter-snapshot.ram.regs" "$tmp/yield-one.trace" \
  "$tmp/yield-one.probes" "$tmp/yield-one.ram" "$tmp/yield-one.ram.regs" \
  "$tmp/yield-bulk.trace" "$tmp/yield-bulk.probes" "$tmp/yield-bulk.ram" \
  "$tmp/yield-bulk.ram.regs" "$tmp/yield-driver.o" "$tmp/yield-one-run" \
  "$tmp/yield-bulk-run" "$tmp/no-option.out" \
  "$tmp/same-block-timing.probe" "$tmp/same-block-interpreter.trace" \
  "$tmp/same-block-interpreter.log" "$tmp/same-block-interpreter.probes" \
  "$tmp/same-block-interpreter-snapshot.ram" \
  "$tmp/same-block-interpreter-snapshot.ram.regs" \
  "$tmp/same-block-yield-one.trace" "$tmp/same-block-yield-one.probes" \
  "$tmp/same-block-yield-one.ram" "$tmp/same-block-yield-one.ram.regs" \
  "$tmp/same-block-yield-bulk.trace" "$tmp/same-block-yield-bulk.probes" \
  "$tmp/same-block-yield-bulk.ram" "$tmp/same-block-yield-bulk.ram.regs" \
  "$tmp/generated-aware.out" "$tmp/same-block-aware.out" \
  "$tmp/same-block-generation-aware.log" "$tmp/unknown-generation-aware.log" \
  "$tmp/unknown-driver.o" "$tmp/unknown-run" "$tmp/delayed-aware.out" \
  "$tmp/preloaded-aware.out" "$tmp/delayed-generation-aware.log" \
  "$tmp/preloaded-generation-aware.log" "$tmp/operand-driver.o" \
  "$tmp/operand-run" "$tmp/operand-aware.out" \
  "$tmp/ambiguous-driver.o" "$tmp/ambiguous-pc-driver.o" \
  "$tmp/ambiguous-run" "$tmp/ambiguous-pc-run" \
  "$tmp/ambiguous-generation-aware.log" "$tmp/ambiguous-pc-generation-aware.log"; \
  rmdir "$tmp"' EXIT

"$build/tools/a6502/a6502" generation-switch.s "$tmp/switch.b33"
"$build/tools/a6502/a6502" generation-preloaded.s "$tmp/preloaded.b33"
"$build/tools/a6502/a6502" generation-switch-delayed.s "$tmp/delayed.b33"
"$build/tools/a6502/a6502" generation-switch-no-transition.s "$tmp/no-transition.b33"
"$build/tools/a6502/a6502" generation-same-block.s "$tmp/same-block.b33"
"$build/tools/a6502/a6502" generation-stack-written-code.s "$tmp/stack-written-code.b33"
"$build/tools/a6502/a6502" generation-state.s "$tmp/states.b33"
"$build/tools/a6502/a6502" generation-switch-ambiguous.s "$tmp/ambiguous.b33"
for name in switch preloaded delayed no-transition same-block; do
  if ! "$build/tools/a2run/a2run" "$tmp/$name.b33" --collect --limit=20 --frames=150 \
      --snapshot-at=0380 --snapshot-out="$tmp/$name.ram" --out="$tmp/$name.json" \
      >"$tmp/$name.log" 2>&1; then
    cat "$tmp/$name.log" >&2
    exit 1
  fi
done
if ! "$build/tools/a2run/a2run" "$tmp/stack-written-code.b33" --collect --limit=20 \
    --frames=150 --snapshot-at=0380 --snapshot-out="$tmp/stack-written-code.ram" \
    --out="$tmp/stack-written-code.json" >"$tmp/stack-written-code.log" 2>&1; then
  cat "$tmp/stack-written-code.log" >&2
  exit 1
fi

python3 - "$tmp" "$build/tools/apple2tc/apple2tc" <<'PY'
import json
import pathlib
import re
import subprocess
import sys
import tempfile

tmp = pathlib.Path(sys.argv[1])
apple2tc = pathlib.Path(sys.argv[2])

def read_case(name, expected_ram, expected_a, expected_x=0x22, expected_y=0,
              expected_sp=0xff):
    ram = (tmp / f"{name}.ram").read_bytes()
    assert len(ram) == 0x10000
    assert ram[0x200:0x200 + len(expected_ram)] == bytes(expected_ram), name
    fields = dict(line.split(maxsplit=1) for line in
                  (tmp / f"{name}.ram.regs").read_text().splitlines())
    for key, value in {"pc": 0x380, "a": expected_a, "x": expected_x, "y": expected_y,
                       "sp": expected_sp, "status": 0x20}.items():
        if value is not None:
            assert int(fields[key], 16) == value, (name, key, fields[key])
    return json.loads((tmp / f"{name}.json").read_text())

def segments_at(recording, addr):
    return [(index, segment["bytes"][addr - segment["addr"]:])
            for index, generation in enumerate(recording["generations"])
            for segment in generation["code"]
            if segment["addr"] <= addr < segment["addr"] + len(segment["bytes"])]

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

switch = read_case("switch", [0x11, 0x22, 0x11], 0x11)
switch_saved_pcs = [generation["regs"]["pc"] for generation in switch["generations"]]
assert switch_saved_pcs == [0x0300, 0x031e, 0x0330, 0x0380], switch_saved_pcs
assert switch["Branches"]["790"] == [0x900]
assert switch["Branches"]["808"] == [0x900]
assert switch["Branches"]["826"] == [0x900]
delayed = read_case("delayed", [0x11, 0x22, 0x11], 0x11, expected_y=1)
no_transition = read_case("no-transition", [0x11, 0x11], 0x11, 0xff)
same_block = read_case(
    "same-block", [0xa2], 0xa2, 0x11, expected_y=None, expected_sp=None)
stack_written_code = json.loads((tmp / "stack-written-code.json").read_text())
stack_ram = (tmp / "stack-written-code.ram").read_bytes()
assert stack_ram[0x13c:0x141] == bytes([0xa9, 0x11, 0x4c, 0x80, 0x03])
stack_regs = dict(line.split(maxsplit=1) for line in
                  (tmp / "stack-written-code.ram.regs").read_text().splitlines())
assert int(stack_regs["pc"], 16) == 0x380 and int(stack_regs["a"], 16) == 0x11
assert stack_written_code["Branches"]["789"] == [0x13c]
stack_first = {
    entry[0]: entry[1:] for entry in stack_written_code["FirstInstructions"]
}
assert stack_first[0x13c][:3] == [0xa9, 0x11, 0x4c]
assert not any(
    segment["addr"] <= 0x13c < segment["addr"] + len(segment["bytes"])
    for generation in stack_written_code["generations"]
    for segment in generation["code"]
), "implicit stack writes are not included in recorded generation segments"
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

delayed_versions = segments_at(delayed, 0x900)
assert [entry[1][:2] for entry in delayed_versions] == [
    [0xa9, 0x11], [0xa2, 0x22], [0xa9, 0x11]
]
delayed_saved_pcs = [generation["regs"]["pc"] for generation in delayed["generations"]]
assert delayed_saved_pcs[1] == 0x0323, delayed_saved_pcs
assert delayed_saved_pcs[2] == 0x033e, delayed_saved_pcs
delayed_branches = delayed["Branches"]
assert delayed_branches["790"] == [0x900]  # Initial JSR at $0316.
assert delayed_branches["822"] == [0x900]  # Replacement call at $0336.
assert delayed_branches["840"] == [0x900]  # Restoration call at $0348.

no_transition_versions = segments_at(no_transition, 0x900)
assert [entry[1][:3] for entry in no_transition_versions] == [[0xa9, 0x11, 0x60]]
assert no_transition["Branches"]["790"] == [0x900]
assert no_transition["Branches"]["796"] == [0x900]
no_transition_first = {
    entry[0]: entry[1:] for entry in no_transition["FirstInstructions"]
}
assert no_transition_first[0x900][:3] == [0xa9, 0x11, 0x60]

same_block_versions = segments_at(same_block, 0x305)
assert [entry[1][:2] for entry in same_block_versions] == [[0xa2, 0x11]]
same_block_state_paths = []
for index in range(2):
    output = tmp / f"same-block-state{index}.asm"
    with output.open("w") as stream:
        result = subprocess.run(
            [str(apple2tc), str(tmp / "same-block.b33"), "--asm",
             f"--run-data={tmp / 'same-block.json'}", f"--generation-state={index}"],
            stdout=stream, stderr=subprocess.PIPE, text=True)
    assert result.returncode == 0, result.stderr
    same_block_state_paths.append(output.read_text())
expect_instruction(same_block_state_paths[0], 0x302, "STA")
expect_instruction(same_block_state_paths[0], 0x305, "LDA", "#$11")
expect_instruction(same_block_state_paths[1], 0x305, "LDX", "#$11")
block_lines = [
    line for line in same_block_state_paths[0].splitlines()
    if line == "; Code range [$0300-$030C]"
]
assert len(block_lines) == 1, block_lines

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

ambiguous_regs = dict(pc=0x300, a=0, x=0, y=0, status=0x20, sp=0xff)
ambiguous_recording = {
    "BranchTargets": [],
    "Branches": {},
    "FirstInstructions": [
        [0x300, 0xa9, 0x00, 0x00, True],
        [0x302, 0x60, 0x00, 0x00, False],
        [0x400, 0x60, 0x00, 0x00, False],
        [0x500, 0x60, 0x00, 0x00, False],
    ],
    "generations": [
        {"regs": ambiguous_regs, "code": []},
        {"regs": ambiguous_regs, "code": [
            {"addr": 0x300, "bytes": [0x4c, 0x00, 0x04]},
        ]},
        {"regs": ambiguous_regs, "code": [
            {"addr": 0x300, "bytes": [0x4c, 0x00, 0x05]},
        ]},
    ],
}
(tmp / "ambiguous.json").write_text(json.dumps(ambiguous_recording))

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
    print("Default Simple C does not yet execute all recorded opcode versions")
PY
fi
"$build/tools/apple2tc/apple2tc" "$tmp/switch.b33" --generation-aware \
  --run-data="$tmp/switch.json" >"$tmp/generation-switch-generated.c" \
  2>"$tmp/generation-aware-generate.log"
grep -Fq '/* $030C LDA */ s_a = update_nz(0x11);' "$tmp/generation-switch-generated.c"
grep -Fq '/* $0900 LDA */ s_a = update_nz(peek(0x0901));' \
  "$tmp/generation-switch-generated.c"
python3 - "$tmp/generation-switch-generated.c" "$tmp/generation-switch.probe" <<'PY'
import pathlib
import re
import sys

generated = pathlib.Path(sys.argv[1]).read_text()
sites = sorted(set(int(value, 16) for value in
                   re.findall(r"CYCLES\(0x([0-9a-fA-F]+),", generated)))
assert 0x0380 in sites, sites
sites.remove(0x0380)
site_list = ", ".join(f"${site:04X}" for site in sites)
pathlib.Path(sys.argv[2]).write_text(
    'probe visit(site = pc) { printf("%04X\\n", site) }\n'
    'probe finish() { stop }\n'
    f"install visit at {site_list}\n"
    "install finish at $0380\n")
PY
"$build/tools/a2run/a2run" "$tmp/switch.b33" --from-rom --frames=150 \
  --trace --probe="$tmp/generation-switch.probe" --probe-out="$tmp/interpreter.probes" \
  --snapshot-at=0380 --snapshot-out="$tmp/interpreter-snapshot.ram" \
  >"$tmp/interpreter.trace" 2>"$tmp/interpreter.log"
cc -std=c11 -I../include -I"$tmp" -c generation-switch-driver.c -o "$tmp/driver.o"
c++ "$tmp/driver.o" "$build/lib/a2host/liba2host.a" "$build/lib/a2io/liba2io.a" \
  -pthread -lm -o "$tmp/generated-run"
"$tmp/generated-run" >"$tmp/generated-aware.out"
python3 - "$tmp" <<'PY'
import pathlib
import sys

tmp = pathlib.Path(sys.argv[1])
ram = (tmp / "switch.ram").read_bytes()
expected = " ".join(f"${addr:04x}={ram[addr]:02x}" for addr in range(0x200, 0x203))
actual = (tmp / "generated-aware.out").read_text().strip()
print(f"Interpreter: {expected}\nGeneration-aware: {actual}")
if actual != expected:
    raise SystemExit("FAIL: generation-aware code does not execute all recorded opcode versions")
PY
cc -std=c11 -I../include -I"$tmp" -c generation-switch-yield-driver.c \
  -o "$tmp/yield-driver.o"
c++ "$tmp/yield-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/yield-one-run"
"$tmp/yield-one-run" "$tmp/generation-switch.probe" "$tmp/yield-one.probes" \
  "$tmp/yield-one.ram" 1 >"$tmp/yield-one.trace"
c++ "$tmp/yield-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/yield-bulk-run"
"$tmp/yield-bulk-run" "$tmp/generation-switch.probe" "$tmp/yield-bulk.probes" \
  "$tmp/yield-bulk.ram" 1000 >"$tmp/yield-bulk.trace"
python3 - "$tmp" <<'PY'
import pathlib
import re
import sys

tmp = pathlib.Path(sys.argv[1])

def block_events(trace, block_pcs):
    events = []
    for line in trace.splitlines():
        match = re.match(r"\s*(\d+)\s+([0-9A-F]{4}):", line)
        if not match:
            continue
        cycles, pc = int(match.group(1)), int(match.group(2), 16)
        if pc in block_pcs:
            events.append((cycles, pc))
        if pc == 0x0380:
            break
    assert events and events[-1][1] == 0x0380, events
    return events

def probe_prefix(path):
    lines = path.read_text().splitlines()
    if "0380" in lines:
        lines = lines[:lines.index("0380") + 1]
    return lines

generated = (tmp / "generation-switch-generated.c").read_text()
cycle_costs = {}
for pc_text, cycles_text in re.findall(
        r"CYCLES\(0x([0-9a-fA-F]+),\s*(\d+)\)", generated):
    pc, cycles = int(pc_text, 16), int(cycles_text)
    if pc in cycle_costs and cycle_costs[pc] != cycles:
        raise SystemExit(f"FAIL: block ${pc:04X} has state-dependent cycle costs")
    cycle_costs[pc] = cycles
block_pcs = set(cycle_costs)
interpreter_events = block_events((tmp / "interpreter.trace").read_text(), block_pcs)
if not interpreter_events or interpreter_events[0][1] != 0x0300:
    raise SystemExit(f"FAIL: interpreter trace did not reach the program entry: "
                     f"{interpreter_events[:5]}")
interpreter_offset = interpreter_events[0][0]
normalized_interpreter = [(cycles - interpreter_offset, pc)
                          for cycles, pc in interpreter_events]
target_block_cycles = cycle_costs[0x0380]
for name in ("yield-one", "yield-bulk"):
    actual_events = block_events((tmp / f"{name}.trace").read_text(), block_pcs)
    actual_events = [
        (cycles - cycle_costs[pc], pc) for cycles, pc in actual_events
    ]
    generation_offset = interpreter_events[0][0] - actual_events[0][0]
    normalized_actual = [
        (cycles - actual_events[0][0], pc) for cycles, pc in actual_events
    ]
    if normalized_actual != normalized_interpreter:
        raise SystemExit(f"FAIL: {name} debug events differ at transition boundaries: "
                         f"{normalized_actual} != {normalized_interpreter}")
    if probe_prefix(tmp / f"{name}.probes") != probe_prefix(tmp / "interpreter.probes"):
        raise SystemExit(f"FAIL: {name} probe events differ from interpreter")
    generated_ram = (tmp / f"{name}.ram").read_bytes()
    interpreter_ram = (tmp / "interpreter-snapshot.ram").read_bytes()
    for start, end in ((0x0200, 0x0203), (0x0900, 0x0903)):
        if generated_ram[start:end] != interpreter_ram[start:end]:
            raise SystemExit(f"FAIL: {name} RAM differs at ${start:04X}-${end - 1:04X}")
    def fields(path):
        return dict(line.split(maxsplit=1) for line in path.read_text().splitlines())
    generated_regs = fields(tmp / f"{name}.ram.regs")
    interpreter_regs = fields(tmp / "interpreter-snapshot.ram.regs")
    for key in ("pc", "a", "x", "y", "sp", "status"):
        if generated_regs[key] != interpreter_regs[key]:
            raise SystemExit(f"FAIL: {name} register {key} differs from interpreter")
    if int(generated_regs["cycles"]) - target_block_cycles + generation_offset != \
            int(interpreter_regs["cycles"]):
        raise SystemExit(f"FAIL: {name} transition cycle total differs from interpreter")
print("Transition debug/probe events, cycles, and repeated-call snapshots match")
PY
"$build/tools/apple2tc/apple2tc" "$tmp/same-block.b33" --generation-aware \
  --run-data="$tmp/same-block.json" >"$tmp/generation-switch-generated.c" \
  2>"$tmp/same-block-generation-aware.log"
python3 - "$tmp/generation-switch-generated.c" "$tmp/same-block-timing.probe" <<'PY'
import pathlib
import re
import sys

generated = pathlib.Path(sys.argv[1]).read_text()
sites = sorted(set(int(value, 16) for value in
                   re.findall(r"CYCLES\(0x([0-9a-fA-F]+),", generated)))
assert 0x0380 in sites, sites
site_list = ", ".join(f"${site:04X}" for site in sites)
pathlib.Path(sys.argv[2]).write_text(
    'probe visit(site = pc) { printf("%04X\\n", site) }\n'
    'probe finish() { stop }\n'
    f"install visit at {site_list}\n"
    "install finish at $0380\n")
PY
"$build/tools/a2run/a2run" "$tmp/same-block.b33" --from-rom --frames=150 --trace \
  --probe="$tmp/same-block-timing.probe" \
  --probe-out="$tmp/same-block-interpreter.probes" \
  --snapshot-at=0380 --snapshot-out="$tmp/same-block-interpreter-snapshot.ram" \
  >"$tmp/same-block-interpreter.trace" 2>"$tmp/same-block-interpreter.log"
cc -std=c11 -I../include -I"$tmp" -c generation-switch-yield-driver.c \
  -o "$tmp/yield-driver.o"
c++ "$tmp/yield-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/yield-one-run"
"$tmp/yield-one-run" "$tmp/same-block-timing.probe" \
  "$tmp/same-block-yield-one.probes" "$tmp/same-block-yield-one.ram" 1 \
  >"$tmp/same-block-yield-one.trace"
c++ "$tmp/yield-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/yield-bulk-run"
"$tmp/yield-bulk-run" "$tmp/same-block-timing.probe" \
  "$tmp/same-block-yield-bulk.probes" "$tmp/same-block-yield-bulk.ram" 1000 \
  >"$tmp/same-block-yield-bulk.trace"
python3 - "$tmp" <<'PY'
import pathlib
import re
import sys

tmp = pathlib.Path(sys.argv[1])
generated = (tmp / "generation-switch-generated.c").read_text()
costs = {}
for pc_text, cycles_text in re.findall(
        r"CYCLES\(0x([0-9a-fA-F]+),\s*(\d+)\)", generated):
    pc, cycles = int(pc_text, 16), int(cycles_text)
    if pc in costs and costs[pc] != cycles:
        raise SystemExit(f"FAIL: same-block ${pc:04X} has differing state cycle costs")
    costs[pc] = cycles

def events(path):
    result = []
    for line in path.read_text().splitlines():
        match = re.match(r"\s*(\d+)\s+([0-9A-F]{4}):", line)
        if match:
            pc = int(match.group(2), 16)
            if pc in costs:
                result.append((int(match.group(1)), pc))
            if pc == 0x0380:
                break
    if not result or result[-1][1] != 0x0380:
        raise SystemExit(f"FAIL: missing same-block completion event: {result}")
    return result

interpreter_events = events(tmp / "same-block-interpreter.trace")
if interpreter_events[0][1] != 0x0300:
    raise SystemExit(f"FAIL: same-block interpreter missed entry event: {interpreter_events}")
interpreter_start = interpreter_events[0][0]
expected_events = [(cycles - interpreter_start, pc)
                   for cycles, pc in interpreter_events]
target_cycles = costs[0x0380]

def probes(path):
    lines = path.read_text().splitlines()
    return lines[:lines.index("0380") + 1] if "0380" in lines else lines

for name in ("same-block-yield-one", "same-block-yield-bulk"):
    observed = events(tmp / f"{name}.trace")
    observed = [(cycles - costs[pc], pc) for cycles, pc in observed]
    offset = interpreter_events[0][0] - observed[0][0]
    normalized = [(cycles - observed[0][0], pc) for cycles, pc in observed]
    if normalized != expected_events:
        raise SystemExit(f"FAIL: {name} repeated a block/debug event or changed timing")
    if probes(tmp / f"{name}.probes") != probes(tmp / "same-block-interpreter.probes"):
        raise SystemExit(f"FAIL: {name} repeated or omitted an intra-block transition probe")
    for start, end in ((0x0200, 0x0201), (0x0305, 0x0306)):
        if (tmp / f"{name}.ram").read_bytes()[start:end] != \
                (tmp / "same-block-interpreter-snapshot.ram").read_bytes()[start:end]:
            raise SystemExit(f"FAIL: {name} RAM differs at ${start:04X}")
    def fields(path):
        return dict(line.split(maxsplit=1) for line in path.read_text().splitlines())
    generated_regs = fields(tmp / f"{name}.ram.regs")
    interpreter_regs = fields(tmp / "same-block-interpreter-snapshot.ram.regs")
    for key in ("pc", "a", "x", "status"):
        if generated_regs[key] != interpreter_regs[key]:
            raise SystemExit(f"FAIL: {name} register {key} differs from interpreter")
    if int(generated_regs["cycles"]) - target_cycles + offset != \
            int(interpreter_regs["cycles"]):
        raise SystemExit(f"FAIL: {name} same-block transition cycles differ")
print("Intra-block transition debug/probe events and yield cycle accounting match")
PY
"$build/tools/apple2tc/apple2tc" "$tmp/switch.b33" --generation-aware \
  --run-data="$tmp/switch.json" >"$tmp/generation-switch-generated.c" \
  2>"$tmp/generation-aware-generate.log"
cc -std=c11 -I../include -I"$tmp" -c generation-switch-operand-driver.c \
  -o "$tmp/operand-driver.o"
c++ "$tmp/operand-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/operand-run"
"$tmp/operand-run" >"$tmp/operand-aware.out"
grep -q '^A=22$' "$tmp/operand-aware.out"

run_generation_aware_fixture() {
  local name=$1
  "$build/tools/apple2tc/apple2tc" "$tmp/$name.b33" --generation-aware \
    --run-data="$tmp/$name.json" >"$tmp/generation-switch-generated.c" \
    2>"$tmp/$name-generation-aware.log"
  cc -std=c11 -I../include -I"$tmp" -c generation-switch-driver.c -o "$tmp/driver.o"
  c++ "$tmp/driver.o" "$build/lib/a2host/liba2host.a" "$build/lib/a2io/liba2io.a" \
    -pthread -lm -o "$tmp/generated-run"
  "$tmp/generated-run" >"$tmp/$name-aware.out"
}
run_generation_aware_fixture delayed
run_generation_aware_fixture preloaded
python3 - "$tmp" <<'PY'
import pathlib
import sys

tmp = pathlib.Path(sys.argv[1])
for name in ("delayed", "preloaded"):
    ram = (tmp / f"{name}.ram").read_bytes()
    expected = " ".join(f"${addr:04x}={ram[addr]:02x}" for addr in range(0x200, 0x203))
    actual = (tmp / f"{name}-aware.out").read_text().strip()
    print(f"{name} interpreter: {expected}\n{name} generated:   {actual}")
    if actual != expected:
        raise SystemExit(f"FAIL: generation-aware {name} replay differs from interpreter")
PY

"$build/tools/apple2tc/apple2tc" "$tmp/same-block.b33" --generation-aware \
  --run-data="$tmp/same-block.json" >"$tmp/generation-switch-generated.c" \
  2>"$tmp/same-block-generation-aware.log"
cc -std=c11 -I../include -I"$tmp" -c generation-switch-driver.c -o "$tmp/driver.o"
c++ "$tmp/driver.o" "$build/lib/a2host/liba2host.a" "$build/lib/a2io/liba2io.a" \
  -pthread -lm -o "$tmp/generated-run"
"$tmp/generated-run" >"$tmp/same-block-aware.out"
python3 - "$tmp" <<'PY'
import pathlib
import sys

tmp = pathlib.Path(sys.argv[1])
ram = (tmp / "same-block.ram").read_bytes()
expected = " ".join(f"${addr:04x}={ram[addr]:02x}" for addr in range(0x200, 0x203))
actual = (tmp / "same-block-aware.out").read_text().strip()
print(f"Same-block interpreter: {expected}\nSame-block generated:   {actual}")
if actual != expected:
    raise SystemExit("FAIL: generation-aware code missed an intra-block transition")
PY

"$build/tools/apple2tc/apple2tc" "$tmp/switch.b33" --generation-aware \
  --run-data="$tmp/switch.json" >"$tmp/generation-switch-generated.c" \
  2>"$tmp/generation-aware-generate.log"
cc -std=c11 -I../include -I"$tmp" -c generation-switch-unknown-driver.c \
  -o "$tmp/unknown-driver.o"
c++ "$tmp/unknown-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/unknown-run"
if "$tmp/unknown-run" >"$tmp/unknown-generation-aware.log" 2>&1; then
  echo "Expected unknown generated opcode to fail closed" >&2
  exit 1
fi
grep -q 'Unknown generation instruction at \$0900' "$tmp/unknown-generation-aware.log"

"$build/tools/apple2tc/apple2tc" "$tmp/ambiguous.b33" --generation-aware \
  --run-data="$tmp/ambiguous.json" >"$tmp/generation-switch-generated.c" \
  2>"$tmp/generation-aware-generate.log"
cc -std=c11 -I../include -I"$tmp" -c generation-switch-ambiguous-driver.c \
  -o "$tmp/ambiguous-driver.o"
c++ "$tmp/ambiguous-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/ambiguous-run"
if "$tmp/ambiguous-run" >"$tmp/ambiguous-generation-aware.log" 2>&1; then
  echo "Expected overlapping generation signatures to fail closed" >&2
  exit 1
fi
grep -q 'Ambiguous generation state at \$0300' "$tmp/ambiguous-generation-aware.log"

cc -std=c11 -DAMBIGUOUS_PC=1 -I../include -I"$tmp" \
  -c generation-switch-ambiguous-driver.c -o "$tmp/ambiguous-pc-driver.o"
c++ "$tmp/ambiguous-pc-driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/ambiguous-pc-run"
if "$tmp/ambiguous-pc-run" >"$tmp/ambiguous-pc-generation-aware.log" 2>&1; then
  echo "Expected ambiguous current-PC instructions to fail closed" >&2
  exit 1
fi
grep -q 'Ambiguous generation instruction at \$0300' \
  "$tmp/ambiguous-pc-generation-aware.log"

"$build/tools/apple2tc/apple2tc" "$tmp/no-transition.b33" --simple-c \
  --run-data="$tmp/no-transition.json" >"$tmp/generation-switch-generated.c" \
  2>"$tmp/generate.log"
if grep -q 'APPLE2TC_GENERATION_AWARE' "$tmp/generation-switch-generated.c"; then
  echo "Ordinary Simple C unexpectedly enabled generation-aware dispatch" >&2
  exit 1
fi
cc -std=c11 -I../include -I"$tmp" -c generation-switch-driver.c -o "$tmp/driver.o"
c++ "$tmp/driver.o" "$build/lib/a2host/liba2host.a" \
  "$build/lib/a2io/liba2io.a" -pthread -lm -o "$tmp/generated-run"
"$tmp/generated-run" >"$tmp/no-option.out"
python3 - "$tmp" <<'PY'
import pathlib
import sys

tmp = pathlib.Path(sys.argv[1])
ram = (tmp / "no-transition.ram").read_bytes()
expected = " ".join(f"${addr:04x}={ram[addr]:02x}" for addr in range(0x200, 0x203))
actual = (tmp / "no-option.out").read_text().strip()
if actual != expected:
    raise SystemExit(f"FAIL: ordinary Simple C replay differs: {actual} != {expected}")
print(f"Ordinary Simple C: {actual}")
PY
