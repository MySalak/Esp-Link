#!/usr/bin/env python3
"""
ESP32 SWD Programmer - Interactive Terminal UI
===============================================
Run:  python main.py
"""

import os
import sys
import time

import serial
import serial.tools.list_ports
from config import *
from utils import (
    clr,
    banner,
    separator,
    C_RED,
    C_RESET,
    C_BOLD,
    C_DIM,
    C_YELLOW,
    C_CYAN,
    C_GREEN,
)
from menus import menu_connect, menu_upload, menu_serial, menu_erase

from sketch_utils import (
    list_sketches,
    select_sketch,
    build_sketch,
    build_and_upload,
    gen_db,
    scaffold_sketch_interactive,
    upload_sketch_stlink,
    build_and_upload_production,
    process_production
)

# ─── Shared serial state ─────────────────────────────────────────────────────
_ser = None


def prompt_com_port():
    ports = serial.tools.list_ports.comports()
    if not ports:
        choice = input(
            "\n  No COM ports detected. Enter manually (e.g. COM6) or 0 to go back: "
        ).strip()
        if choice == "0":
            return None
        return choice

    print("\n  Available COM Ports:")
    for i, p in enumerate(ports, 1):
        print(f"    {C_CYAN}{i}.{C_RESET} {p.device} - {p.description}")
    print(f"    {C_DIM}0. Go back{C_RESET}")

    choice = input(
        f"\n  Select port (1-{len(ports)}), type manually, or 0 to go back: "
    ).strip()
    if choice == "0":
        return None
    if choice.isdigit() and 1 <= int(choice) <= len(ports):
        return ports[int(choice) - 1].device
    return choice


def get_or_connect_ser():
    """Return the active serial connection, prompting to connect if not yet open."""
    global _ser
    if _ser is not None and _ser.is_open:
        return _ser

    print(f"\n  {C_YELLOW}ST-Long requires a serial connection.{C_RESET}")
    port = prompt_com_port()
    if not port:
        return None
    try:
        _ser = serial.Serial(port, BAUD, timeout=0.1)
        time.sleep(1)
        _ser.reset_input_buffer()
        print(f"  {C_GREEN}Connected to {port} @ {BAUD} baud{C_RESET}")
        return _ser
    except serial.SerialException as e:
        print(f"\n  {C_RED}ERROR:{C_RESET} Cannot open {port}: {e}")
        time.sleep(2)
        return None


# ─── Programmer selector ──────────────────────────────────────────────────────
def select_programmer():
    """Prompt the user to choose ST-Link or ST-Long. Returns 'stlink' | 'stlong' | None."""
    print(f"\n  {C_BOLD}Select Programmer:{C_RESET}")
    print(f"    {C_CYAN}1.{C_RESET} ST-Link")
    print(f"    {C_CYAN}2.{C_RESET} ST-Long  {C_DIM}(ESP32 Bridge){C_RESET}")
    print(f"    {C_DIM}0. Cancel{C_RESET}")
    while True:
        try:
            choice = input("\n  Choice: ").strip()
        except KeyboardInterrupt:
            return None
        if choice == "1":
            return "stlink"
        if choice == "2":
            return "stlong"
        if choice == "0":
            return None


# ─── Programmer submenu ───────────────────────────────────────────────────────
def submenu_programmer():
    global _ser
    while True:
        clr()
        banner("Programmer Menu")

        # Show ST-Long connection status
        if _ser and _ser.is_open:
            print(
                f"  ST-Long : {C_GREEN}{C_BOLD}{_ser.port}{C_RESET}  @  {_ser.baudrate} baud\n"
            )
        else:
            print(f"  ST-Long : {C_DIM}not connected{C_RESET}\n")

        print(f"  {C_BOLD}{C_MAGENTA}{'═'*50}{C_RESET}")
        print(f"  {C_BOLD}  1.{C_RESET}  Upload")
        print(f"  {C_BOLD}  2.{C_RESET}  Build")
        print(f"  {C_BOLD}  3.{C_RESET}  Build  {C_DIM}(Production){C_RESET}")
        print(f"  {C_BOLD}  4.{C_RESET}  Build & Upload")
        print(f"  {C_BOLD}  5.{C_RESET}  Build & Upload  {C_DIM}(Production){C_RESET}")
        print(f"  {C_BOLD}{C_RED}{'═'*15} {C_MAGENTA}ST-Long utilities{C_RED} {'═'*16}{C_RESET}")
        print(f"  {C_BOLD}  6.{C_RESET}  Check connection to STM32")
        print(
            f"  {C_BOLD}  7.{C_RESET}  Read serial  "
        )
        print(f"  {C_BOLD}  8.{C_RESET}  Erase flash")
        print(f"  {C_DIM}  0.  Back to Main Menu{C_RESET}")
        print(f"  {C_BOLD}{C_MAGENTA}{'═'*50}{C_RESET}")

        try:
            choice = input("\n  Select option: ").strip()
        except KeyboardInterrupt:
            choice = "0"

        try:
            # ── Upload actions (programmer selection happens here) ──
            if choice == "1":
                prog = select_programmer()
                if prog == "stlink":
                    upload_sketch_stlink()
                    input("\n  Press Enter to return to menu…")
                elif prog == "stlong":
                    ser = get_or_connect_ser()
                    if ser:
                        menu_upload(ser, do_verify=True)
                        input("\n  Press Enter to return to menu…")

            elif choice == "2":
                sketch = select_sketch()
                if sketch:
                    build_sketch(sketch)
                input("\n  Press Enter to return to menu…")

            elif choice == "3":
                sketch = select_sketch(prefix="production")
                if sketch:
                    process_production(sketch, do_upload=False)
                input("\n  Press Enter to return to menu…")

            elif choice == "4":
                prog = select_programmer()
                if prog:
                    sketch = select_sketch()
                    if sketch:
                        if prog == "stlong":
                            ser = get_or_connect_ser()
                            if ser:
                                build_and_upload(sketch, programmer="stlong32", ser=ser)
                        else:
                            build_and_upload(sketch, programmer="stlink")
                    input("\n  Press Enter to return to menu…")

            elif choice == "5":
                prog = select_programmer()
                if prog:
                    sketch = select_sketch(prefix="production")
                    if sketch:
                        if prog == "stlong":
                            ser = get_or_connect_ser()
                            if ser:
                                build_and_upload_production(
                                    sketch, programmer="stlong32", ser=ser
                                )
                        else:
                            build_and_upload_production(sketch, programmer="stlink")
                    input("\n  Press Enter to return to menu…")

            # ── ST-Long-only utilities ──
            elif choice == "6":
                ser = get_or_connect_ser()
                if ser:
                    menu_connect(ser)
                input("\n  Press Enter to return to menu…")

            elif choice == "7":
                ser = get_or_connect_ser()
                if ser:
                    menu_serial(ser)

            elif choice == "8":
                ser = get_or_connect_ser()
                if ser:
                    menu_erase(ser)

            elif choice == "0":
                break

        except serial.SerialException as e:
            print(f"\n  {C_RED}Serial Connection Error:{C_RESET} {e}")
            print(f"  {C_YELLOW}The connection to ST-Long was lost. Disconnecting...{C_RESET}")
            if _ser and _ser.is_open:
                try: _ser.close()
                except: pass
            _ser = None
            time.sleep(2)
        except KeyboardInterrupt:
            print(f"\n\n  {C_YELLOW}Action cancelled (Ctrl+C). Returning...{C_RESET}")
            time.sleep(1)

# ─── Main menu ────────────────────────────────────────────────────────────────
def main():
    if os.name == "nt":
        os.system("color")

    global _ser
    while True:
        clr()
        banner("Main Menu")
        print(f"  {C_BOLD}  1.{C_RESET}  Programmer")
        print(f"  {C_BOLD}  2.{C_RESET}  List Sketches")
        print(f"  {C_BOLD}  3.{C_RESET}  Scaffold New Sketch")
        print(f"  {C_BOLD}  4.{C_RESET}  Generate Build DB")
        print(f"  {C_DIM}  0.  Exit{C_RESET}")
        separator()

        try:
            choice = input("\n  Select option: ").strip()
        except KeyboardInterrupt:
            choice = "0"

        try:
            if choice == "1":
                submenu_programmer()
            elif choice == "2":
                list_sketches()
                input("\n  Press Enter to return to menu…")
            elif choice == "3":
                scaffold_sketch_interactive()
                input("\n  Press Enter to return to menu…")
            elif choice == "4":
                sketch = select_sketch()
                if sketch:
                    gen_db(sketch)
                input("\n  Press Enter to return to menu…")
            elif choice == "0":
                clr()
                print(f"\n  {C_DIM}Goodbye.{C_RESET}\n")
                if _ser and _ser.is_open:
                    _ser.close()
                break
        except serial.SerialException as e:
            print(f"\n  {C_RED}Serial Connection Error:{C_RESET} {e}")
            print(f"  {C_YELLOW}The connection to ST-Long was lost. Disconnecting...{C_RESET}")
            if _ser and _ser.is_open:
                try: _ser.close()
                except: pass
            _ser = None
            time.sleep(2)
        except KeyboardInterrupt:
            print(f"\n\n  {C_YELLOW}Action cancelled (Ctrl+C). Returning...{C_RESET}")
            time.sleep(1)


if __name__ == "__main__":
    main()
