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
| Android 16 BPF map stub | `android16/bpf_localnet_stub.c` + init rc: fixes `system_server` crashing on every app update/uninstall (see below) |

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

## Android 16 on this kernel: required userspace fix (BPF map stub)

On a 4.9 kernel the Android 16 Connectivity module cannot create its `local_net_access_map` (`BPF_MAP_TYPE_LPM_TRIE`; the
loader skips it below kernel 4.14 and this tree has no `lpm_trie.c`). `BpfNetMaps.initBpfMaps()` then aborts before it assigns
`sLocalNetBlockedUidMap`, and **every app update or uninstall** (`PACKAGE_REMOVED`) kills `system_server` with a
`NullPointerException` in `BpfNetMaps.removeUidFromLocalNetBlockMap` - the UI "soft reboots" (black screen, boot animation again),
repeatedly while Play Store updates its apps after the first setup.

`android16/bpf_localnet_stub.c` is a 2 KB freestanding aarch64 program that pins a same-sized HASH map
(key 28 B, value 1 B, 1000 entries) at `/sys/fs/bpf/netd_shared/map_netd_local_net_access_map` (`root:net_bw_acct`, `0660`).
The local-network access feature stays inactive, exactly as on any pre-4.14 kernel.

```sh
clang --target=aarch64-linux-gnu -O2 -static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -fno-pic       -fuse-ld=lld -Wl,-e,_start -Wl,--build-id=none -Wl,-z,norelro -o bpf_localnet_stub android16/bpf_localnet_stub.c
# install into the system image: /system/bin/bpf_localnet_stub  (mode 0755, SELinux label u:object_r:bpfloader_exec:s0)
#                                /system/etc/init/zz_bpf_localnet.rc  (android16/zz_bpf_localnet.rc)
```

Two things matter: the init service must run in the **`bpfloader` SELinux domain** (the rc does `seclabel u:r:bpfloader:s0`;
a BPF map takes the label of its creator, so a map created from `adb root`/`su` is denied to `system_server`), and it must run
right after the loader (`on load-bpf-programs` in the rc, which is processed after init.rc's own `exec_start bpfloader`).
Check: `dmesg | grep bpf_localnet_stub` shows the service started in `u:r:bpfloader:s0`, `ls -lZ /sys/fs/bpf/netd_shared/map_netd_local_net_access_map` exists,
and `am broadcast -a android.intent.action.PACKAGE_REMOVED -d package:x --ei android.intent.extra.UID 1019999 --user 0` no longer crashes `system_server`.

## Known limits

* Not hand-tested: S Pen, charging, USB/MTP, SIM/calls, headset jack. Harmless leftover denials under enforcing:
  fs-verity ioctl probes (unsupported on 4.9), a debug console start.
* Samsung's `FIVE` firmware blob (`firmware/five/five.mbn`) is part of the original drop and kept as is.
* Pre-built boot images (clang 20, permissive and enforcing) are attached to the GitHub release.
