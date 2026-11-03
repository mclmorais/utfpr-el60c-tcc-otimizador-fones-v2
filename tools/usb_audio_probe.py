#!/usr/bin/env python3
"""Check on the board that USB audio from the host reaches the speaker.

Flashes the firmware, plays a quiet 440 Hz tone to the board's USB audio
device and reads, over ST-LINK in hot-plug mode, the state of the playback
session and of the speaker node. The test passes when both are STARTED while
the tone plays. It also reports the worst-case cycles per packet of each DSP
Module and of the whole Chain, measured by the firmware with the DWT cycle
counter, against the 1 ms packet budget. Run it after updating the HAL or the
USB code: a build that compiles cleanly can still leave the speaker silent.

With --worst-case, before the tone it writes over ST-LINK the heaviest state
the DSP can be in: every Band away from 0 dB and both Modules on, with the
Crossfeed as the Technique. Once the speaker plays it switches to the Virtual
speakers, so the worst case holds a packet where both Techniques are computed
and then the Virtual speakers alone.

Needs the board on both USB cables (ST-LINK and USB OTG) and the Python
packages sounddevice and numpy.

Usage:
  tools/usb_audio_probe.py [firmware.elf]                # default build/Debug/horoscope.elf
  tools/usb_audio_probe.py --worst-case [firmware.elf]
"""

import argparse
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
TONE_SECONDS = 16
# The host takes a few seconds to enumerate the board after a reset, and to start the stream after play().
ENUMERATION_SECONDS = 8
STREAM_START_SECONDS = 6

# AUDIO_Session_t.state sits after the node_list pointer; AUDIO_Node_t.state is the first field.
SESSION = ("USB_AudioPlabackSession", 4)
SPEAKER_NODE = ("PlaybackSpeakerOutputNode", 0)
CHAIN_CYCLES = "chainWorstCaseCycles"
MODULE_CYCLES = "moduleWorstCaseCycles"
# Chain order, as in the chain array of Application/DSP/Src/audio_user_dsp.c.
MODULES = ["Virtualizacao", "Equalizador"]
CPU_HZ = 216_000_000
PACKET_BUDGET_CYCLES = CPU_HZ // 1000
STATES = ["OFF", "INITIALIZED", "STARTED", "STOPPED", "ERROR"]
BAND_COUNT = 31
# eqGains is in tenths of a dB; alternating signs keep the boosted tone far from full scale.
WORST_CASE_GAIN = 60
# VIRTUALIZER_VIRTUAL_SPEAKERS in Application/DSP/Inc/virtualizer.h.
VIRTUAL_SPEAKERS = 1
CROSSFEED = 0


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


def write(addresses, symbol, offset, bits, value):
    """Write a value of the given width into a running firmware's variable."""
    value &= (1 << bits) - 1
    programmer("mode=HOTPLUG", f"-w{bits}", hex(addresses[symbol][0] + offset), hex(value))


def apply_worst_case(addresses):
    for band in range(BAND_COUNT):
        write(addresses, "eqGains", 2 * band, 16, WORST_CASE_GAIN if band % 2 else -WORST_CASE_GAIN)
    write(addresses, "eqEnabled", 0, 8, 1)
    write_technique(addresses, CROSSFEED)
    write(addresses, "virtEnabled", 0, 8, 1)


def write_technique(addresses, technique):
    write(addresses, "virtTechnique", 0, 8 * addresses["virtTechnique"][1], technique)


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
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf", nargs="?", default=os.path.join(ROOT, "build", "Debug", "horoscope.elf"))
    parser.add_argument("--worst-case", action="store_true", help="measure with the heaviest DSP state")
    args = parser.parse_args()
    elf = args.elf
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
    if args.worst_case:
        apply_worst_case(addresses)
    player = threading.Thread(target=play_tone, args=(sd, np))
    player.start()
    time.sleep(STREAM_START_SECONDS)
    playing = (read_state(addresses, SESSION), read_state(addresses, SPEAKER_NODE))
    if args.worst_case:
        write_technique(addresses, VIRTUAL_SPEAKERS)
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
