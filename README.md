# android_kernel_samsung_gtactivexl

Linux **4.9.227** kernel for the **Samsung Galaxy Tab Active Pro LTE (SM-T545, `gtactivexl`, Snapdragon 670)**,
modified so that it boots **without Samsung's protection layers** (DEFEX, KDP/RKP, UH hypervisor hooks).
That lets a Generic System Image / LineageOS (Android 11 … **16**) run on top of the tablet's stock Android 11 vendor partition.

Verified on a real SM-T545: **LineageOS 23 (Android 16) + MindTheGapps**, SELinux **enforcing**, Wi-Fi, Bluetooth, camera,
audio, GPS, NFC (service up), sensors, Play Store.

> **Warning.** Flashing this trips Knox (the e-fuse is permanent), voids the warranty and can brick the device.
> It requires an unlocked bootloader and Download mode. No warranty of any kind; you do this at your own risk.
> This project is not affiliated with Samsung, Qualcomm, Google or LineageOS.

## Where the source comes from

`Kernel.tar.gz` from `SM-T545_EUR_RR_Opensource.zip` (Samsung open source release, GPL source request),
sha256 `9131bdebc8f90844d4c85bc59377154011434b68921ed11041443ba7623ba735`. The kernel is GPLv2 (`COPYING`).
The first commit of this repository is that drop **unmodified**; every change is a separate commit:

| Commit | What |
|---|---|
| Import Samsung SM-T545 EUR RR open source kernel | pristine Samsung tree |
| Convert CRLF line endings to LF in 37 files | build fix (CRLF breaks Kbuild/Makefiles); content unchanged |
| Makefile: build with clang directly | no `gcc-wrapper.py`; `cc-disable-warning` for `-Wno-undefined-optimized` |
| cgroup: accept the `cpuset_v2_mode` mount option | required for Android 12+; without it Android 16 never gets its cpuset hierarchy |
| tooling | `sm-t545/build.sh`, `sm-t545/mkboot.py`, this README |

The "clean" configuration is **not** a new defconfig: `sm-t545/build.sh` takes Samsung's
`sdm670_sec_gtactivexl_eur_open_defconfig` and applies a short option list with `scripts/config`
(DEFEX/KDP/RKP/UH off, `BLK_CGROUP` on, lz4 crypto on, a few debug options off, `LOCALVERSION=-23809241`
to match the stock vermagic).

## Build

You need AOSP prebuilts: GCC 4.9 for aarch64 (binutils) and clang, plus **GNU make 4.2.x**.

```sh
CLANG=/path/to/clang-r377782d/bin/clang \
GCC=/path/to/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
O=$PWD/../out J=8 sm-t545/build.sh
# -> ../out/arch/arm64/boot/Image.gz-dtb
```

* clang **10.0.6** (`r377782d`) and **20.0.0** (`r547379`) both build and boot. With clang >= 14 the script sets up
  the binutils name shims, `--disable DEBUG_INFO` (old GNU `as` rejects DWARF 5) and `KCFLAGS=-Wno-error -Wno-strict-prototypes`.
  On a pinned-frequency micro benchmark the two compilers were within noise of each other.
* Do **not** turn off `SEC_DEBUG_SCHED_LOG` (build error in `sec_debug_user_reset.c`).
* LTO exists in the tree (`LTO_CLANG`) but links with GNU gold + `LLVMgold.so`, which only older clang ships. Untested here.

## Make a boot.img and flash it

```sh
STOCK_BOOT=stock_boot.img MKBOOTIMG=/path/to/aosp/system/tools/mkbootimg/mkbootimg.py \
  python3 sm-t545/mkboot.py ../out/arch/arm64/boot/Image.gz-dtb boot.img
# add EXTRA="androidboot.selinux=permissive" to boot permissive while you collect denials
tar --format=gnu -cf BOOT.tar boot.img        # Odin / Heimdall-style flash in Download mode
```

`stock_boot.img` is the boot image of your own firmware (it supplies the header values). The script checks that the
header is identical to it and pads the image to the 64 MiB boot partition.

The Android side (system image, `vbmeta` with verification disabled, SELinux rules for the Android 11 vendor on an
Android 16 system) is outside this repository.

## Known limits

* Not hand-tested: S Pen, charging, USB/MTP, SIM/calls, headset jack. Harmless leftover denials under enforcing:
  fs-verity ioctl probes (unsupported on 4.9), a debug console start.
* Samsung's `FIVE` firmware blob (`firmware/five/five.mbn`) is part of the original drop and kept as is.
* Pre-built boot images (clang 20, permissive and enforcing) are attached to the GitHub release.
