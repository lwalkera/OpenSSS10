#!/usr/bin/env python3
"""Install the SSS10 web app (openbeken/webapp) on an OpenBeken device.

The app is served from the device's own LittleFS: startup.js as it is, and
app.js gzipped as app.js.gz (OpenBeken serves .gz files with
Content-Encoding: gzip). With --set-root, OpenBeken's WebApp URL is pointed
at /api/lfs/, so "Launch Web Application" (/app) opens it.

    tools/webapp.py 192.168.0.108 --set-root
    make webapp IP=192.168.0.108
"""
import argparse
import gzip
import json
import os
import sys
import urllib.parse

from ota import http, http_bytes, upload

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "openbeken", "webapp")
ROOT = "/api/lfs/"


def files():
    with open(os.path.join(SRC, "startup.js"), "rb") as f:
        startup = f.read()
    with open(os.path.join(SRC, "app.js"), "rb") as f:
        app = gzip.compress(f.read(), compresslevel=9, mtime=0)
    return [("startup.js", startup), ("app.js.gz", app)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("ip", help="OpenBeken device address")
    parser.add_argument("--set-root", action="store_true",
                        help=f"set OpenBeken's WebApp URL to {ROOT} (the app on the device)")
    args = parser.parse_args()

    for name, data in files():
        print(f"uploading {name} ({len(data)} bytes)", flush=True)
        upload(args.ip, name, data)
        if http_bytes(f"http://{args.ip}/api/lfs/{name}") != data:
            sys.exit(f"{name}: read-back differs from what was uploaded")

    listing = json.loads(http(f"http://{args.ip}/api/lfs/"))
    used = sum(f["size"] for f in listing["content"] if f["type"] == 1)
    print(f"LittleFS files now hold {used} bytes")

    if args.set_root:
        http(f"http://{args.ip}/cfg_webapp_set?url={urllib.parse.quote(ROOT, safe='')}")
    info = json.loads(http(f"http://{args.ip}/api/info"))
    print(f"WebApp URL: {info['webapp']}")
    if info["webapp"] == ROOT:
        print(f"open http://{args.ip}/app")
    else:
        print(f"the device still loads the app from {info['webapp']}; rerun with --set-root to use this one")


if __name__ == "__main__":
    main()
