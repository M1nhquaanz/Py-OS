# 🐍 PyOS: Baremetal MicroPython Kernel Sandbox

[![Architecture](https://img.shields.io/badge/Architecture-x86__32%20Protected%20Mode-blue.svg)](https://en.wikipedia.org/wiki/X86)
[![Kernel](https://img.shields.io/badge/Kernel-Freestanding%20C99-orange.svg)]()
[![Engine](https://img.shields.io/badge/Engine-MicroPython%20v1.20-brightgreen.svg)](https://micropython.org/)
[![Toolchain](https://img.shields.io/badge/Toolchain-MinGW--w64%20%2F%20GCC-lightgrey.svg)](https://www.mingw-w64.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

> ⚠️ **Disclaimer / Note**: PyOS **is not a full-fledged or commercial Operating System**. This is a solo hobby project built purely for **"vibecoding"** (experimenting and coding for fun). At its core, it is a low-level **C Kernel Sandbox** that embeds the MicroPython v1.20 interpreter directly into the kernel level to run straight on bare-metal x86 hardware (or via QEMU) without relying on any underlying host OS like Windows or Linux.
>
> 📌 **Known Issue**: There is a minor rendering bug where `_0x0a_` appears on output during code execution due to unhandled newline byte sequences (I will fix this later).
---

## ✨ Features & Highlights

- **Vibe-coded Baremetal Engine**: Embeds MicroPython v1.20 directly into the C Kernel layer, managing its own heap and executing Python independently of a host OS.
- **Custom VGA Text GUI**: 
  - Fake desktop text-mode GUI (80x25 resolution) rendered directly into VGA video memory (`0xB8000`).
  - Supports desktop windows, a status bar, and retro color palettes.
- **In-Memory RAM VFS (Virtual File System)**:
  - Lightweight in-memory virtual file system to store and manage Python scripts.
  - Built-in shell commands: `new`, `open`, `save`, `close`, `cat`, `ls`, `touch`, `write`, `rm`.
- **Python Execution Environment**:
  - Run multi-line Python scripts stored in the RAM VFS using the `run <filename>` command.
  - Execute single-line Python statements directly in the interactive shell terminal.
- **Baremetal Memory & Hardware Handling**:
  - Manages a 256KB Dynamic Heap dedicated to MicroPython's Garbage Collector (GC).
  - Handles PS/2 keyboard interrupts directly, supporting arrow keys, Backspace, Enter, and Shift key combos.

---

## 📸 Interface Preview

The simulated Desktop & Terminal GUI rendered entirely via Kernel VGA memory:

```text
+------------------------------------------------------------------------------+
|  [PyOS v1.0 Enhanced VGA]        Arch: x86_32 | RAM Heap: 256KB | Status: OK |
+------------------------------------------------------------------------------+
| Terminal - MicroPython Interpreter Subsystem                                 |
|                                                                              |
| root@pyos:~# help                                                            |
| PyOS Builtin Shell Commands:                                                 |
|   help               - Show command list                                     |
|   fetch              - Display architecture system info                      |
|   ls                 - List files in virtual RAM disk                        |
|   new <file>         - Create and open a new file buffer                     |
|   open <file>        - Open file contents into active buffer                 |
|   save [file]        - Save current buffer to file                           |
|   run <file>         - Execute Python script from VFS                        |
|   clear              - Clear screen                                          |
|                                                                              |
+------------------------------------------------------------------------------+
|  [F1] Clear | Commands: help, ls, new, open, save, close, cat, rm, run       |
+------------------------------------------------------------------------------+