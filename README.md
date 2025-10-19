# 🎮 Commodore 64 Emulator

A C++ Commodore 64 emulator focused on **accuracy and debuggability**, built with SDL2.  
Supports multiple cartridge formats, VIC-II video rendering, SID audio, and an integrated monitor/debugger. This project is actively under development and there are a lot of features that may not work correctly.

---

## ✨ Features
- **VIC-II graphics emulation** (text, bitmap, sprites, multicolor, raster interrupts, badlines).
- **SID sound support** (waveforms, filters, sync, ring modulation, sample playback).
- **PLA memory mapping** with support for 8K, 16K, Ultimax, Ocean, MagicDesk, Dinamic, etc.
- **Cartridge loader** for `.CRT` images with mapper detection.
- **Built-in ML Monitor** for debugging:
  - Breakpoints and watchpoints
  - Assembler/Disassembler
  - Memory/register editing
- Cross-platform: runs on Windows, Linux, and macOS (via SDL2).

---

## 🚀 Getting Started

### Prerequisites
- [SDL2](https://www.libsdl.org/)  
- C++17 or newer compiler (GCC, Clang, MSVC, or MinGW).  
- [Boost](https://www.boost.org/).

---

## 🎮 Usage
Run the emulator, then use the built-in monitor to load software:

```text
Note: Disk is under active development 
Command line options:
  --help                Produce the help message
  --cartridge arg       Path and filename for cartridge to load on boot
  --tape arg            Path and filename for TAP or T64 tape image to load
  --program arg         Path and filename for PRG or P00 image to load
  --disk arg            Path and filename for Disk image file to load (D64,
                        D81, etc.
  --version             Print version and exit.

monitor> cart load cart/Adventureland.crt
monitor> g
```

Debugging examples:
```text
monitor> d $C000 20    ; disassemble 20 bytes from $C000
monitor> bp $E000      ; set breakpoint at $E000
monitor> watch $D020   ; watch border color register
```

## 📖 Licenses

Copyright (c) 2025 Christopher Broschard.  
All rights reserved.

This source code is provided for personal, educational, and
non-commercial use only. Redistribution, modification, or use
in whole or in part for any other purpose is prohibited without
the prior written consent of the copyright holder.

Third-party dependencies are licensed separately:

- [Boost](https://www.boost.org/) – [Boost Software License 1.0](Licenses/BOOST_LICENSE_1_0.txt)
- [SDL2](https://www.libsdl.org/) – [zlib License](Licenses/SDL2_zlib_LICENSE.txt)
- [Dear Imgui](https://https://github.com/ocornut/imgui) - [MIT License](Licenses/MIT_LICENSE.txt)
---

## 🙏 Acknowledgements
- **C64 Programmer’s Reference Guide** for hardware documentation.  
- **VICE Emulator** as a reference implementation.  
- Commodore community for decades of reverse-engineering work.  
