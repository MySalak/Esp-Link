import time
import sys
from pathlib import Path
from config import *
from utils import *
from protocol import send_cmd
from upload import do_upload, do_connect_and_run

def _mass_erase(ser):
    print(f"  Erasing all flash (up to 30 s)…")
    resp = send_cmd(ser, "eraseall", timeout=60)
    if resp and resp.startswith("OK"):
        ok("Mass erase complete!")
        return True
    err("Mass erase failed.")
    return False

def _page_erase(ser, page, bank):
    resp = send_cmd(ser, f"erase {page} {bank}", timeout=30)
    if resp and resp.startswith("OK"):
        ok(f"Page {page} (bank {bank}) erased!")
        return True
    err("Page erase failed.")
    return False

def _check_connection(ser):
    resp = send_cmd(ser, "idcode")
    if resp and resp.startswith("OK:"):
        print()
        ok("Connected successfully to STM32!")
        print(f"  {C_CYAN}Target IDCODE: {resp.split(':')[1].strip()}{C_RESET}")
        return True
    else:
        err("Failed to read IDCODE.")
        return False

def menu_connect(ser):
    clr()
    banner("Check Connection (STM32)")
    if do_connect_and_run(ser, _check_connection):
        print()
        # Also reset the target so it's not left halted
        send_cmd(ser, "reset", timeout=5, verbose=False)
        ok("Target reset and running.")
    else:
        print()

def menu_upload(ser, do_verify=False):
    clr()
    banner("Upload Firmware" + (" (with Verify)" if do_verify else ""))

    bin_dir = Path(BIN_DIR)
    bin_dir.mkdir(exist_ok=True)
    bins = sorted(bin_dir.glob("*.bin"))

    if not bins:
        print(f"  {C_YELLOW}No .bin files found in  {BIN_DIR}/{C_RESET}")
        print(f"  Copy your firmware .bin files into the  {C_BOLD}./bin/{C_RESET}  folder.")
        print()
        input("  Press Enter to go back…")
        return

    print(f"  {C_DIM}Available firmware in  {BIN_DIR}/:{C_RESET}\n")
    for i, b in enumerate(bins, 1):
        sz = b.stat().st_size
        print(f"  {C_BOLD}  {i}.{C_RESET}  {b.name:<40}  {C_DIM}{sz:>10,} B{C_RESET}")
    print()
    print(f"  {C_DIM}  0.  Back{C_RESET}")
    separator()

    choice = input("\n  Select firmware [0]: ").strip()
    if not choice or choice == "0":
        return
    try:
        idx = int(choice) - 1
        if not (0 <= idx < len(bins)):
            raise ValueError
    except ValueError:
        err("Invalid selection.")
        time.sleep(1)
        return

    fw_path = bins[idx]
    print()
    success = do_upload(ser, fw_path, do_verify=do_verify)
    print()
    if success:
        print(f"  {C_GREEN}{C_BOLD}══ Upload successful! ══{C_RESET}")
    else:
        print(f"  {C_RED}{C_BOLD}══ Upload failed. ══{C_RESET}")
    print()
    input("  Press Enter to continue…")

def menu_serial(ser):
    clr()
    banner("Read Serial  (STM32 UART)")
    print(f"  {C_DIM}Listening on RX2 (GPIO16) ← STM32 TX  @  115200 baud{C_RESET}")
    print(f"  {C_DIM}Press  0  or  Ctrl+C  to return to the main menu.{C_RESET}")
    separator()
    print()

    is_windows = sys.platform == "win32"
    if is_windows:
        import msvcrt

    try:
        while True:
            if is_windows and msvcrt.kbhit():
                c = msvcrt.getch()
                if c == b'0':
                    raise KeyboardInterrupt
                    
            if ser.in_waiting:
                raw = ser.readline()
                # Decode as strict ASCII. Any byte > 127 (common in baud glitches) becomes \ufffd
                line = raw.decode("ascii", errors="replace").strip()
                if not line:
                    continue
                if line.startswith("[STM32] "):
                    ts = time.strftime("%H:%M:%S")
                    stm32_text = line[8:]
                    
                    # 1. If it contains ANY invalid ASCII bytes (baud rate glitches), drop it completely
                    if '\ufffd' in stm32_text:
                        continue
                        
                    # 2. Keep only standard printable ASCII
                    filtered = ''.join(c for c in stm32_text if (32 <= ord(c) <= 126) or c == '\t')
                    
                    stripped = filtered.strip()
                    is_noise = False
                    
                    if stripped:
                        # Count letters and numbers
                        alnum_count = sum(c.isalnum() for c in stripped)
                        sym_count = len(stripped) - alnum_count
                        
                        # 3. Drop lines that are almost entirely symbols, UNLESS it's a formatting line
                        if alnum_count == 0:
                            if not (stripped.startswith("===") or stripped.startswith("---")):
                                is_noise = True
                        elif (sym_count > alnum_count * 2) and len(stripped) < 20:
                            if not (stripped.startswith("===") or stripped.startswith("---")):
                                is_noise = True
                                
                        # 4. Drop very short isolated garbage
                        if len(stripped) <= 3 and alnum_count < 2:
                            is_noise = True
                            
                    if not is_noise and (filtered or not stm32_text):
                        print(f"  {C_DIM}{ts}{C_RESET}  {C_CYAN}{filtered}{C_RESET}")
                else:
                    print(f"  {C_WHITE}{line}{C_RESET}")
            else:
                time.sleep(0.01)
    except KeyboardInterrupt:
        print(f"\n  {C_DIM}Stopped.{C_RESET}")
        time.sleep(0.5)

def menu_erase(ser):
    clr()
    banner("Erase Flash")
    print(f"  {C_BOLD}  1.{C_RESET}  Mass erase  {C_DIM}(wipes all flash){C_RESET}")
    print(f"  {C_BOLD}  2.{C_RESET}  Page erase  {C_DIM}(single page){C_RESET}")
    print(f"  {C_DIM}  0.  Back{C_RESET}")
    separator()

    choice = input("\n  Select [0]: ").strip()
    print()

    if choice == "1":
        do_connect_and_run(ser, _mass_erase)
    elif choice == "2":
        try:
            page = int(input("  Page number : ").strip())
            bank_s = input("  Bank (0 / 1) [0]: ").strip()
            bank   = int(bank_s) if bank_s else 0
        except (ValueError, KeyboardInterrupt):
            err("Cancelled.")
            time.sleep(1)
            return
        do_connect_and_run(ser, _page_erase, page, bank)
    elif choice == "0" or choice == "":
        return
    else:
        err("Invalid option.")
        time.sleep(1)
        return

    print()
    input("  Press Enter to continue…")
