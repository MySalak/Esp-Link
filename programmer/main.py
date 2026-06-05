#!/usr/bin/env python3
"""
ESP32 SWD Programmer - Interactive Terminal UI
===============================================
Run:  python main.py COM6
"""

import os
import sys
import time
import argparse
import sys
import time

import serial
import serial.tools.list_ports
from config import *
from utils import clr, banner, separator, C_RED, C_RESET, C_BOLD, C_DIM, C_YELLOW, C_CYAN
from menus import menu_connect, menu_upload, menu_serial, menu_erase

from sketch_utils import (
    list_sketches, select_sketch, build_sketch, build_and_upload, 
    gen_db, scaffold_sketch_interactive, upload_sketch_stlink
)

def submenu_stlink():
    while True:
        clr()
        banner("ST-Link Programmer Menu")
        print(f"  {C_BOLD}  1.{C_RESET}  Upload")
        print(f"  {C_BOLD}  2.{C_RESET}  Build and Upload")
        print(f"  {C_DIM}  0.  Back to Main Menu{C_RESET}")
        separator()

        try:
            choice = input("\n  Select option: ").strip()
        except KeyboardInterrupt:
            choice = "0"

        try:
            if choice == "1":
                upload_sketch_stlink()
                input("\n  Press Enter to return to menu…")
            elif choice == "2":
                sketch = select_sketch()
                if sketch:
                    build_and_upload(sketch, programmer="stlink")
                input("\n  Press Enter to return to menu…")
            elif choice == "0":
                break
        except KeyboardInterrupt:
            print(f"\n\n  {C_YELLOW}Action cancelled (Ctrl+C). Returning...{C_RESET}")
            time.sleep(1)

def prompt_com_port():
    ports = serial.tools.list_ports.comports()
    if not ports:
        choice = input("\n  No COM ports detected. Enter manually (e.g. COM6) or 0 to go back: ").strip()
        if choice == "0": return None
        return choice
    
    print("\n  Available COM Ports:")
    for i, p in enumerate(ports, 1):
        print(f"    {C_CYAN}{i}.{C_RESET} {p.device} - {p.description}")
    print(f"    {C_DIM}0. Go back{C_RESET}")
    
    choice = input(f"\n  Select port (1-{len(ports)}), type manually, or 0 to go back: ").strip()
    if choice == "0":
        return None
    if choice.isdigit() and 1 <= int(choice) <= len(ports):
        return ports[int(choice)-1].device
    return choice

def submenu_stlong(ser):
    while True:
        clr()
        banner("ST-Long Programmer Menu")
        if ser:
            print(f"  Port : {C_BOLD}{ser.port}{C_RESET}  @  {ser.baudrate} baud\n")
        else:
            print(f"  Port : {C_RED}NOT CONNECTED{C_RESET}\n")

        print(f"  {C_BOLD}  1.{C_RESET}  Build & Upload Sketch")
        print(f"  {C_BOLD}  2.{C_RESET}  Check connection to STM32")
        print(f"  {C_BOLD}  3.{C_RESET}  Upload firmware file")
        print(f"  {C_BOLD}  4.{C_RESET}  Upload & Verify")
        print(f"  {C_BOLD}  5.{C_RESET}  Read serial  {C_DIM}(STM32 UART output){C_RESET}")
        print(f"  {C_BOLD}  6.{C_RESET}  Erase flash")
        print(f"  {C_DIM}  0.  Back to Main Menu{C_RESET}")
        separator()

        try:
            choice = input("\n  Select option: ").strip()
        except KeyboardInterrupt:
            choice = "0"

        try:
            if choice == "1":
                sketch = select_sketch()
                if sketch:
                    build_and_upload(sketch, programmer="stlong", ser=ser)
                input("\n  Press Enter to return to menu…")
            elif choice == "2":
                if ser: menu_connect(ser)
                input("\n  Press Enter to return to menu…")
            elif choice == "3":
                if ser: menu_upload(ser, do_verify=False)
            elif choice == "4":
                if ser: menu_upload(ser, do_verify=True)
            elif choice == "5":
                if ser: menu_serial(ser)
            elif choice == "6":
                if ser: menu_erase(ser)
            elif choice == "0":
                break
        except KeyboardInterrupt:
            print(f"\n\n  {C_YELLOW}Action cancelled (Ctrl+C). Returning...{C_RESET}")
            time.sleep(1)

def main():
    if os.name == "nt":
        os.system("color")

    ser = None
    while True:
        clr()
        banner("Main Menu")
        print(f"  {C_BOLD}  1.{C_RESET}  Use ST-Link Programmer")
        print(f"  {C_BOLD}  2.{C_RESET}  Use ST-Long Programmer (ESP32 Bridge)")
        print(f"  {C_BOLD}  3.{C_RESET}  List Sketches")
        print(f"  {C_BOLD}  4.{C_RESET}  Scaffold New Sketch")
        print(f"  {C_BOLD}  5.{C_RESET}  Build Sketch")
        print(f"  {C_BOLD}  6.{C_RESET}  Generate Build DB")
        print(f"  {C_DIM}  0.  Exit{C_RESET}")
        separator()

        try:
            choice = input("\n  Select option: ").strip()
        except KeyboardInterrupt:
            choice = "0"

        try:
            if choice == "1":
                submenu_stlink()
            elif choice == "2":
                if ser is None:
                    port = prompt_com_port()
                    if port:
                        try:
                            ser = serial.Serial(port, BAUD, timeout=0.1)
                            time.sleep(1)
                            ser.reset_input_buffer()
                        except serial.SerialException as e:
                            print(f"\n{C_RED}ERROR:{C_RESET} Cannot open {port}: {e}")
                            time.sleep(2)
                if ser:
                    submenu_stlong(ser)
            elif choice == "3":
                list_sketches()
                input("\n  Press Enter to return to menu…")
            elif choice == "4":
                scaffold_sketch_interactive()
                input("\n  Press Enter to return to menu…")
            elif choice == "5":
                sketch = select_sketch()
                if sketch: build_sketch(sketch)
                input("\n  Press Enter to return to menu…")
            elif choice == "6":
                sketch = select_sketch()
                if sketch: gen_db(sketch)
                input("\n  Press Enter to return to menu…")
            elif choice == "0":
                clr()
                print(f"\n  {C_DIM}Goodbye.{C_RESET}\n")
                if ser:
                    ser.close()
                break
        except KeyboardInterrupt:
            print(f"\n\n  {C_YELLOW}Action cancelled (Ctrl+C). Returning...{C_RESET}")
            time.sleep(1)

if __name__ == "__main__":
    main()
