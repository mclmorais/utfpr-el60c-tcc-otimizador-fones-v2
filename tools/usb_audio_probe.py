#!/usr/bin/env python3
"""Check on the board that USB audio from the host reaches the speaker.

Flashes the firmware, plays a quiet 440 Hz tone to the board's USB audio
device and reads, over ST-LINK in hot-plug mode, the state of the playback
session and of the speaker node. The test passes when both are STARTED while
the tone plays. It also reports the worst-case cycles per packet of each DSP
Module and of the whole Chain, measured by the firmware with the DWT cycle
counter, against the 1 ms packet budget. Run it after updating the HAL or the
USB code: a build that compiles cleanly can still leave the speaker silent.

Needs the board on both USB cables (ST-LINK and USB OTG) and the Python
packages sounddevice and numpy.

Usage: tools/usb_audio_probe.py [firmware.elf]   # default build/Debug/horoscope.elf
"""

import glob
import os
import re
import subprocess
import sys
import threading
import time

from lcd_capture import bundle_path, programmer_cli

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVICE_NAME = "STM32 AUDIO"
TONE_SECONDS = 10
# The host takes a few seconds to enumerate the board after a reset, and to start the stream after play().
ENUMERATION_SECONDS = 8
STREAM_START_SECONDS = 6

# AUDIO_Session_t.state sits after the node_list pointer; AUDIO_Node_t.state is the first field.
SESSION = ("USB_AudioPlabackSession", 4)
SPEAKER_NODE = ("PlaybackSpeakerOutputNode", 0)
CHAIN_CYCLES = "chainWorstCaseCycles"
MODULE_CYCLES = "moduleWorstCaseCycles"
# Chain order, as in the chain array of Application/DSP/Src/audio_user_dsp.c.
MODULES = ["Equalizador"]
CPU_HZ = 216_000_000
PACKET_BUDGET_CYCLES = CPU_HZ // 1000
STATES = ["OFF", "INITIALIZED", "STARTED", "STOPPED", "ERROR"]


def symbols(elf):
    """Map each symbol name to (address, size in bytes); size is 0 when the ELF does not record one."""
    found = glob.glob(os.path.join(bundle_path(), "gnu-tools-for-stm32", "*", "bin", "arm-none-eabi-nm*"))
    if not found:
        sys.exit("arm-none-eabi-nm not found; install the 'gnu-tools-for-stm32' bundle")
    out = subprocess.run([sorted(found)[-1], "-S", elf], capture_output=True, text=True, check=True).stdout
    table = {}
    for fields in (line.split() for line in out.splitlines()):
        if len(fields) == 4:
            table[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
        elif len(fields) == 3:
            table[fields[2]] = (int(fields[0], 16), 0)
    return table


def programmer(*args):
    result = subprocess.run([programmer_cli(), "-c", "port=SWD", *args], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(result.stdout + result.stderr)
    return result.stdout


def read_state(addresses, field):
    value = read_word(addresses, *field)
    return STATES[value] if value < len(STATES) else hex(value)


def read_word(addresses, symbol, offset=0):
    out = programmer("mode=HOTPLUG", "-r32", hex(addresses[symbol][0] + offset), "4")
    return int(re.search(r"^0x[0-9A-F]+ : ([0-9A-F]{8})", out, re.MULTILINE).group(1), 16)


def cycles_line(name, cycles):
    return (f"{name}: {cycles} cycles per packet "
            f"({100 * cycles / PACKET_BUDGET_CYCLES:.1f}% of the {PACKET_BUDGET_CYCLES}-cycle 1 ms budget)")


def play_tone(sd, np):
    devices = [i for i, d in enumerate(sd.query_devices())
               if DEVICE_NAME in d["name"] and d["max_output_channels"] > 0]
    if not devices:
        sys.exit(f"no output device named '{DEVICE_NAME}'; is the USB OTG cable connected?")
    rate = int(sd.query_devices(devices[0])["default_samplerate"])
    t = np.arange(TONE_SECONDS * rate) / rate
    tone = (0.02 * np.sin(2 * np.pi * 440 * t)).astype("float32")
    sd.play(np.column_stack([tone, tone]), rate, device=devices[0], blocking=True)


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "Debug", "horoscope.elf")
    addresses = symbols(elf)
    module_count = addresses[MODULE_CYCLES][1] // 4
    if module_count != len(MODULES):
        sys.exit(f"the firmware Chain has {module_count} Modules but MODULES names {len(MODULES)}; update MODULES")
    programmer("-w", elf, "-v", "-rst")
    time.sleep(ENUMERATION_SECONDS)

    # PortAudio lists devices when it loads, so import it only after the board enumerates.
    import numpy as np
    import sounddevice as sd

    idle = (read_state(addresses, SESSION), read_state(addresses, SPEAKER_NODE))
    player = threading.Thread(target=play_tone, args=(sd, np))
    player.start()
    time.sleep(STREAM_START_SECONDS)
    playing = (read_state(addresses, SESSION), read_state(addresses, SPEAKER_NODE))
    player.join()
    module_cycles = [read_word(addresses, MODULE_CYCLES, 4 * i) for i in range(len(MODULES))]
    chain_cycles = read_word(addresses, CHAIN_CYCLES)

    print(f"idle:    session={idle[0]} speaker={idle[1]}")
    print(f"playing: session={playing[0]} speaker={playing[1]}")
    print("dsp worst case:")
    for name, cycles in zip(MODULES, module_cycles):
        print("  " + cycles_line(name, cycles))
    print("  " + cycles_line("Cadeia", chain_cycles))
    passed = playing == ("STARTED", "STARTED")
    print("PASS" if passed else "FAIL: the speaker did not start while the tone played")
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
