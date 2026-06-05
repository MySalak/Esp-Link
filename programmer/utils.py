import os
import sys
import time
from config import *

def clr():
    os.system("cls" if os.name == "nt" else "clear")

def banner(title=""):
    print(f"{C_BOLD}{C_BLUE}╔══════════════════════════════════════════════════════╗{C_RESET}")
    print(f"{C_BOLD}{C_BLUE}║" + "MySalak STM32Duino Programmer".center(54) + f"║{C_RESET}")
    print(f"{C_BOLD}{C_BLUE}║" + "ST-Long32".center(54) + f"║{C_RESET}")
    if title:
        padded = title.center(54)
        print(f"{C_BOLD}{C_BLUE}║{C_CYAN}{padded}{C_BLUE}║{C_RESET}")
    print(f"{C_BOLD}{C_BLUE}╚══════════════════════════════════════════════════════╝{C_RESET}")
    print()

def info(msg):  print(f"  {C_DIM}←{C_RESET} {msg}")
def send(msg):  print(f"  {C_YELLOW}→{C_RESET} {msg}")
def ok(msg):    print(f"  {C_GREEN}✓{C_RESET} {msg}")
def err(msg):   print(f"  {C_RED}✗{C_RESET} {msg}")
def stm(msg):   print(f"  {C_CYAN}[STM32]{C_RESET} {msg}")

def separator():
    print(f"  {C_DIM}{'─'*50}{C_RESET}")

def wait_for_enter_or_cancel():
    if sys.platform == "win32":
        import msvcrt
        while True:
            if msvcrt.kbhit():
                c = msvcrt.getch()
                if c == b'0':
                    print("0")
                    return False
                if c in (b'\r', b'\n'):
                    print()
                    return True
            time.sleep(0.02)
    else:
        try:
            return input().strip() != "0"
        except KeyboardInterrupt:
            return False
