#!/usr/bin/env python3
"""Update the FT32 MCU over Wi-Fi through OpenBeken.

Needs an OpenBeken build with MCU OTA support (the tuyaMcu_otaStart command).
The image is uploaded to OpenBeken's LittleFS, then OpenBeken sends it to the
MCU with the Tuya MCU upgrade commands (0x0A/0x0B). The MCU checks the image
(header, CRC-32, product key), and the bootloader installs it on restart.

    tools/ota.py 192.168.0.108 build/pir-switch.bin
    make ota IP=192.168.0.108
"""
import argparse
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from http.client import HTTPConnection

LFS_NAME = "mcu.bin"
MAX_IMAGE_SIZE = 0x6FFC      # MCU staging area, see linker/FT32F030x8.ld
PRODUCT_KEY = b"urlxtrtlq9fjkv2d"
PRODUCT_KEY_OFFSET = 0x1000


def http(url, data=None, timeout=30):
    req = urllib.request.Request(url, data=data, method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode("utf-8", errors="replace")


def upload(ip, name, data, chunk=1024, rate=1536):
    """POST a file to OpenBeken's LittleFS at about `rate` bytes/s.

    OpenBeken writes uploads to flash at roughly 2.7 KB/s and keeps whatever
    arrives faster in heap; a 20 KB burst drops free heap below 25 KB, and
    OpenBeken reboots once it stays that low for 5 s.
    """
    conn = HTTPConnection(ip, timeout=60)
    conn.putrequest("POST", f"/api/lfs/{name}")
    conn.putheader("Content-Length", str(len(data)))
    conn.endheaders()
    for pos in range(0, len(data), chunk):
        conn.send(data[pos:pos + chunk])
        time.sleep(chunk / rate)
    reply = conn.getresponse().read().decode("utf-8", errors="replace")
    conn.close()
    return reply


def http_bytes(url, timeout=30):
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return resp.read()


def command(ip, cmd):
    return http(f"http://{ip}/cm?cmnd={urllib.parse.quote(cmd)}")


def ota_status(ip):
    """The 'MCU update: ...' line from OpenBeken's main page, or None."""
    page = http(f"http://{ip}/index?state=1")
    m = re.search(r"MCU update: ([^<]*)", page)
    return m.group(1).strip() if m else None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("ip", help="OpenBeken device address")
    parser.add_argument("image", help="firmware .bin (with the patched OTA header)")
    parser.add_argument("--timeout", type=int, default=180, help="seconds to wait for completion")
    args = parser.parse_args()

    image = open(args.image, "rb").read()
    if len(image) > MAX_IMAGE_SIZE:
        sys.exit(f"{args.image}: {len(image)} bytes exceeds the 0x{MAX_IMAGE_SIZE:X}-byte limit")
    if image[PRODUCT_KEY_OFFSET:PRODUCT_KEY_OFFSET + len(PRODUCT_KEY)] != PRODUCT_KEY:
        sys.exit(f"{args.image}: product key missing at offset 0x{PRODUCT_KEY_OFFSET:X}; "
                 "the MCU would reject it")

    # OpenBeken's LittleFS is 32 KB by default: with autoexec.bat there is
    # room for one image, and littlefs writes a replacement file before it
    # frees the old one, so remove any earlier image first.
    command(args.ip, f"lfs_remove {LFS_NAME}")
    print(f"uploading {args.image} ({len(image)} bytes) to {args.ip} as {LFS_NAME}")
    try:
        reply = json.loads(upload(args.ip, LFS_NAME, image))
    except OSError as e:
        reply = {"error": str(e)}
    if reply.get("size") != len(image):
        sys.exit(f"upload failed: {reply} (is OpenBeken's LittleFS big enough? see lfs_size)")
    readback = http_bytes(f"http://{args.ip}/api/lfs/{LFS_NAME}")
    if readback != image:
        sys.exit("upload failed: the file read back from OpenBeken differs from the image")

    run_update(args)


def run_update(args):
    print("starting the MCU update")
    command(args.ip, f"tuyaMcu_otaStart {LFS_NAME}")

    deadline = time.time() + args.timeout
    last = None
    while time.time() < deadline:
        time.sleep(2)
        try:
            status = ota_status(args.ip)
        except OSError as e:          # the device may be busy; keep polling
            status = f"(no answer: {e})"
        if status != last:
            print(f"  {status}")
            last = status
        if status and status.startswith(("done", "failed")):
            # Free the space; the transfer has finished with the file
            command(args.ip, f"lfs_remove {LFS_NAME}")
        if status and status.startswith("done"):
            print("update installed; the MCU restarted with the new firmware")
            return
        if status and status.startswith("failed"):
            sys.exit("update failed")
    sys.exit(f"no result after {args.timeout} s (last status: {last})")


if __name__ == "__main__":
    main()
