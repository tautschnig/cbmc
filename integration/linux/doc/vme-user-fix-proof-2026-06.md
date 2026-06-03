# vme_user SLAVE-path OOB: fix + formal proof + dynamic re-confirmation

**Date:** 2026-06-03
**Bug:** `drivers/staging/vme_user/vme_user.c` — `buffer_from_user`
/ `buffer_to_user` copy `count` bytes into/out of the fixed
`PCI_BUF_SIZE` (128 KiB) `kern_buf` without bounding `count`
against `size_buf`.  `vme_user_write` / `vme_user_read` only clamp
`count` to the VME *window* size (`image_size =
vme_get_size(resource)`), which `VME_SET_SLAVE` sets from
user-supplied `slave.size` (validated only against the VME
address space, up to 4 GiB — not against `PCI_BUF_SIZE`).  When
the window exceeds 128 KiB, the copy overruns `kern_buf`.

Found by the CodeQL→CBMC pipeline; CBMC stage-2 verdict FAILED;
dynamically confirmed with KASAN (see
`vme-user-dynamic-poc-plan-2026-06.md` §12).

## The fix

Clamp `count` against `size_buf` in both slave-path helpers,
mirroring `resource_from_user` (the master path, which already
does this).  Patch: `poc/vme_user_oob_fix.patch`.

```c
/* Clamp to the fixed kern_buf (size_buf): the VME window
 * (image_size) may exceed PCI_BUF_SIZE, so *ppos + count can
 * run past kern_buf otherwise.
 */
if (*ppos >= image[minor].size_buf)
	return 0;
if (count > image[minor].size_buf - *ppos)
	count = image[minor].size_buf - *ppos;
```

`size_buf` is `unsigned long long`; `*ppos` is `loff_t` (signed
64-bit).  The caller guarantees `*ppos >= 0` (checked in
`vme_user_{read,write}`), and the `*ppos >= size_buf` early
return makes `size_buf - *ppos` well-defined and positive, so the
clamp is type-correct.

## Formal proof (CBMC)

`vme_user_fix.c` models the PATCHED slave path with the same
scaled constants as the original `vme_user_refine.c`, so the two
are directly comparable:

| Harness | File | Verdict |
|---------|------|---------|
| `harness_buffer_from_user` (original) | `vme_user_refine.c` | **FAILED** — `memcpy destination region writeable: FAILURE` |
| `harness_buffer_from_user` (patched) | `vme_user_fix.c` | **SUCCESSFUL** |
| `harness_buffer_to_user` (patched) | `vme_user_fix.c` | **SUCCESSFUL** |

Command:
```
cbmc vme_user_fix.c --function harness_buffer_from_user \
     --bounds-check --pointer-check
```

The patched harness adds exactly the fix's two clamps after the
caller's window clamp; CBMC proves `*ppos + count <= BUF` at the
copy, so the write stays inside `kern_buf`.

### Note on `check_preservation`

The MCP `check_preservation` tool runs `verify_property` on
**Java/JVerify** sources; these harnesses are C verified with the
`cbmc` binary, so the C-native equivalent was used: run `cbmc` on
both original and patched and confirm FAILED → SUCCESSFUL.

## Dynamic re-confirmation (KASAN)

Rebuilt `vme_user.ko` with the fix AND the non-power-of-2
diagnostic buffer (`PCI_BUF_SIZE = 0x20040`, so KASAN has a
redzone that would catch any residual overrun), then re-ran the
256 KiB-window trigger in QEMU/KVM:

* **0 KASAN reports** (vs the slab-out-of-bounds splat on the
  unfixed module).
* Trigger output: `write@0 size=0x40000: 131136` — the write is
  now clamped to `size_buf` (0x20040 = 131 136) instead of
  262 144.  Read side (`write@0x20000`) returns 64, i.e.
  `size_buf - 0x20000`.

The diagnostic `0x20040` was reverted afterwards; the committed
fix is independent of buffer size.

## Auto-harnesser limitation (honest)

Running `auto_harness.py` on the *patched* source still yields
FAILED, because its heuristic guard extractor (a) leaves `*ppos`
unconstrained and (b) reduces `count > size_buf - *ppos` to
`count > BUF` without the `- *ppos` term or the `*ppos >=
size_buf` early return.  So the generated harness copies at
`kern_buf + ppos` with nondet `ppos` → spurious OOB.  This is a
known precision gap (a false alarm on correct code), addressed by
the additive-bound / ppos-aware extension tracked as the next
harnesser improvement.  The hand-written `vme_user_fix.c` is the
authoritative proof of the fix.
