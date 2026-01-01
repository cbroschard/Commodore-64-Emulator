# 🎮 Commodore 64 Emulator (C++ / SDL2)

A Commodore 64 emulator written in modern C++, built with **SDL2** and designed around two priorities:

- **Accuracy-first emulation** (with a strong focus on real hardware behavior over “good enough” hacks)
- **Debuggability** (integrated monitor, break/watchpoints, and visibility into what the machine is doing)

This project is actively under development; expect rough edges and incomplete features.

---

## Highlights

### 🧠 Emulation core
- **VIC‑II graphics**: text/bitmap modes, sprites, multicolor, raster IRQs, badlines (in progress toward tighter timing).
- **SID audio**: waveforms, filter routing, SYNC/RING modulation, and DAC/sample playback support (in progress/iterating).
- **PLA memory mapping** including cartridge line behavior (GAME/EXROM) and common mapper families.

### 💾 Media & peripherals
- **Cartridges**: `.CRT` with mapper detection (8K/16K, Ultimax, Ocean, MagicDesk, Dinamic, and more).
- **Disks**: disk image support is a major focus; IEC behavior and drive accuracy are being actively refined.
- **Tapes / programs**: boot loading support for common tape/program image formats.

> Note: Some formats/features may work better than others at any given time—this repo is very “work in progress.”

### 🛠️ Built-in monitor / debugger
- Breakpoints and watchpoints
- Disassembler and memory/register inspection/editing
- Designed to “break into the monitor” during execution for fast iteration while debugging emulation issues

---

## ✅ Current focus / roadmap (high level)

If you want to understand where most development energy goes, it’s roughly:

- **IEC / drive behavior correctness** (1541/1571/1581-style behavior, handshakes, timing, edge cases)
- **Cycle/timing accuracy** (raster, badlines, interrupt timing, bus contention)
- **SID correctness** (filter routing, modulation edge cases, sample playback quirks)
- **Usability** (cleaner UI flows for selecting media/device numbers, better monitor UX, controller mapping)

---

## 🔧 Requirements

- C++17 or newer compiler (GCC / Clang / MSVC / MinGW)
- SDL2
- Boost
- Dear ImGui

Third‑party licenses are listed in the **Licenses/** folder.

---

## 🧩 ROMs (important)

This emulator typically needs C64 ROM images (KERNAL/BASIC/CHAR).

Because ROMs are copyrighted, they are **not** included in this repo.

Provide your own ROM images and configure the emulator to find them (via your config/settings or the repo’s expected ROM path, depending on your setup).

---

## 🚀 Running

Run the emulator and use the UI/menu to load images (Cartridge, Disk, Tape, Program, etc.).

### Command line options

```text
  --help                Produce the help message
  --cartridge arg       Path and filename for cartridge to load on boot
  --tape arg            Path and filename for TAP or T64 tape image to load
  --program arg         Path and filename for PRG or P00 image to load
  --disk arg            Path and filename for disk image to load (D64, D71, D81, etc.)
  --version             Print version and exit
```

### Monitor examples

```text
monitor> cart load cart/Adventureland.crt
monitor> g
```

Debugging examples:

```text
monitor> d $C000 20    ; disassemble 20 bytes from $C000
monitor> bp $E000      ; set breakpoint at $E000
monitor> watch $D020   ; watch border color register
```

---

## 🧱 Building (practical notes)

Build steps depend on your environment. Common setups:

### Windows (MinGW / MSVC)
- Install SDL2 and ensure your compiler can find headers/libs
- Install Boost
- Build the project using your preferred IDE/toolchain

### Linux / macOS
- Install SDL2 and Boost from your package manager
- Build with your compiler/IDE toolchain

If your repo includes project files or a build system (e.g., IDE project files or CMake), follow those instructions.
If not, adding a simple CMake build is a great next step—happy to help wire that up cleanly.

---

## 📖 License

Copyright (c) 2025 Christopher Broschard.  
All rights reserved.

This source code is provided for personal, educational, and
non-commercial use only. Redistribution, modification, or use
in whole or in part for any other purpose is prohibited without
the prior written consent of the copyright holder.

Third-party dependencies are licensed separately:

- Boost – Boost Software License 1.0 (see `Licenses/BOOST_LICENSE_1_0.txt`)
- SDL2 – zlib License (see `Licenses/SDL2_zlib_LICENSE.txt`)
- Dear ImGui – MIT License (see `Licenses/MIT_LICENSE.txt`)

---

## 🙏 Acknowledgements

- C64 Programmer’s Reference Guide (hardware documentation)
- VICE emulator (reference behavior)
- The broader Commodore community for decades of reverse‑engineering work
