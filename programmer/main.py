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

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run:  pip install pyserial")
    sys.exit(1)

from config import *
from utils import clr, banner, separator
from menus import menu_connect, menu_upload, menu_serial, menu_erase

def main():
    if os.name == "nt":
        os.system("color")

    parser = argparse.ArgumentParser(description="ESP32 SWD Programmer – Interactive TUI")
    parser.add_argument("port",          help="Serial port (e.g. COM6 or /dev/ttyUSB0)")
    parser.add_argument("--baud", "-b",  type=int, default=BAUD, help=f"Baud rate (default {BAUD})")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"{C_RED}ERROR:{C_RESET} Cannot open {args.port}: {e}")
        sys.exit(1)

    time.sleep(1)
    ser.reset_input_buffer()

    while True:
        clr()
        banner()
        print(f"  Port : {C_BOLD}{args.port}{C_RESET}  @  {args.baud} baud\n")
        print(f"  {C_BOLD}  1.{C_RESET}  Check connection to STM32")
        print(f"  {C_BOLD}  2.{C_RESET}  Upload firmware")
        print(f"  {C_BOLD}  3.{C_RESET}  Upload firmware & Verify")
        print(f"  {C_BOLD}  4.{C_RESET}  Read serial  {C_DIM}(STM32 UART output){C_RESET}")
        print(f"  {C_BOLD}  5.{C_RESET}  Erase flash")
        print(f"  {C_DIM}  0.  Exit{C_RESET}")
        separator()

        try:
            choice = input("\n  Select option: ").strip()
        except KeyboardInterrupt:
            choice = "0"

        try:
            if choice == "1":
                menu_connect(ser)
                input("\n  Press Enter to return to menu…")
            elif choice == "2":
                menu_upload(ser, do_verify=False)
            elif choice == "3":
                menu_upload(ser, do_verify=True)
            elif choice == "4":
                menu_serial(ser)
            elif choice == "5":
                menu_erase(ser)
            elif choice == "0":
                clr()
                print(f"\n  {C_DIM}Goodbye.{C_RESET}\n")
                ser.close()
                break
        except KeyboardInterrupt:
            print(f"\n\n  {C_YELLOW}Action cancelled (Ctrl+C). Returning to menu...{C_RESET}")
            time.sleep(1)

if __name__ == "__main__":
    main()
