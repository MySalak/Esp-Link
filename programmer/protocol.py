import time
from config import *
from utils import *

def _read_line(ser, timeout=30):
    start = time.time()
    while (time.time() - start) < timeout:
        if ser.in_waiting:
            raw = ser.readline()
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if line.startswith("[STM32] "):
                return line[8:], True      # strip prefix
            return line, False
        time.sleep(0.01)
    return None, False

def wait_response(ser, timeout=30, verbose=True):
    start = time.time()
    while (time.time() - start) < timeout:
        line, is_stm = _read_line(ser, timeout=0.1)
        if line is None:
            continue
        if is_stm:
            if verbose:
                stm(line)
            continue
        if verbose:
            info(line)
        if line.startswith("OK") or line.startswith("ERROR") or line == "READY":
            return line
    return None

def send_cmd(ser, cmd, timeout=30, verbose=True):
    if verbose:
        send(cmd)
    ser.write(f"{cmd}\r\n".encode())
    ser.flush()
    return wait_response(ser, timeout=timeout, verbose=verbose)

def wait_for_chunk_ack(ser, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if ser.in_waiting:
            raw = ser.readline()
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if line.startswith("[STM32] "):
                continue   # ignore STM32 output during transfers
            if (line.startswith("PROGRESS:") or
                    line.startswith("ERROR:") or
                    line.startswith("OK:")):
                return line
        else:
            time.sleep(0.005)
    return None

def send_binary_with_flow(ser, data, label="Uploading"):
    total = len(data)
    sent  = 0
    t0    = time.time()

    while sent < total:
        chunk = data[sent:sent + CHUNK_SIZE]
        ser.write(chunk)
        ser.flush()
        sent += len(chunk)

        pct     = sent * 100 // total
        elapsed = time.time() - t0
        speed   = sent / elapsed if elapsed > 0 else 0
        bar_len = 30
        filled  = int(bar_len * sent / total)
        bar     = "█" * filled + "░" * (bar_len - filled)
        print(f"\r  {C_CYAN}[{bar}]{C_RESET} {pct:3d}%  {sent}/{total} B  {speed:.0f} B/s",
              end="", flush=True)

        ack = wait_for_chunk_ack(ser)
        if ack is None:
            print()
            err("ESP32 stopped responding (timeout waiting for chunk ACK)!")
            return False
        if ack.startswith("ERROR:"):
            print()
            err(ack)
            return False
            
        ram_info = ""
        if "ESP32 RAM:" in ack:
            ram_info = f"  |  ESP32 RAM: {ack.split('ESP32 RAM:')[1].split(')')[0].strip()}"
            
        print(f"\r  {C_CYAN}[{bar}]{C_RESET} {pct:3d}%  {sent}/{total} B  {speed:.0f} B/s{C_DIM}{ram_info}{C_RESET}    ",
              end="", flush=True)

    print()  # newline after progress bar
    return True
