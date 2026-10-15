// SPDX-License-Identifier: GPL-2.0-only
/*
 * bpf_localnet_stub - pin the BPF map that Android 16's Connectivity module expects on 4.9 kernels.
 *
 * Why: NetBpfLoad skips BPF_MAP_TYPE_LPM_TRIE maps on kernels < 4.14, so
 * /sys/fs/bpf/netd_shared/map_netd_local_net_access_map is never created. BpfNetMaps.initBpfMaps() then throws
 * before it assigns sLocalNetBlockedUidMap, the field stays null, and every PACKAGE_REMOVED broadcast (app
 * update/uninstall) crashes system_server with a NullPointerException (framework "soft reboot").
 *
 * What: create a plain HASH map with the same key/value sizes as the LPM trie
 *   key  = LocalNetAccessKey {u32 lpm_bitlen; u32 if_index; in6_addr remote_ip6; u16 protocol; u16 remote_port} = 28 bytes
 *   value = bool = 1 byte, max_entries 1000
 * and pin it with the owner/mode the loader uses for the other netd maps. The local-network access feature itself
 * stays inactive (nothing matches), which is the same as on any pre-4.14 kernel.
 *
 * Freestanding aarch64 Linux program (raw syscalls, no libc). Run it as the bpfloader domain right after the loader.
 */
#define NR_bpf        280
#define NR_fchmodat   53
#define NR_fchownat   54
#define NR_write      64
#define NR_exit_group 94
#define AT_FDCWD      (-100)
#define EEXIST        17
#define BPF_MAP_CREATE 0
#define BPF_OBJ_PIN    6
#define BPF_MAP_TYPE_HASH 1
#define AID_NET_BW_ACCT 3007

static const char PATH[] = "/sys/fs/bpf/netd_shared/map_netd_local_net_access_map";

static long sys5(long n, long a, long b, long c, long d, long e)
{
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return x0;
}

static void say(const char *s)
{
    long n = 0;
    while (s[n]) n++;
    sys5(NR_write, 2, (long)s, n, 0, 0);
}

static __attribute__((noreturn)) void quit(long code)
{
    for (;;) sys5(NR_exit_group, code, 0, 0, 0, 0);
}

union attr {
    struct { unsigned map_type, key_size, value_size, max_entries, map_flags; } create;
    struct { unsigned long long pathname; unsigned bpf_fd; } pin;
    char raw[128];                      /* the kernel requires the unused tail to be zero */
};

void _start(void)
{
    union attr a;
    char *p = a.raw;
    for (int i = 0; i < (int)sizeof a; i++) p[i] = 0;
    a.create.map_type = BPF_MAP_TYPE_HASH;
    a.create.key_size = 28;
    a.create.value_size = 1;
    a.create.max_entries = 1000;
    long fd = sys5(NR_bpf, BPF_MAP_CREATE, (long)&a, sizeof a, 0, 0);
    if (fd < 0) { say("bpf_localnet_stub: BPF_MAP_CREATE failed\n"); quit(1); }

    for (int i = 0; i < (int)sizeof a; i++) p[i] = 0;
    a.pin.pathname = (unsigned long long)(unsigned long)PATH;
    a.pin.bpf_fd = (unsigned)fd;
    long r = sys5(NR_bpf, BPF_OBJ_PIN, (long)&a, sizeof a, 0, 0);
    if (r == -EEXIST) { say("bpf_localnet_stub: map already pinned\n"); quit(0); }
    if (r < 0) { say("bpf_localnet_stub: BPF_OBJ_PIN failed\n"); quit(2); }

    sys5(NR_fchownat, AT_FDCWD, (long)PATH, 0, AID_NET_BW_ACCT, 0);
    sys5(NR_fchmodat, AT_FDCWD, (long)PATH, 0660, 0, 0);
    say("bpf_localnet_stub: pinned local_net_access_map\n");
    quit(0);
}
