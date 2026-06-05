import os
import sys
import subprocess
import random
import shutil
import re

from config import *
from utils import info, send, ok, err, clr, banner, separator

FQBN = "STMicroelectronics:stm32:GenU5:pnum=GENERIC_U585CIUX,xserial=generic,usb=none,xusb=FS,dbg=none,rtlib=nano,upload_method=swdMethod"

def get_sketches():
    # Looks one directory up (in the project root) for 'sketches' directory
    sketches_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), "sketches")
    if not os.path.isdir(sketches_dir):
        return []

    sketches = []
    for f in os.listdir(sketches_dir):
        if os.path.isdir(os.path.join(sketches_dir, f)):
            sketches.append(f)
    return sorted(sketches)

def list_sketches():
    sketches = get_sketches()
    if not sketches:
        err("No sketches found.")
    else:
        info("Available sketches:")
        for idx, sketch in enumerate(sketches, 1):
            print(f"    {C_CYAN}{idx}.{C_RESET} {sketch}")

def select_sketch(prefix=None):
    sketches = get_sketches()
    if prefix:
        sketches = [s for s in sketches if s.startswith(prefix)]

    if not sketches:
        err("No sketches available.")
        return None

    for idx, sketch in enumerate(sketches, 1):
        print(f"    {C_CYAN}{idx}.{C_RESET} {sketch}")

    while True:
        try:
            choice = input(f"\n  Select a sketch (1-{len(sketches)}) or 0 to go back: ")
            if not choice.strip(): continue
            idx = int(choice)
            if idx == 0:
                return None
            if 1 <= idx <= len(sketches):
                return sketches[idx-1]
            err("Invalid, please select a valid number.")
        except ValueError:
            err("Invalid input, please enter a number.")

def run_command(cmd, cwd=None):
    send(f"Running: {' '.join(cmd)}")
    if cwd is None:
        cwd = os.path.dirname(os.path.dirname(__file__)) # Project root
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        err(f"Command failed with exit code: {result.returncode}")
        return False
    ok("Command succeeded.")
    return True

def build_sketch(sketch_name, config_dir="configs"):
    sketch_path = os.path.join("sketches", sketch_name)
    cmd = [
        "arduino-cli", "compile",
        "--fqbn", FQBN,
        "--build-path", "build",
        "--library", config_dir,
        "--libraries", "includes",
        sketch_path
    ]
    success = run_command(cmd)
    if success:
        proj_root = os.path.dirname(os.path.dirname(__file__))
        build_bin = os.path.join(proj_root, "build", f"{sketch_name}.ino.bin")
        if os.path.isfile(build_bin):
            target_dir = os.path.join(os.path.dirname(__file__), "bin")
            os.makedirs(target_dir, exist_ok=True)
            target_bin = os.path.join(target_dir, f"{sketch_name}.bin")
            shutil.copy2(build_bin, target_bin)
            ok(f"Copied compiled binary to programmer/bin/{sketch_name}.bin")
    return success

def upload_sketch_stlink():
    cmd = [
        "arduino-cli", "upload",
        "--fqbn", FQBN,
        "--build-path", "build"
    ]
    return run_command(cmd)

def build_and_upload(sketch_name, programmer="stlong32", config_dir="configs", ser=None):
    if build_sketch(sketch_name, config_dir):
        if programmer == "stlink":
            upload_sketch_stlink()
        elif programmer == "stlong32":
            # For STLong32, we need to upload the compiled .bin file
            if ser is None:
                err("STLong32 Programmer requires a connected COM port!")
                return
            
            # The bin file is generated in the build/ directory
            bin_file = os.path.join(os.path.dirname(os.path.dirname(__file__)), "build", f"{sketch_name}.ino.bin")
            if not os.path.isfile(bin_file):
                err(f"Compiled binary not found at {bin_file}")
                return
                
            # Temporarily trick config to point to our build folder
            import config
            old_bin_dir = config.BIN_DIR
            config.BIN_DIR = os.path.dirname(bin_file)
            
            # Upload using our custom menu function
            # Since the menu function prompts for file, we bypass it and call the actual function directly
            from upload import do_connect_and_run, upload_firmware
            
            info(f"Using STLong32 to upload {bin_file}")
            
            with open(bin_file, "rb") as f:
                fw_data = f.read()
            
            do_connect_and_run(ser, upload_firmware, fw_data, True) # do_verify = True
            
            config.BIN_DIR = old_bin_dir

def gen_db(sketch_name, config_dir="configs"):
    sketch_path = os.path.join("sketches", sketch_name)
    cmd = [
        "arduino-cli", "compile",
        "--fqbn", FQBN,
        "--build-path", "build",
        "--only-compilation-database",
        "--library", config_dir,
        "--libraries", "includes",
        sketch_path
    ]
    return run_command(cmd)

def scaffold_sketch_interactive():
    sketch_name = input("\n  Enter new sketch name (or 0 to go back): ").strip()
    if sketch_name == "0":
        return
    if not sketch_name:
        err("Sketch name cannot be empty.")
        return

    description = input("  Enter sketch description: ").strip()
    author = input("  Enter sketch author name: ").strip()

    scaffold_sketch(sketch_name, description, author)

def scaffold_sketch(sketch_name, description="Sketch Description", author="Author Name"):
    sketch_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), "sketches", sketch_name)
    if os.path.exists(sketch_dir):
        err(f"Sketch directory '{sketch_dir}' already exists.")
        return

    os.makedirs(sketch_dir)
    ino_file = os.path.join(sketch_dir, f"{sketch_name}.ino")

    template = f"""/**
 * @file {sketch_name}.ino
 * @brief {description}
 *
 * @author {author}
 *
 * @section LICENSE
 *
 * -- fill the LICENSE yourself --
 *
 * @section DEPENDENCIES
 *
 * -- fill this yourself --
 */

void setup() {{
  // put your setup code here, to run once:

}}

void loop() {{
  // put your main code here, to run repeatedly:

}}
"""
    with open(ino_file, "w") as f:
        f.write(template)

    ok(f"Sketch '{sketch_name}' scaffolded successfully at {ino_file}")
