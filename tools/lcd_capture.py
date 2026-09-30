#!/usr/bin/env python3
"""Capture the STM32F769I-DISCO LCD framebuffer over ST-LINK into a PNG.

Reads LTDC layer 0 (ARGB8888, 800x480 at 0xC0000000) from SDRAM in hot-plug
mode, so the firmware keeps running. A full frame takes about 10 s on the
on-board ST-LINK/V2-1; anything redrawn during that window may look torn.

Usage: tools/lcd_capture.py [output.png]
"""

import glob
import os
import subprocess
import sys
import tempfile

from PIL import Image

FB_ADDRESS = 0xC0000000
WIDTH, HEIGHT = 800, 480
FB_SIZE = WIDTH * HEIGHT * 4


def programmer_cli():
    found = glob.glob(os.path.expanduser(
        "~/.local/share/stm32cube/bundles/programmer/*/bin/STM32_Programmer_CLI"))
    if not found:
        sys.exit("STM32_Programmer_CLI not found; install the 'programmer' bundle")
    return sorted(found)[-1]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "lcd.png"
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "fb.bin")
        result = subprocess.run(
            [programmer_cli(), "-q", "-c", "port=SWD", "mode=HOTPLUG",
             "-u", hex(FB_ADDRESS), str(FB_SIZE), raw],
            capture_output=True, text=True)
        if result.returncode != 0 or not os.path.exists(raw):
            sys.exit(result.stdout + result.stderr)
        with open(raw, "rb") as f:
            data = f.read()
    Image.frombytes("RGBA", (WIDTH, HEIGHT), data, "raw", "BGRA").convert("RGB").save(out)
    print(out)


if __name__ == "__main__":
    main()
