import time
from pathlib import Path
from config import *
from utils import *
from protocol import send_cmd, send_binary_with_flow

def do_upload(ser, fw_path, do_verify=False):
    fw_path = Path(fw_path)
    if not fw_path.is_file():
        err(f"File not found: {fw_path}")
        return False

    fw_size = fw_path.stat().st_size
    with open(fw_path, "rb") as f:
        fw_data = f.read()

    print(f"\n  Firmware : {C_BOLD}{fw_path.name}{C_RESET}  ({fw_size:,} bytes)")
    print(f"  Address  : {FLASH_BASE}")
    separator()

    total_steps = 4 if do_verify else 3
    # ── Step 1 : Connect (requires reset button) ──────────────
    print(f"\n  {C_BOLD}[1/{total_steps}] Connect{C_RESET}")
    print(f"  {C_YELLOW}Press and HOLD the STM32 RESET button, then press Enter (or press 0 to cancel)…{C_RESET}", end="", flush=True)
    if not wait_for_enter_or_cancel():
        return False

    total_start_time = time.time()
    resp = send_cmd(ser, "connect", timeout=15)
    if not resp or not resp.startswith("OK"):
        err("Connection failed!")
        return False

    # ── Step 2 : Program ──────────────────────────────────────
    print(f"\n  {C_BOLD}[2/{total_steps}] Program{C_RESET}")
    resp = send_cmd(ser, f"program {FLASH_BASE} {fw_size}", timeout=120)
    if resp != "READY":
        err("Unexpected response – expected READY")
        send_cmd(ser, "disconnect", timeout=5, verbose=False)
        return False

    ok("ESP32 ready – streaming firmware…")
    t_start = time.time()
    if not send_binary_with_flow(ser, fw_data, label="Programming"):
        err("Streaming failed!")
        return False

    # Wait for final OK after all chunks are flashed
    print(f"  Waiting for flash write to finish…")
    resp = ""
    deadline = time.time() + 30
    while time.time() < deadline:
        if ser.in_waiting:
            raw  = ser.readline()
            line = raw.decode("utf-8", errors="replace").strip()
            if not line or line.startswith("[STM32] "):
                continue
            info(line)
            if line.startswith("OK:") or line.startswith("ERROR:"):
                resp = line
                break
        time.sleep(0.01)

    if not resp.startswith("OK"):
        err("Flash write failed!")
        return False

    elapsed = time.time() - t_start
    ok(f"Programmed {fw_size:,} bytes in {elapsed:.1f}s")

    if do_verify:
        print(f"\n  {C_BOLD}[3/{total_steps}] Verify{C_RESET}")
        resp = send_cmd(ser, f"verify {FLASH_BASE} {fw_size}", timeout=10)
        if resp != "READY":
            err("Unexpected response – expected READY")
            send_cmd(ser, "disconnect", timeout=5, verbose=False)
            return False

        ok("ESP32 ready – streaming verification data…")
        v_start = time.time()
        if not send_binary_with_flow(ser, fw_data, label="Verifying"):
            err("Streaming verification data failed!")
            return False

        print(f"  Waiting for verification to finish…")
        resp = ""
        deadline = time.time() + 30
        while time.time() < deadline:
            if ser.in_waiting:
                raw  = ser.readline()
                line = raw.decode("utf-8", errors="replace").strip()
                if not line or line.startswith("[STM32] "):
                    continue
                info(line)
                if line.startswith("OK:") or line.startswith("ERROR:"):
                    resp = line
                    break
            time.sleep(0.01)

        if not resp.startswith("OK"):
            err("Verification failed!")
            return False
            
        v_elapsed = time.time() - v_start
        ok(f"Verification passed in {v_elapsed:.1f}s!")

    # ── Final Step : Reset & run ──────────────────────────────────
    print(f"\n  {C_BOLD}[{total_steps}/{total_steps}] Reset target{C_RESET}")
    send_cmd(ser, "reset", timeout=5)
    ok("Target is running!")
    
    total_elapsed = time.time() - total_start_time
    print(f"\n  {C_BOLD}{C_GREEN}Overall time taken: {total_elapsed:.1f} seconds{C_RESET}")
    return True

def do_connect_and_run(ser, fn, *args):
    """
    Connect (with reset prompt), call fn(ser, *args), then disconnect.
    """
    print(f"  {C_YELLOW}Press and HOLD the STM32 RESET button, then press Enter (or press 0 to cancel)…{C_RESET}", end="", flush=True)
    if not wait_for_enter_or_cancel():
        return False

    resp = send_cmd(ser, "connect", timeout=15)
    if not resp or not resp.startswith("OK"):
        err("Connection failed!")
        return False

    result = fn(ser, *args)
    send_cmd(ser, "disconnect", timeout=5, verbose=False)
    return result
