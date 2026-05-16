# Phase 1 outcomes — balance-module factory + 8 new modules

Phase 1 of the CVE-survey-driven catalog expansion is landed.
This document records what was delivered, what was validated, and
what's next.

## Delivered

### Tooling

* **`integration/linux/scan/balance_module_factory.py`** —
  factory script with embedded Jinja2 templates that produces
  the nine files of a refcount-balance property module from a
  single `ModuleConfig`.  Eight built-in configs cover the
  Phase-1 priority list.

  Templates emit:
  - `properties/<module>/<module>.{h,c,cocci}`
  - `properties/<module>/{test_unit.c,run.sh,README.md}`
  - `scan/adapters/<short>_kernel_{adapter,adapter_probe,direct_harness}.c`

  Wiring snippets for `synthesise_harness.py` /
  `scan.py` / `scan-per-file.sh` are printed after generation.
  Wiring is not auto-injected because the insertion sites are
  not at predictable markers; printing is the right tradeoff.

  Running cost per new module: ~30 seconds for generation,
  ~30 seconds for the unit-test validation, ~3 minutes to
  paste the wiring into the three orchestration files.

### Modules (eight new, all factory-generated)

Ordered by CVE-mention frequency in the 2023-2026 survey:

| # | Module             | Get / Put APIs                       | CVE mentions |
|--:|--------------------|--------------------------------------|-------------:|
| 1 | `device_lifetime`  | `get_device` / `put_device`          |       82 + 8 |
| 2 | `of_node_lifetime` | `of_node_get` / `of_node_put`        |           55 |
| 3 | `inode_lifetime`   | `ihold` / `iput`                     |           50 |
| 4 | `dentry_lifetime`  | `dget` / `dput`                      |           47 |
| 5 | `fput_lifetime`    | `get_file` / `fput`                  |           34 |
| 6 | `sock_lifetime`    | `sock_hold` / `sock_put` (inline)    |       24 + 18 |
| 7 | `skb_lifetime`     | `skb_get` / `kfree_skb`              |       16 + 12 |
| 8 | `module_lifetime`  | `try_module_get` / `module_put`      |       11 + 4 |

Together: ~410 CVE descriptions across the 8,719 surveyed.
Conservatively this doubles the existing catalog's coverage in
absolute mention count.

### Catalog total

The CBMC property-module catalog now stands at sixteen:

* aead, page_provenance/scatterlist, pipe_buffer (May 2026)
* cred_lifetime, lock_state, refcount_lifetime, alloc_tag,
  kobject_lifetime (May 2026 follow-ups)
* **device_lifetime, of_node_lifetime, inode_lifetime,
  dentry_lifetime, fput_lifetime, sock_lifetime, skb_lifetime,
  module_lifetime** (this phase)

## Validation

### Per-module unit tests
All 8 modules' `properties/<module>/run.sh` regression tests
pass (`VERIFICATION SUCCESSFUL` on the four-case ghost /
`<type>_live` predicate exercise).

### Per-module direct-call harnesses
For all 8 modules:
- Vulnerable shape (`init=1`, two puts): `VERIFICATION FAILED`
  with `<put_fn>.precondition.4` (the `<type>_live(p) == 1`
  clause) firing on the second put — exit code 10.
- Fix shape (`init=2`, two puts): `VERIFICATION SUCCESSFUL` —
  exit code 0.

### Real-kernel smoke
- `drivers/base/core.c::device_link_free` produces a
  `CONTRACT VIOLATION (real candidate)` on lines 325 and 326
  (back-to-back `put_device(consumer)` / `put_device(supplier)`
  pair).  Tagged `[empty-ghost-confidence: low]` because the
  parameter is `struct device_link *` not `struct device *`
  directly — wrapper_paths gap.
- All four LTS kernels pass `smoke-all-lts.sh` with the new
  modules wired in (no infrastructure regressions).

### Existing-regressions
- `scan/run.sh` (10 cases): all pass.
- `scan/test-per-file.sh` (4 cases): all pass.
- `scan/test-per-file-mode.sh`: passes.

## Wiring footprint

Each new module added entries to four locations:

1. `scan/synthesise_harness.py::MODULE_GHOST_BOOTSTRAP` — six
   lines per module (`types`, `ghost_init_call`,
   `ghost_init_args_template`, `ghost_init_decl`,
   `forward_decls`, `wrapper_paths`).
2. `scan/scan.py::CONTRACT_FUNCTIONS` — one entry per module
   listing the contracted symbol(s).
3. `scan/scan.py::KERNEL_ADAPTERS` — one entry per module with
   adapter/probe/harness paths plus `slice_preserve` and
   `required_bodies`.
4. `scan/scan-per-file.sh::ADAPTER_STEM` and
   `CONTRACT_TARGETS` — one case branch per module.

Total wiring delta across all 8 modules: ~150 lines in
`synthesise_harness.py`, ~210 lines in `scan.py`, ~40 lines in
`scan-per-file.sh`.

## What this changes

* **Catalog coverage** doubles (in CVE-mention proxy terms)
  while the marginal cost of the next module is bounded by the
  factory.
* **Driver-core and filesystem subsystems** are now first-class
  scan targets.  The previous catalog skewed toward crypto, vfs
  pages, and refcount primitives; the eight new modules cover
  `drivers/*` (device, of_node, module), `fs/*` (inode, dentry,
  fput), and `net/*` (sock, skb).
* **Future module additions** can drop into a one-config-line
  edit in the factory rather than ~6 hours of authoring.

## What's next (Phase 2)

The CVE survey's Phase 2 list, all of which the factory does
*not* yet templatise (they need new ghost shapes):

1. **`cancel_work_before_free`** — `cancel_delayed_work_sync` /
   `del_timer_sync` / `cancel_work_sync` must run before
   `kfree` of the containing struct.  Targets the
   `cleanup_ordering` bucket (51 CVEs) plus the corresponding
   portion of `use_after_free`.

2. **`rcu_critical_section`** — track `rcu_read_lock` /
   `rcu_read_unlock` depth; flag `rcu_dereference` outside.
   Targets `rcu_misuse` (44 CVEs).

3. **`kref_lifetime`** — generic `kref_get` / `kref_put`
   predicate, the foundational primitive most type-specific
   lifetime modules wrap (kref mentions: 30 + 5 in
   2023-2026).  This one *can* go through the factory but
   the harness has to stack-allocate a `struct kref` with a
   real refcount field, so the templates need a small
   variant.

4. **`netlink_attr_validation`** — bounds and presence
   checks on `nla_data` / `nla_get_*` returns.  Targets
   `out_of_bounds` in `net/netlink/` and `net/netfilter/`.

5. **`concurrent_pointer_publish`** — pointer published to
   a shared structure before fully initialised.  Uses CBMC's
   concurrency support (slow but tractable, per user note).

6. **`bpf_helper_arg_validation`** — argument-bounds
   preconditions on BPF helpers like `bpf_skb_check_mtu`.
   Uses the BPF front-end CBMC has from other work, per
   user note.

## Wrapper-path follow-ups for Phase-1 modules

The smoke run on `drivers/base/core.c::device_link_free`
showed the expected `[empty-ghost-confidence: low]` tag when
the function operates on `link->consumer` / `link->supplier`
rather than a bare `struct device *` parameter.  Same pattern
for the other Phase-1 modules:

| Module           | Likely wrapper paths to populate |
|------------------|----------------------------------|
| device_lifetime  | `device_link → consumer`, `device_link → supplier`, `gendisk → driverfs_dev`, `cdev → kobj.parent` |
| of_node_lifetime | `device → of_node`, `device_node → parent`, `device_node → child` |
| inode_lifetime   | `dentry → d_inode`, `file → f_inode`, `kiocb → ki_filp.f_inode` |
| dentry_lifetime  | `path → dentry`, `file → f_path.dentry` |
| fput_lifetime    | `kiocb → ki_filp`, `iocb → ki_filp` |
| sock_lifetime    | `sk_buff → sk`, `request_sock → sk` |
| skb_lifetime     | (parameter type usually direct; few wrappers) |
| module_lifetime  | `kthread → owner`, `pernet_operations → owner` |

These are mechanical follow-ups that the factory could
templatise (a fourth template field in `ModuleConfig`).
Filing for the next iteration.

## Reproducing

```sh
# Generate a module from a built-in config:
./integration/linux/scan/balance_module_factory.py --module sock_lifetime

# Or write your own JSON config:
cat > /tmp/my_module.json <<'EOF'
{
  "module":            "rdma_dev_lifetime",
  "type_text":         "struct ib_device",
  "param_name":        "ibdev",
  "get_fn":            "ib_device_get",
  "put_fn":            "ib_device_put",
  "kernel_header":     "<rdma/ib_verbs.h>",
  "is_static_inline":  false,
  "static_inline_in":  "",
  "adapter_short":     "rdma_dev",
  "ghost_short":       "ib_device",
  "cve_motivation":    "RDMA core UAFs",
  "subsystem_focus":   "drivers/infiniband/*",
  "smoke_target":      "drivers/infiniband/core/device.c"
}
EOF
./integration/linux/scan/balance_module_factory.py --config /tmp/my_module.json
```

The factory writes nine files and prints the wiring snippets to
paste into the three orchestration files.

## Cross-references

- [CVE survey](cve-survey-2023-2026.md) — the prioritisation
  evidence for which 8 modules to build first.
- [`balance_module_factory.py`](../scan/balance_module_factory.py)
  — the templating script.
- [`synthesise_harness.py::MODULE_GHOST_BOOTSTRAP`](../scan/synthesise_harness.py)
  — per-file harness configuration for each module.
