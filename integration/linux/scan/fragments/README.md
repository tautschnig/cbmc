# Config fragments

Each fragment in this directory is a standard Kconfig fragment — a
list of `CONFIG_FOO=y` (or `=m`, `=n`) lines — that enables the set
of kernel options required to preprocess a specific bundle of source
files.

Fragments are applied on top of `make allnoconfig` (not `defconfig`)
via `../configure.sh`, so each one should be explicit about what it
turns on.  `allnoconfig` gives us a minimal, predictable starting
state; `defconfig` drags in a full working kernel that is much
larger than we need for scanning.

## Fragments today

- `baseline.config` — minimal prerequisites (64-bit x86_64,
  multi-user, HRT).  Every scan uses this.
- `crypto-aead.config` — the AEAD userspace-crypto API
  (`crypto/algif_aead.c`, which hosts CVE-2026-31431).  Requires
  baseline.

## How to add a fragment

1. Identify the target `.c` file(s) you want to be able to
   preprocess with `goto-cc`.
2. Find the Kconfig symbol that gates them in the kernel's
   `Makefile` or `Kbuild`, e.g. `obj-$(CONFIG_FOO_BAR) += foo_bar.o`.
3. In a recent kernel tree, run
   `make menuconfig`; enable `CONFIG_FOO_BAR`; accept any
   dependencies menuconfig pulls in transitively; save; diff the
   resulting `.config` against the baseline to harvest just the
   deltas.  Those deltas are your fragment.
4. Add it here and list it in this README.

## How to apply

```sh
scan/configure.sh /path/to/linux scan/fragments/baseline.config \
                                 scan/fragments/crypto-aead.config
```

Fragments are passed as positional arguments and merged in the order
given.  The script drives `scripts/kconfig/merge_config.sh` from the
kernel tree, then `make olddefconfig`.

## What about enabling "everything"?

Some Kconfig symbols are mutually exclusive (different CPU
schedulers, different unwinders, different crypto providers, …), so
there is no universally-valid "enable everything."  `make
allyesconfig` comes close and is a reasonable option for a full
nightly sweep, but it produces a significantly larger compile than
we need for most scan targets.  The per-target fragment approach
keeps each scan lean and reproducible; the small upfront cost is
one fragment per subsystem we want to scan, which amortises well
over the life of the tool.
