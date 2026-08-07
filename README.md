# 🎮 Commodore 64 Emulator

A Commodore 64 emulator written in modern C++ using **SDL3** and **Dear ImGui**.

The project is built around two primary goals:

- **Hardware-focused accuracy** — model real Commodore 64 behavior, timing, bus activity, and device interactions rather than relying on compatibility hacks.
- **Debuggability** — provide an integrated machine-language monitor with detailed visibility into the CPU, VIC-II, SID, CIAs, memory, cartridges, drives, and system bus.

This emulator is under active development. Many programs and games are playable, but timing-sensitive software, uncommon hardware configurations, and newly implemented features may still expose bugs.

---

## ✨ Highlights

### 🧠 Emulation core

- MOS 6510 CPU emulation
- Instruction-level and micro-operation execution support
- IRQ, NMI, RDY, AEC, and bus-arbitration handling
- PLA-based memory decoding
- Open-bus and data-bus-latch behavior
- PAL and NTSC machine modes
- Warm and cold reset support
- Save-state and load-state support

### 🖥️ VIC-II graphics

- Character and bitmap display modes
- Standard and multicolor graphics
- Extended color mode
- Hardware sprites
- Sprite expansion and multicolor support
- Sprite/background and sprite/sprite collision handling
- Raster interrupts
- Badline processing
- Border and display-window behavior
- VIC-II and CPU bus arbitration
- Ongoing work toward tighter cycle and pixel accuracy

### 🔊 SID audio

- MOS 6581 and MOS 8580 model selection
- Three SID voices
- Triangle, sawtooth, pulse, and noise waveforms
- ADSR envelopes
- Pulse-width control
- Oscillator synchronization
- Ring modulation
- Filter routing
- Volume and DAC-based sample playback
- PAL and NTSC timing support

SID behavior is still being refined, particularly filters, waveform interactions, envelope edge cases, and sample playback.

### ⏱️ CIA and peripheral timing

- CIA 1 and CIA 2 register emulation
- Timers A and B
- Time-of-day clocks
- Interrupt control
- Keyboard matrix and joystick ports
- VIC-II bank selection
- IEC-related CIA behavior
- Serial-port state inspection through the monitor

---

## 💾 Media and peripherals

### Cartridges

Supports `.CRT` cartridge images with automatic mapper selection.

Implemented cartridge families include:

- Generic 8K, 16K, and Ultimax cartridges
- Action Replay variants
- Final Cartridge variants
- KCS Power Cartridge
- Ocean
- Magic Desk
- EasyFlash
- Retro Replay
- Super Snapshot
- Epyx FastLoad
- Super Zaxxon
- IDE64
- GMOD2
- C64 Game System
- Simons' BASIC
- Expert Cartridge
- COMAL 80
- Structured BASIC
- Warp Speed
- Dinamic
- REX Utility
- Ross
- Westermann
- Mikro Assembler
- Freeze Frame variants
- Additional mapper types

Some cartridge hardware provides configurable switches, buttons, freezer functions, storage devices, or bank-selection behavior through the emulator UI and monitor.

### Disk drives

Supported drive models:

- Commodore 1541
- Commodore 1571
- Commodore 1581

Supported disk-image formats include:

- `.D64`
- `.D71`
- `.D81`

The emulator includes IEC bus communication and drive-device integration. Drive timing and compatibility continue to be refined.

### Tape and program images

Supported media includes:

- `.TAP`
- `.T64`
- `.PRG`
- `.P00`

Cassette controls include:

- Play
- Stop
- Rewind
- Eject

Programs may be loaded independently or while retaining an attached cartridge.

### Additional hardware

- REU memory expansion support
- IDE64 storage-image support
- Multiple joystick ports
- SDL3 gamepad support
- Keyboard-configurable joystick mappings via the configuration file

---

## 🛠️ Integrated monitor and debugger

The built-in machine-language monitor is intended for emulator development, software debugging, and direct inspection of the emulated machine.

Features include:

- CPU register inspection
- Memory viewing and editing
- Disassembly
- Assembly support
- Breakpoints
- Read, write, and read/write watchpoints
- Execution stepping
- Step-over support
- CPU execution and micro-operation state
- Bus-arbitration inspection
- Cartridge mapping and bank inspection
- PLA decoding
- VIC-II state inspection
- SID state inspection
- CIA register, timer, port, TOD, interrupt, and serial inspection
- Stack inspection
- Trace controls
- Asynchronous break notifications

Example commands:

```text
> d $C000 20
> bp $E000
> watch $D020
> cpu regs
> cpu busarb
> vic all
> cia 1 all
> cia 2 timers
> sid
> cart info
> pla
> g
```

Commands and syntax may change as the monitor continues to evolve.

---

## 🖼️ User interface

The graphical interface uses:

- SDL3
- SDL3 Renderer
- Dear ImGui
- Dear ImGui docking branch
- SDL3 gamepad APIs

The UI provides access to:

- Cartridge, disk, tape, and program loading
- Drive model and device selection
- Cartridge switches and buttons
- IDE64 storage devices
- REU configuration
- PAL and NTSC selection
- SID model selection
- Joystick and gamepad assignment
- Save states
- Load states
- Warm and cold resets
- Emulator pause
- Integrated monitor access
- Future development continues to allow for configuration of Joystick keys and ROM locations

---

## ✅ Current development focus

Current work is primarily focused on:

- VIC-II cycle and pixel accuracy
- CPU and VIC-II bus arbitration
- Badline and raster timing
- Sprite fetch and display timing
- Open-bus behavior
- Interrupt-edge timing
- SID accuracy
- CIA edge cases
- Drive and IEC timing
- Save-state compatibility and versioning
- Cartridge mapper coverage
- Debugger and monitor improvements
- SDL3 integration and UI cleanup

---

## 🔧 Requirements

- A C++17-compatible compiler
- SDL3
- Boost
- Dear ImGui with the SDL3 and SDLRenderer3 backends

The current Windows development environment uses:

- Code::Blocks
- MinGW-w64 / GCC
- SDL3
- Boost Program_options
- Dear ImGui docking branch

Third-party license files are provided in the `licenses/` directory.

---

## 🧱 Building

Build configuration currently depends on the development environment and project files included with the repository.

### Windows

Typical requirements:

- Install SDL3 development libraries
- Install Boost
- Add the Dear ImGui core source files
- Add the SDL3 Dear ImGui platform backend
- Add the SDLRenderer3 Dear ImGui renderer backend
- Configure compiler include directories
- Configure SDL3 and Boost library directories
- Link against SDL3
- Build using Code::Blocks, MinGW, MSVC, or another compatible toolchain

The following Dear ImGui backend source files should be compiled:

```text
imgui_impl_sdl3.cpp
imgui_impl_sdlrenderer3.cpp
```

The Dear ImGui core and backend files should all come from the same release and branch.

### Linux and macOS

Install SDL3, Boost, Dear ImGui, and a C++17-compatible compiler using the appropriate package manager or development environment.

Project-specific build instructions may be expanded as additional build systems are added.

---

## 🚀 Running

Start the emulator and use the menu bar to load media or configure the machine.

Typical workflow:

1. Launch the emulator.
2. Select a PAL or NTSC machine mode.
3. Attach a cartridge, disk, tape, or program image.
4. Select the appropriate drive or peripheral configuration.
5. Use the integrated monitor when debugging or inspecting execution.

### Global shortcuts

Current shortcuts include:

```text
F12             Toggle the machine-language monitor
Ctrl+Space      Pause or resume emulation
Ctrl+W          Warm reset
Ctrl+Shift+R    Cold reset
Alt+P           Cassette play
Alt+S           Cassette stop
Alt+R           Cassette rewind
Alt+E           Cassette eject
```

Shortcut mappings may change during development.

---

## Command-line options

Command-line media selection is deprecated in favor of the graphical interface, but some options may remain available:

```text
--help                Display the help message
--cartridge <path>    Load a cartridge image at startup
--tape <path>         Load a TAP or T64 image at startup
--program <path>      Load a PRG or P00 image at startup
--version             Display version information
```

---

## ⚠️ Project status

This project is not yet a finished or fully validated Commodore 64 implementation.

Known areas that may still require work include:

- Cycle-exact VIC-II behavior
- Pixel-level rendering edge cases
- SID analog-filter accuracy
- CIA timing edge cases
- IEC and disk-drive timing
- Less-common cartridge hardware
- Save-state compatibility across development versions
- Compatibility with timing-sensitive demos and copy protection

Bug reports should include:

- Software title
- Media format
- PAL or NTSC mode
- Cartridge configuration
- Drive model and device number
- Relevant monitor output
- Steps needed to reproduce the issue

---

## 📖 License

Copyright © 2025–2026 Christopher Broschard.  
All rights reserved.

This source code is provided for personal, educational, and non-commercial use only.

Redistribution, modification, or use of this code, in whole or in part, for any other purpose is prohibited without the prior written consent of the copyright holder.

Third-party dependencies are licensed separately:

- Boost — Boost Software License 1.0
- SDL3 — zlib License
- Dear ImGui — MIT License

See the `licenses/` directory for the complete third-party license texts.

---

## 🙏 Acknowledgements

This project benefits from documentation, testing material, and research produced by the Commodore community, including:

- Commodore 64 Programmer's Reference Guide
- MOS Technology hardware documentation
- VIC-II and SID research by the Commodore community
- VICE emulator behavior and documentation
- C64-Wiki
- Codebase64
- Lemon64
- The developers and contributors behind SDL3, Boost, and Dear ImGui
- The broader Commodore preservation and reverse-engineering community
