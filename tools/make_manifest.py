#!/usr/bin/env python3
"""Generate manifest.json for the internet firmware update (see main/updater.h).

Usage:
  tools/make_manifest.py --version 0.7.0 \
      --base-url https://github.com/golyakoff/FireLamp_ESP32/releases/download/v0.7.0 \
      esp32=build-esp32/FireLamp.bin:firelamp_esp32_v0.7.0.bin \
      esp32c3=build-esp32c3/FireLamp.bin:firelamp_esp32c3_v0.7.0.bin \
      > manifest.json

Each build is TARGET=LOCAL_FILE:ASSET_NAME, the URL is BASE_URL/ASSET_NAME.
"""
import argparse
import hashlib
import json
import os
import re
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--version', required=True, help='firmware version, MAJOR.MINOR.PATCH')
    p.add_argument('--base-url', required=True, help='https URL of the release assets')
    p.add_argument('builds', nargs='+', help='TARGET=LOCAL_FILE:ASSET_NAME')
    args = p.parse_args()

    if not re.fullmatch(r'v?\d+\.\d+(\.\d+)?', args.version):
        p.error('version must be MAJOR.MINOR.PATCH')
    if not args.base_url.startswith('https://'):
        p.error('base URL must be https')

    builds = {}
    for b in args.builds:
        m = re.fullmatch(r'([a-z0-9]+)=(.+):([^:/]+)', b)
        if not m:
            p.error(f'invalid build "{b}", expected TARGET=LOCAL_FILE:ASSET_NAME')
        target, path, asset = m.groups()
        with open(path, 'rb') as f:
            data = f.read()
        builds[target] = {
            'url': args.base_url.rstrip('/') + '/' + asset,
            'size': len(data),
            'sha256': hashlib.sha256(data).hexdigest(),
        }

    json.dump({'version': args.version.lstrip('v'), 'builds': builds}, sys.stdout, indent=2)
    sys.stdout.write('\n')


if __name__ == '__main__':
    main()
