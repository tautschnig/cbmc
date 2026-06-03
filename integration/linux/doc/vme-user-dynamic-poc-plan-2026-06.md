# End-to-end dynamic PoC plan for a tainted-size → OOB candidate

**Date:** 2026-06-03
**Worked example:** the `vme_user` SLAVE-path OOB surfaced by
the CodeQL→CBMC pipeline (`abc-codeql-cbmc-vme-user-2026-06.md`).
**Purpose:** turn a *static* refinement-confirmed candidate into
a *dynamic* confirmation (or refutation) with a real kernel and
a memory-safety oracle, and define a reusable recipe for any
tainted-size candidate.

## 0. Oracle

**KASAN** (`CONFIG_KASAN=y`, generic mode).  A genuine OOB write
into a kmalloc'd buffer produces a
`BUG: KASAN: slab-out-of-bounds in <fn>` report with a stack
trace.  That report (naming `_copy_from_user`/`buffer_from_user`
/`vme_user_write`) is the confirmation; absence of a report on a
well-formed trigger is evidence the candidate is a false alarm.

## 1. Why this is feasible with no hardware

* `drivers/staging/vme_user` binds to **any** registered VME
  bridge.  The in-tree **`vme_fake`** bridge (`CONFIG_VME_FAKE`)
  registers a software bridge with master/slave windows — no VME
  hardware required.
* Critically, `vme_fake`'s `fake_alloc_consistent` is a plain
  **`kmalloc(size, GFP_KERNEL)`** (vme_fake.c:995).  So the
  SLAVE `kern_buf` (the fixed `PCI_BUF_SIZE` = 128 KiB buffer)
  is a **slab object KASAN instruments** — an OOB write past it
  is catchable.  (The MASTER buffer is also `kmalloc`, but that
  path is clamped to `size_buf` and is safe.)

This is the rare staging driver whose bug is dynamically
reachable in a pure VM.

## 2. Environment — local QEMU/KVM (primary)

The build host has `/dev/kvm` and nested virt enabled, 96 cores
and 743 GiB RAM.  A local QEMU/KVM guest is **free, fast, and
fully reversible**, so it is preferred over EC2.

* `apt-get install qemu-system-x86` (or a static qemu) — the
  only host change required.
* Boot a freshly-built `bzImage` + a BusyBox initramfs directly
  with `-kernel`/`-initrd` (no disk image, no bootloader).
* Serial console to stdio (`console=ttyS0`), `-nographic`,
  `-no-reboot`, short boot.  `panic_on_warn=0` so KASAN reports
  print without an immediate reset.

### EC2 fallback (only if local KVM is unusable)
A dedicated **bare-metal** instance (e.g. `m5.metal`/`c5.metal`)
or any instance with nested-virt, in our account/region, running
the same QEMU recipe.  Rationale to avoid unless needed: billable,
slower to iterate, and offers nothing the local KVM lacks here.
If used: launch, `scp` the bzImage+initramfs, run QEMU over SSH,
**terminate when done** (record instance-id; do not leave running).

## 3. Kernel build (6.12 LTS)

Tree: `/home/ubuntu/linux_6_12` (the tree the CodeQL DB was built
from, so source lines match).  Out-of-tree build dir to keep the
source clean: `make O=/tmp/kbuild-kasan`.

Config = `defconfig` + this fragment (`merge_config.sh` or
`scripts/config`):

```
CONFIG_KASAN=y
CONFIG_KASAN_GENERIC=y
CONFIG_KASAN_OUTLINE=y          # smaller image, fine for PoC
CONFIG_STAGING=y
CONFIG_VME_BUS=y                # core, built-in
CONFIG_VME_FAKE=y               # fake bridge, built-in
CONFIG_VME_USER=y               # driver, built-in (no module load needed)
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y         # /dev nodes auto-created
CONFIG_BLK_DEV_INITRD=y
CONFIG_SERIAL_8250=y
CONFIG_SERIAL_8250_CONSOLE=y
CONFIG_PANIC_ON_OOPS=n
# WERROR off (gcc 11 on 6.12), KCSAN/UBSAN off (not needed)
```

Build everything **built-in** (`=y`) so there is no module-load
step in the guest.  `make -j96 bzImage`.

## 4. Rootfs (BusyBox initramfs)

Minimal initramfs (cpio.gz), no disk:

* static **BusyBox** (`busybox-static` from apt, or prebuilt) at
  `/bin/busybox` with the usual symlinks, `/init` shell script.
* `/init`: mount `proc`, `sysfs`, `devtmpfs` on `/dev`, then run
  the trigger, then `poweroff -f`.
* the **trigger binary** (below), static, at `/trigger`.

`/init` sketch:
```sh
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev
echo "=== running trigger ==="
/trigger
echo "=== trigger done ==="
poweroff -f
```

Pack: `find . | cpio -o -H newc | gzip > initramfs.cpio.gz`.

## 5. Trigger program

Static C program (`gcc -static`).  Steps, matching the
`buffer_from_user` path:

1. Find the SLAVE char device.  `vme_user` registers a class and
   devices; with devtmpfs they appear as `/dev/bus/vme/s0…s3`
   (slave) and `m0…m3` (master), plus `ctl`.  (Exact node names
   to be confirmed in-guest via `ls /dev/bus/vme`.)
2. `open("/dev/bus/vme/s0", O_RDWR)`.
3. `ioctl(fd, VME_SET_SLAVE, &slave)` with
   `slave.enable = 1`, `slave.vme_addr = 0`,
   `slave.size = 0x40000` (256 KiB > PCI_BUF_SIZE 128 KiB),
   `slave.aspace = VME_A32`, `slave.cycle = VME_SCT`.
   (`struct vme_slave` and `VME_SET_SLAVE` from
   `drivers/staging/vme_user/vme_user.h`.)
4. `write(fd, buf, 0x40000)` of a 256 KiB userspace buffer —
   `vme_user_write` clamps `count` to `image_size - *ppos`
   (= 256 KiB), `buffer_from_user` does
   `copy_from_user(kern_buf + 0, buf, 256 KiB)` into the 128 KiB
   `kern_buf` → **128 KiB OOB write** → KASAN.

Caveats to resolve in-guest:
* `vme_check_window`/`fake_slave_set` must accept `size=0x40000`
  (the fake bridge advertises A32 windows; expected fine, but if
  it caps the window we pick the largest accepted size that
  still exceeds 128 KiB).
* If a single `write` is internally chunked, use `lseek` +
  smaller writes to reach offsets ≥128 KiB; the OOB is the same.

## 6. Run & capture

```
qemu-system-x86_64 -enable-kvm -m 2G -nographic -no-reboot \
  -kernel /tmp/kbuild-kasan/arch/x86/boot/bzImage \
  -initrd initramfs.cpio.gz \
  -append "console=ttyS0 panic_on_warn=0 oops=panic=0"
```

Capture serial to a log.  **Success oracle:** a
`BUG: KASAN: slab-out-of-bounds` block whose call trace contains
`buffer_from_user`/`vme_user_write`/`_copy_from_user`, with
"Write of size … to addr …" past the 128 KiB object.  Save the
full splat.

## 7. Honest outcomes

* **KASAN fires on the SLAVE path** → the candidate is a
  **confirmed** OOB (subject to the privilege/HW caveats already
  documented).  Then: check the CVE record for prior disclosure;
  if undisclosed, prepare a maintainer report with the splat and
  the trigger.
* **No KASAN, clean run** → the static refinement was a false
  alarm (e.g. the fake bridge clamps the window, or another guard
  exists) → record the refutation; the pipeline still worked, the
  candidate was simply over-approximate.
* **Kernel oops/corruption without a clean KASAN line** → still a
  memory-safety violation; capture and analyze.

## 8. Generalization (reusable recipe for any tainted-size candidate)

1. **Config** the subsystem of the candidate built-in, plus
   KASAN, plus whatever software backend lets it run without
   hardware (a `*_fake`/`dummy`/`vfio`/`null` backend, or a
   loopback/netlink/socket path — many subsystems have one).
2. **Trigger** = the syscall sequence reaching the sink with the
   tainted value oversized (ioctl/write/sendmsg/setsockopt). The
   CodeQL candidate already names the entry function and the
   tainted parameter; map it to the syscall.
3. **Oracle** = KASAN (slab/page OOB), or KFENCE/UBSAN for
   specific shapes.
4. **Harvest**: when KASAN fires, the splat + trigger is a
   ready-made reproducer for a maintainer report.

Subsystems *without* a software backend (need real hardware) are
out of scope for the pure-VM PoC; those stop at static
refinement + maintainer review.

## 9. Risks / time budget

* Kernel build ~10–20 min on 96 cores; initramfs/trigger minutes;
  QEMU boot seconds.  Main risk is config/device-node/ioctl
  detail iteration — budget a few cycles.
* If CBMC's `vme_fake` window cap blocks an oversized slave
  window, fall back to the MASTER-buffer analysis or pick a
  candidate whose backend imposes no cap.

## 10. Status checklist

- [ ] qemu installed
- [ ] KASAN kernel built (bzImage)
- [ ] initramfs + static busybox + trigger built
- [ ] booted; `/dev/bus/vme` enumerated
- [ ] trigger run; serial captured
- [ ] KASAN verdict recorded (fire / clean / oops)
