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
            if not choice.strip():
                continue
            idx = int(choice)
            if idx == 0:
                return None
            if 1 <= idx <= len(sketches):
                return sketches[idx - 1]
            err("Invalid, please select a valid number.")
        except ValueError:
            err("Invalid input, please enter a number.")


def run_command(cmd, cwd=None):
    send(f"Running: {' '.join(cmd)}")
    if cwd is None:
        cwd = os.path.dirname(os.path.dirname(__file__))  # Project root
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        err(f"Command failed with exit code: {result.returncode}")
        return False
    ok("Command succeeded.")
    return True


def build_sketch(sketch_name, config_dir="configs", target_name=None):
    sketch_path = os.path.join("sketches", sketch_name)
    cmd = [
        "arduino-cli",
        "compile",
        "--fqbn",
        FQBN,
        "--build-path",
        "build",
        "--library",
        config_dir,
        "--libraries",
        "includes",
        sketch_path,
    ]
    success = run_command(cmd)
    if success:
        proj_root = os.path.dirname(os.path.dirname(__file__))
        build_bin = os.path.join(proj_root, "build", f"{sketch_name}.ino.bin")
        if os.path.isfile(build_bin):
            target_dir = os.path.join(os.path.dirname(__file__), "bin")
            os.makedirs(target_dir, exist_ok=True)
            t_name = target_name if target_name else sketch_name
            target_bin = os.path.join(target_dir, f"{t_name}.bin")
            shutil.copy2(build_bin, target_bin)
            ok(f"Copied compiled binary to programmer/bin/{t_name}.bin")
    return success


def upload_sketch_stlink():
    cmd = ["arduino-cli", "upload", "--fqbn", FQBN, "--build-path", "build"]
    return run_command(cmd)


def build_and_upload(
    sketch_name, programmer="stlong32", config_dir="configs", ser=None, target_name=None
):
    if build_sketch(sketch_name, config_dir, target_name=target_name):
        if programmer == "stlink":
            upload_sketch_stlink()
        elif programmer == "stlong32":
            # For STLong32, we need to upload the compiled .bin file
            if ser is None:
                err("STLong32 Programmer requires a connected COM port!")
                return

            # The bin file is generated in the build/ directory
            bin_file = os.path.join(
                os.path.dirname(os.path.dirname(__file__)),
                "build",
                f"{sketch_name}.ino.bin",
            )

            if not os.path.isfile(bin_file):
                err(f"Compiled binary not found at {bin_file}")
                return

            from upload import do_upload

            info(f"Using STLong32 to upload {bin_file}")
            do_upload(ser, bin_file, do_verify=True)


def process_production(sketch_name, programmer="stlong32", ser=None, do_upload=True):
    info("--- Fetching Devices from Chirpstack ---")
    sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
    try:
        import helper.chirpstack as cs

        devices = cs.get_chirpstack_devices()
        if not devices:
            err("No devices found in Chirpstack.")
            return

        info("--- Select Device ---")
        for idx, dev in enumerate(devices, 1):
            name = dev.get("name", "Unknown")
            deui = dev.get("devEui", "")
            print(f"    {C_CYAN}{idx}.{C_RESET} {name} ({deui})")

        dev_choice = input(
            f"\n  Select a device (1-{len(devices)}) or 0 to cancel: "
        ).strip()
        if not dev_choice or not dev_choice.isdigit():
            err("Invalid choice.")
            return

        dev_idx = int(dev_choice)
        if dev_idx == 0 or dev_idx > len(devices):
            err("Invalid choice.")
            return

        selected_dev = devices[dev_idx - 1]
        build_for_device(sketch_name, selected_dev, programmer, ser, do_upload)
    except Exception as e:
        err(f"Error: {e}")


def build_for_device(
    sketch_name,
    selected_dev,
    programmer="stlong32",
    ser=None,
    do_upload=True,
    version_string=None,
):
    try:
        import helper.chirpstack as cs

        activation_resp = cs.get_chirpstack_device_activation(selected_dev["devEui"])
        if not activation_resp:
            err("Failed to get activation for device (is it activated?).")
            return

        activation = activation_resp.get("deviceActivation", activation_resp)
        dev_addr = activation.get("devAddr", "")
        nwk_s_key = activation.get("nwkSEncKey", "")
        app_s_key = activation.get("appSKey", "")

        node_name_clean = (
            selected_dev.get("name", "Unknown").replace(" ", "_").replace("-", "_")
        )
        # target_name = f"{sketch_name.replace('@', '_')}_{node_name_clean}"
        # devEUI
        target_name = (
            f"{selected_dev.get('devEui', 'Unknown')}_{sketch_name.replace('@', '_')}"
        )

        ok(f"Using keys for device {selected_dev.get('name', 'Unknown')}")
    except Exception as e:
        err(f"Error: {e}")
        return

    is_old = False
    if "@" in sketch_name:
        suffix = sketch_name.split("@")[1]
        if "old" in suffix:
            is_old = True

    proj_root = os.path.dirname(os.path.dirname(__file__))
    tmp_dirname = selected_dev["devEui"]
    tmp_dir = os.path.join(proj_root, "configs", tmp_dirname)
    os.makedirs(tmp_dir, exist_ok=True)

    try:
        config_path = os.path.join(proj_root, "configs", "config.h")
        with open(config_path, "r") as f:
            config_content = f.read()

        if dev_addr:
            config_content = re.sub(
                r'const char devAddr\[\] PROGMEM = ".*";',
                f'const char devAddr[] PROGMEM = "{dev_addr}";',
                config_content,
            )
        if nwk_s_key:
            config_content = re.sub(
                r'const char nwkSKey\[\] PROGMEM = ".*";',
                f'const char nwkSKey[] PROGMEM = "{nwk_s_key}";',
                config_content,
            )
        if app_s_key:
            config_content = re.sub(
                r'const char appSKey\[\] PROGMEM = ".*";',
                f'const char appSKey[] PROGMEM = "{app_s_key}";',
                config_content,
            )

        if is_old:
            info("Applying 'old version' RFM_pin mappings.")
            config_content = config_content.replace("// .DIO1 = PB12,", ".DIO1 = PB12,")
            config_content = config_content.replace("// .DIO2 = PB13,", ".DIO2 = PB13,")
            config_content = config_content.replace("// .DIO5 = PB14", ".DIO5 = PB14")

            config_content = config_content.replace(".DIO1 = PA3,", "// .DIO1 = PA3,")
            config_content = config_content.replace(".DIO2 = PA11,", "// .DIO2 = PA11,")
            config_content = config_content.replace(".DIO5 = PA8", "// .DIO5 = PA8")

        if version_string:
            config_content = re.sub(
                r'#define VERSION_STRING ".*"',
                f'#define VERSION_STRING "{version_string}"',
                config_content,
            )

        with open(os.path.join(tmp_dir, "config.h"), "w") as f:
            f.write(config_content)

        rel_tmp_dir = os.path.join("configs", tmp_dirname)
        if do_upload:
            build_and_upload(
                sketch_name,
                programmer=programmer,
                config_dir=rel_tmp_dir,
                ser=ser,
                target_name=target_name,
            )
        else:
            build_sketch(sketch_name, config_dir=rel_tmp_dir, target_name=target_name)
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


# Alias for backwards compatibility
build_and_upload_production = process_production


def gen_db(sketch_name, config_dir="configs"):
    sketch_path = os.path.join("sketches", sketch_name)
    cmd = [
        "arduino-cli",
        "compile",
        "--fqbn",
        FQBN,
        "--build-path",
        "build",
        "--only-compilation-database",
        "--library",
        config_dir,
        "--libraries",
        "includes",
        sketch_path,
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


def scaffold_sketch(
    sketch_name, description="Sketch Description", author="Author Name"
):
    sketch_dir = os.path.join(
        os.path.dirname(os.path.dirname(__file__)), "sketches", sketch_name
    )
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
