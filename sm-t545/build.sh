#!/bin/bash
# Build the SM-T545 "clean" kernel (no DEFEX / KDP / RKP / UH).
#
#   CLANG=<clang binary> GCC=<prefix of GCC 4.9 aarch64 binutils, ending in "aarch64-linux-android-"> \
#   [O=<out dir>] [J=<jobs>] [TARGETS=Image.gz-dtb] sm-t545/build.sh
#
# Tested with AOSP clang r377782d (10.0.6) and r547379 (20.0.0) and GNU make 4.2.1
# (make >= 4.4 fails in techpack/audio). Output: $O/arch/arm64/boot/Image.gz-dtb
set -e
K=$(cd "$(dirname "$0")/.." && pwd)
O=${O:-$K/../out-sm-t545}; J=${J:-$(nproc)}; TARGETS=${TARGETS:-Image.gz-dtb}
: "${CLANG:?set CLANG to the clang binary}"; : "${GCC:?set GCC to the aarch64-linux-android- prefix}"
DEFC=sdm670_sec_gtactivexl_eur_open_defconfig

case "$(make --version | head -1)" in
  *"GNU Make 4.2"*) ;;
  *) echo "warning: GNU make 4.2.x is recommended (4.4 fails in techpack/audio)" >&2 ;;
esac

OPTS=(
  --set-str LOCALVERSION -23809241      # must equal the stock version string: vendor modules check vermagic
  --disable MODULE_SIG_FORCE
  --disable SECURITY_DEFEX              # Samsung DEFEX (exec/safeplace policy that kills unknown binaries)
  --disable UH_LKMAUTH --disable UH_LKM_BLOCK --disable RKP_DMAP_PROT --disable RKP_NS_PROT
  --disable RKP_KDP --disable UH_RKP --disable UH     # hypervisor-backed kernel data protection
  --enable BLK_CGROUP                   # Android 16 needs the blkio cgroup controller
  --enable CRYPTO_LZ4 --enable CRYPTO_LZ4HC           # zram can use lz4
  --disable IPC_LOGGING --disable QCOM_RTB --disable SCHEDSTATS
  --disable DRM_SDE_EVTLOG_DEBUG --disable MSM_SDE_ROTATOR_EVTLOG_DEBUG
)
# Do NOT disable SEC_DEBUG_SCHED_LOG: drivers/debug/sec_debug_user_reset.c then fails to build.

CLANG_MAJOR=$("$CLANG" --version | sed -n 's/.*clang version \([0-9]*\)\..*/\1/p' | head -1)
if [ "${CLANG_MAJOR:-0}" -ge 14 ]; then
  # newer clang looks up GNU binutils by the triple name only, GNU as rejects DWARF5 (".file 0"),
  # and the 2017-era vendor code trips warnings that newer clang treats as errors
  SHIM=$O/crossbin; mkdir -p "$SHIM"
  for t in as ld ld.bfd ar nm objcopy objdump strip; do ln -sf "${GCC}$t" "$SHIM/aarch64-linux-gnu-$t"; done
  export PATH=$SHIM:$PATH KCFLAGS="${KCFLAGS:--Wno-error -Wno-strict-prototypes}"
  OPTS+=(--disable DEBUG_INFO)
fi

M=(make -C "$K" O="$O" DTC_EXT="$K/tools/dtc" CONFIG_BUILD_ARM64_DT_OVERLAY=y ARCH=arm64
    CROSS_COMPILE="$GCC" REAL_CC="$CLANG" CLANG_TRIPLE=aarch64-linux-gnu-
    HOSTCFLAGS=-fcommon HOSTCXXFLAGS=-fcommon)
mkdir -p "$O"
"${M[@]}" "$DEFC"
"$K/scripts/config" --file "$O/.config" "${OPTS[@]}"
"${M[@]}" olddefconfig
"${M[@]}" -j"$J" $TARGETS
echo "done: $O/arch/arm64/boot/Image.gz-dtb"
