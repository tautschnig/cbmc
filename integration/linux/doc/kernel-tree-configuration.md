# Kernel-tree configuration prerequisite

`integration/linux/scan/compile_file.sh` requires the kernel
tree to have been pre-configured.  Historically we used
`allnoconfig` for minimum surface area, but that disables
many subsystem `CONFIG_*` flags (REGMAP, IOMMU, etc.) which
gate the corresponding struct definitions in headers.  Files
in those subsystems then fail to compile with errors like
"array has incomplete element type".

**Recommended setup (since 2026-05):**

```sh
cd /path/to/linux_tree
make ARCH=x86_64 defconfig
make ARCH=x86_64 prepare0
```

`defconfig` enables the standard set of x86_64 distro subsystems
(roughly 1,400 `CONFIG_*` defines vs. ~380 with `allnoconfig`),
which dramatically reduces the compile-error rate on the
n=200 / n=500 random-function FP measurement.

`prepare0` is the lighter-weight alternative to
`prepare scripts` that builds only the generated headers we
need (`autoconf.h`, `compile.h`, etc.) without the userspace
tools that fail to build cleanly under sandbox constraints.

**Migration:** existing trees configured with `allnoconfig`
should be reconfigured.  Doing so is non-destructive (no
files outside the tree are modified) and the compile
infrastructure is unchanged.
