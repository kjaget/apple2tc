/*
 * Copyright (c) Tzvetan Mikov.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/// \file
/// The **host** half of the Apple II host/engine contract: what the machine
/// around the CPU provides, and an engine calls.
///
/// The other half is `a2engine.h` — what an engine provides and the host calls.
/// `system.h` includes both.
///
/// The host owns everything that is not the CPU: the `a2_iostate_t` behind
/// `io_peek`/`io_poke`, the keyboard queue and its replay, the video memory
/// these read and write, frame timing, and the frame-hash oracle. An engine
/// executes 6502 and reaches the machine only through here.
///
/// These are called *from* the engine while the host is blocked inside
/// `run_emulated()`, so they may touch host state without synchronisation —
/// including when the engine runs on its own thread, as the generated C does.
///
/// Note this is the engine-facing surface only. `a2host_api.h` separately
/// declares what the host offers to a *program* embedding it.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Read the $C000-$CFFF soft-switch range.
uint8_t io_peek(uint16_t addr);
uint8_t io_inspect(uint16_t addr);
/// Write the $C000-$CFFF soft-switch range.
void io_poke(uint16_t addr, uint8_t value);

/// io_load_rom loads a ROM into the high address space of the machine.
void io_load_rom(const uint8_t *rom, uint16_t size);

/// Read the currently visible language-card byte without accessing a soft switch.
uint8_t io_peek_upper(uint16_t addr);

/// Restore the language-card storage and mapping captured at collection start.
/// The buffer contains bank 1, bank 2, high RAM and ROM, in that order.
void io_restore_language_card(
    const uint8_t *state, int read_rom, int bank, int write_protected, uint16_t prev_io_addr);

/// Refresh the main flat RAM view from switched RAM/ROM and banked RAM 
/// based on the settings of the language card soft switches.
void io_refresh_main_ram_view(uint8_t *ram);

/// Write bytes into the language-card area at $D000-$FFFF based on the
/// current read mapping (ROM or selected RAM bank).
void io_write_upper_visible(uint16_t addr, const uint8_t *data, size_t size);

/// Called at each basic block when `g_debug` asks for it. See `a2engine.h`.
void debug_asm(uint16_t pc);

/// The engine has reached something it cannot execute — an unknown address, an
/// unimplemented vector target. Reports and does not return.
void error_handler(uint16_t pc);

/// Queue `keys` to be typed into the machine, exactly as `--kbd-file` would
/// feed a file: fed a few at a time as the keyboard queue drains, `\n`
/// translated to Return, `\r` ignored.
///
/// For a program that cannot be used without typing something first. A
/// decompilation traced from the reset vector reproduces the whole boot, so
/// it comes up at whatever prompt the original did, and the recorded session's
/// first keystrokes were the command that started the program. Registering
/// those here makes the built artifact start on its own.
///
/// Call it from `init_emulated()`; `a2host_init_emulation()` looks for queued
/// keys immediately afterwards.
///
/// `--key-file` and `--kbd-file` both take precedence, so replaying a
/// recording is unaffected — the recording already contains those keystrokes,
/// and typing them twice would desynchronise it.
///
/// The string is not copied. Pass a literal or something that outlives the run.
void a2host_buffer_keys(const char *keys);

#ifdef __cplusplus
} // extern "C"
#endif
