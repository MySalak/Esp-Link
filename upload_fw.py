#!/usr/bin/env python3
"""
ESP32 SWD Programmer - Host Upload Tool
========================================
Uploads a .bin firmware file to STM32U585 via the ESP32 SWD bridge.

Usage:
    python upload_fw.py <serial_port> <firmware.bin> [start_address]

Examples:
    python upload_fw.py COM3 firmware.bin
    python upload_fw.py COM3 firmware.bin 0x08000000
    python upload_fw.py /dev/ttyUSB0 firmware.bin
"""

import sys
import os
import time
import serial
import argparse


def wait_response(ser, timeout=30):
    """Read lines until we get OK, ERROR, or READY."""
    start = time.time()
    lines = []
    while (time.time() - start) < timeout:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='replace').strip()
            if line:
                print(f"  <- {line}")
                lines.append(line)
                if line.startswith("OK") or line.startswith("ERROR") or line == "READY":
                    return line, lines
        else:
            time.sleep(0.01)
    return None, lines


def send_command(ser, cmd, timeout=30):
    """Send a text command and wait for response."""
    print(f"  -> {cmd}")
    ser.write(f"{cmd}\r\n".encode())
    ser.flush()
    return wait_response(ser, timeout)


def main():
    parser = argparse.ArgumentParser(description="Upload firmware to STM32U585 via ESP32 SWD bridge")
    parser.add_argument("port", help="Serial port (e.g., COM3 or /dev/ttyUSB0)")
    parser.add_argument("firmware", help="Path to .bin firmware file")
    parser.add_argument("address", nargs="?", default="0x08000000",
                        help="Start address in flash (default: 0x08000000)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    parser.add_argument("--verify", action="store_true", help="Verify after programming")
    parser.add_argument("--no-reset", action="store_true", help="Don't reset target after programming")
    args = parser.parse_args()

    # Check firmware file
    if not os.path.isfile(args.firmware):
        print(f"Error: File not found: {args.firmware}")
        sys.exit(1)

    fw_size = os.path.getsize(args.firmware)
    print(f"Firmware: {args.firmware} ({fw_size} bytes)")
    print(f"Target address: {args.address}")
    print(f"Serial port: {args.port} @ {args.baud} baud")
    print()

    # Open serial port
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    time.sleep(2)  # Wait for ESP32 to boot if just connected
    ser.reset_input_buffer()

    try:
        # Step 1: Connect
        print("[1/5] Connecting via SWD...")
        resp, _ = send_command(ser, "connect", timeout=10)
        if not resp or not resp.startswith("OK"):
            print("ERROR: Failed to connect. Check wiring!")
            sys.exit(1)

        # Step 2: Unlock flash
        print("\n[2/5] Unlocking flash...")
        resp, _ = send_command(ser, "unlock", timeout=5)
        if not resp or not resp.startswith("OK"):
            print("WARNING: Flash unlock may have failed")

        # Step 3: Program firmware
        print(f"\n[3/5] Programming {fw_size} bytes...")
        resp, _ = send_command(ser, f"program {args.address} {fw_size}", timeout=60)

        if resp == "READY":
            # Send binary firmware data
            with open(args.firmware, "rb") as f:
                fw_data = f.read()

            chunk_size = 1024
            sent = 0
            start_time = time.time()

            while sent < fw_size:
                chunk = fw_data[sent:sent + chunk_size]
                ser.write(chunk)
                ser.flush()
                sent += len(chunk)

                # Print progress
                pct = (sent * 100) // fw_size
                elapsed = time.time() - start_time
                speed = sent / elapsed if elapsed > 0 else 0
                print(f"\r  Uploading: {pct}% ({sent}/{fw_size} bytes, {speed:.0f} B/s)", end="")

                # Wait a bit to avoid overwhelming the ESP32
                time.sleep(0.01)

            print()

            # Wait for programming completion
            print("  Waiting for programming to complete...")
            resp, lines = wait_response(ser, timeout=120)

            # Read any remaining progress messages
            time.sleep(1)
            while ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    print(f"  <- {line}")
                    if line.startswith("OK"):
                        resp = line
        else:
            print("ERROR: Unexpected response to program command")
            sys.exit(1)

        if not resp or not resp.startswith("OK"):
            print("ERROR: Programming failed!")
            sys.exit(1)

        print("  Programming complete!")

        # Step 4: Verify (optional)
        if args.verify:
            print(f"\n[4/5] Verifying {fw_size} bytes...")
            resp, _ = send_command(ser, f"verify {args.address} {fw_size}", timeout=10)

            if resp == "READY":
                with open(args.firmware, "rb") as f:
                    fw_data = f.read()
                ser.write(fw_data)
                ser.flush()

                resp, _ = wait_response(ser, timeout=120)
                if resp and resp.startswith("OK"):
                    print("  Verification passed!")
                else:
                    print("  Verification FAILED!")
                    sys.exit(1)
        else:
            print("\n[4/5] Skipping verification (use --verify to enable)")

        # Step 5: Reset target
        if not args.no_reset:
            print("\n[5/5] Resetting target...")
            resp, _ = send_command(ser, "reset", timeout=5)
            print("  Target reset and running!")
        else:
            print("\n[5/5] Skipping reset (--no-reset)")

        elapsed_total = time.time() - start_time
        print(f"\n{'='*50}")
        print(f"SUCCESS! Programmed {fw_size} bytes in {elapsed_total:.1f}s")
        print(f"{'='*50}")

    finally:
        ser.close()


if __name__ == "__main__":
    main()
