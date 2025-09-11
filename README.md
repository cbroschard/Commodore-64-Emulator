# 🎮 Commodore 64 Emulator

A C++ Commodore 64 emulator focused on **accuracy and debuggability**, built with SDL2.  
Supports multiple cartridge formats, VIC-II video rendering, SID audio, and an integrated monitor/debugger.

---

## ✨ Features
- **VIC-II graphics emulation** (text, bitmap, sprites, multicolor, raster interrupts, badlines).
- **SID sound support** (waveforms, filters, sync, ring modulation, sample playback).
- **PLA memory mapping** with support for 8K, 16K, Ultimax, Ocean, MagicDesk, Dinamic, etc.
- **Cartridge loader** for `.CRT` images with mapper detection.
- **Built-in ML Monitor** for debugging:
  - Breakpoints and watchpoints
  - Disassembler
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
monitor> cart load cart/Adventureland.crt
monitor> g
```

Debugging examples:
```text
monitor> d $C000 20    ; disassemble 20 bytes from $C000
monitor> bp $E000      ; set breakpoint at $E000
monitor> watch $D020   ; watch border color register
```

## 📖 License
```
Copyright (c) 2025 Christopher Broschard.  
All rights reserved.

This source code is provided for personal, educational, and
non-commercial use only. Redistribution, modification, or use
in whole or in part for any other purpose is prohibited without
the prior written consent of the copyright holder.
```

---

## 🙏 Acknowledgements
- **C64 Programmer’s Reference Guide** for hardware documentation.  
- **VICE Emulator** as a reference implementation.  
- Commodore community for decades of reverse-engineering work.  
