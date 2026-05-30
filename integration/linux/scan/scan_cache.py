"""Result cache for per-file CBMC scans.

The per-file scan pipeline (compile-stack + harness + cocci
instrumentation + cbmc) costs roughly 30-300 s per case, and
the result is fully determined by:

  * kernel tree + file content hash
  * function name
  * module name
  * INSTRUMENT mode (cocci shapes / 'cocci' / 'regex' / unset)

Cache hits read a JSON descriptor; misses run the scan and
write the descriptor.  This makes repeat measurements (n=500
re-runs on small detector tweaks, multi-LTS scans where many
files are identical across trees) effectively free for the
unchanged cases.

Key layout (all under one cache dir):
  <cache>/v1/<sha256[:16]>__<module>__<instr>__<func>.json

Stored fields:
  rc, stdout, stderr, runtime_s, file_hash, scanner_version

scanner_version is a coarse version tag captured by callers
that bump cleanly when a property module / cocci / harness
changes.  Bumping this invalidates all cache entries.
"""
from __future__ import annotations

import hashlib
import json
import os
import time
from dataclasses import dataclass
from pathlib import Path


CACHE_LAYOUT_VERSION = "v1"


@dataclass
class CacheHit:
    rc: int
    stdout: str
    stderr: str
    runtime_s: float
    file_hash: str
    scanner_version: str


class ScanCache:
    """Filesystem-backed cache.  Disabled when cache_dir is None."""

    def __init__(self, cache_dir: Path | None,
                 scanner_version: str = "0") -> None:
        self.cache_dir = cache_dir
        self.scanner_version = scanner_version
        if cache_dir is not None:
            (cache_dir / CACHE_LAYOUT_VERSION).mkdir(
                parents=True, exist_ok=True)

    @property
    def enabled(self) -> bool:
        return self.cache_dir is not None

    def _key_path(self, kernel_tree: str, file_path: str,
                  function: str, module: str,
                  instrument: str | None) -> tuple[str, Path] | None:
        if self.cache_dir is None:
            return None
        full = Path(kernel_tree) / file_path
        try:
            data = full.read_bytes()
        except OSError:
            return None
        file_hash = hashlib.sha256(data).hexdigest()[:32]
        instr = instrument or "none"
        # Mangle function name to be filesystem-safe.
        func_safe = function.replace("/", "_").replace(":", "_")
        mod_safe = module.replace("/", "_").replace(":", "_")
        instr_safe = instr.replace("/", "_").replace(":", "_")
        key = (f"{file_hash[:16]}__{mod_safe}"
               f"__{instr_safe}__{func_safe}")
        return file_hash, (
            self.cache_dir / CACHE_LAYOUT_VERSION
            / f"{key}.json")

    def get(self, kernel_tree: str, file_path: str,
            function: str, module: str,
            instrument: str | None) -> CacheHit | None:
        kp = self._key_path(kernel_tree, file_path,
                            function, module, instrument)
        if kp is None:
            return None
        file_hash, p = kp
        if not p.exists():
            return None
        try:
            d = json.loads(p.read_text())
        except (json.JSONDecodeError, OSError):
            return None
        # Re-validate file hash and scanner version: cached
        # entries from a different code-version are stale.
        if d.get("file_hash") != file_hash:
            return None
        if d.get("scanner_version") != self.scanner_version:
            return None
        return CacheHit(
            rc=int(d.get("rc", -1)),
            stdout=str(d.get("stdout", "")),
            stderr=str(d.get("stderr", "")),
            runtime_s=float(d.get("runtime_s", 0.0)),
            file_hash=file_hash,
            scanner_version=str(d.get("scanner_version", "")),
        )

    def put(self, kernel_tree: str, file_path: str,
            function: str, module: str,
            instrument: str | None,
            rc: int, stdout: str, stderr: str,
            runtime_s: float) -> None:
        kp = self._key_path(kernel_tree, file_path,
                            function, module, instrument)
        if kp is None:
            return
        file_hash, p = kp
        body = {
            "rc": rc,
            "stdout": stdout,
            "stderr": stderr,
            "runtime_s": runtime_s,
            "file_hash": file_hash,
            "scanner_version": self.scanner_version,
            "ts": int(time.time()),
        }
        try:
            p.write_text(json.dumps(body))
        except OSError:
            pass


def cache_from_env(scanner_version: str = "0") -> ScanCache:
    """Construct a ScanCache from the SCAN_CACHE_DIR
    environment variable.  Disabled when unset."""
    raw = os.environ.get("SCAN_CACHE_DIR")
    if not raw:
        return ScanCache(None, scanner_version=scanner_version)
    return ScanCache(Path(raw), scanner_version=scanner_version)
