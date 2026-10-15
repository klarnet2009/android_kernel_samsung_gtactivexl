#!/usr/bin/env python3
"""Pack a kernel Image.gz-dtb into a boot.img that has the stock SM-T545 header values.

usage: STOCK_BOOT=<stock boot.img> MKBOOTIMG=<path to AOSP mkbootimg.py> \
       [EXTRA="androidboot.selinux=permissive"] mkboot.py <Image.gz-dtb> <out.img>

The stock boot.img (from your own firmware) supplies cmdline, board name and header layout; the result is
checked to be header-identical to it (except kernel size, id and, with EXTRA, the cmdline) and padded to the
64 MiB boot partition. Without EXTRA the kernel boots with the stock cmdline (SELinux enforcing).
"""
import os
import subprocess
import sys

STOCK = os.environ["STOCK_BOOT"]
MKBOOTIMG = os.environ.get("MKBOOTIMG", "mkbootimg.py")
BOOT_PARTITION = 64 * 1024 * 1024

stock = open(STOCK, "rb").read()
kernel, out = sys.argv[1], sys.argv[2]
extra = os.environ.get("EXTRA", "").strip()

cmd = (stock[0x40:0x40 + 512].split(b"\0")[0] + stock[0x260:0x260 + 1024].split(b"\0")[0]).decode("latin-1")
if extra:
    cmd += " " + extra
board = stock[0x30:0x40].split(b"\0")[0].decode("latin-1")
print("cmdline:", cmd)
print("board:", board)

subprocess.check_call([
    "python3", MKBOOTIMG,
    "--kernel", kernel, "--cmdline", cmd, "--base", "0x0", "--kernel_offset", "0x8000",
    "--ramdisk_offset", "0x0", "--second_offset", "0x0", "--tags_offset", "0x1e00000",
    "--pagesize", "4096", "--header_version", "1", "--os_version", "11.0.0",
    "--os_patch_level", "2023-10", "--board", board, "-o", out,
])

new = open(out, "rb").read()
a, b = bytearray(stock[:1648]), bytearray(new[:1648])
ignore = [(8, 12), (0x240, 0x260)]          # kernel_size, id
if extra:
    ignore += [(0x40, 0x40 + 512), (0x260, 0x260 + 1024)]   # cmdline differs on purpose
for lo, hi in ignore:
    a[lo:hi] = b"\0" * (hi - lo)
    b[lo:hi] = b"\0" * (hi - lo)
same = a == b
print("header identical to stock (ignoring kernel_size/id%s): %s" % ("/cmdline" if extra else "", same))
if not same:
    sys.exit(1)

if os.path.getsize(out) > BOOT_PARTITION:
    sys.exit("boot.img is larger than the boot partition")
with open(out, "r+b") as fh:
    fh.truncate(BOOT_PARTITION)
print("padded to", os.path.getsize(out), "bytes")
