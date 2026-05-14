#!/usr/bin/env python3
"""
Audit the TypeScript capability matrix against actual test status.

The matrix at doc/typescript-capability-matrix.md cites regression
tests under regression/typescript/. Over time these can drift:
- A test referenced by the matrix may be deleted.
- A test promoted from KNOWNBUG → CORE leaves the matrix's row
  saying ❌ when reality is ✅.
- A status emoji may be inconsistent with the test's actual label
  (e.g. matrix says ✅ but test.desc says KNOWNBUG).

This script parses the matrix, walks each row's referenced tests,
loads the corresponding test.desc, and reports inconsistencies.

Usage:
    python3 scripts/audit_matrix.py
    python3 scripts/audit_matrix.py --strict     # exit 1 on any
    python3 scripts/audit_matrix.py --verbose    # show all rows

Exit codes:
    0  No errors (warnings allowed unless --strict)
    1  At least one error (or --strict and at least one warning)
    2  Could not read the matrix
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
MATRIX_PATH = REPO_ROOT / "doc" / "typescript-capability-matrix.md"
TESTS_DIR = REPO_ROOT / "regression" / "typescript"

# Status emojis used in the matrix.
SUPPORTED = "✅"
PARTIAL = "⚠️"
NOT_SUPPORTED = "❌"
LIMITED = "⏳"

ALLOWED_STATUSES = {SUPPORTED, PARTIAL, NOT_SUPPORTED, LIMITED}

# test.desc first-line labels we care about.
LABEL_CORE = "CORE"
LABEL_KNOWNBUG = "KNOWNBUG"
LABEL_FUTURE = "FUTURE"
LABEL_THOROUGH = "THOROUGH"


@dataclass
class Row:
    """One data row from a markdown table in the matrix."""

    line_no: int
    feature: str
    status: str  # the emoji; "" if missing
    notes: str
    tests: list[str]
    raw: str

    def __str__(self) -> str:
        return f"{MATRIX_PATH.name}:{self.line_no}  {self.feature}"


@dataclass
class Issue:
    severity: str  # "error" | "warn"
    row: Row
    message: str

    def format(self) -> str:
        sev = "ERROR" if self.severity == "error" else "WARN"
        return f"[{sev}] {self.row}\n        {self.message}"


def parse_matrix(matrix_text: str) -> list[Row]:
    """Extract data rows from the markdown tables.

    A data row starts with `|` and contains at least 4 cells (after
    splitting on `|`). The header row and separator (`|---|`) are
    skipped. Tests cells contain backticked names and an optional
    `[KNOWNBUG]` suffix marker (which we surface as a parse note —
    the audit's job is to compare the matrix's claim to the
    test.desc reality, not to enforce a particular tag scheme).
    """
    rows: list[Row] = []
    in_table = False
    for line_no, line in enumerate(matrix_text.splitlines(), start=1):
        s = line.rstrip()
        if not s.startswith("|"):
            in_table = False
            continue
        # Separator line, e.g. |---|---|
        if re.match(r"^\|[\s\-:|]+\|\s*$", s):
            in_table = True
            continue
        if not in_table:
            # Header or pre-header.
            continue
        cells = [c.strip() for c in _split_table_row(s)]
        if len(cells) < 4:
            continue
        feature, status, notes, tests = cells[:4]
        # Skip the header row (which often re-appears in
        # multi-table sections).
        if status.lower() == "status":
            continue
        rows.append(Row(
            line_no=line_no,
            feature=feature,
            status=status,
            notes=notes,
            tests=_extract_test_names(tests),
            raw=s,
        ))
    return rows


def _split_table_row(line: str) -> list[str]:
    """Split a markdown table row on `|`, respecting `\\|` escapes
    (used to embed literal `|` characters inside a cell, e.g.
    `\\|\\|` for the JS or-operator).
    """
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|"):
        s = s[:-1]
    cells: list[str] = []
    cur: list[str] = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s) and s[i + 1] == "|":
            cur.append("|")
            i += 2
            continue
        if c == "|":
            cells.append("".join(cur))
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    cells.append("".join(cur))
    return cells


def _extract_test_names(cell: str) -> list[str]:
    """Pull all `name`-quoted names from a markdown cell.

    Skip wildcards like `verify-*` (informational rather than a
    specific reference) and anything that wouldn't be a valid
    directory name (whitespace, slashes, dots).
    """
    raw_names = re.findall(r"`([^`]+)`", cell)
    out: list[str] = []
    for n in raw_names:
        if "*" in n or "/" in n or " " in n or "." in n:
            continue
        out.append(n)
    return out


def _read_test_label(test_dir: Path) -> Optional[str]:
    """Return the first non-empty line of test.desc (which is the
    test category label: CORE/KNOWNBUG/FUTURE/THOROUGH/etc), or
    None if the file is unreadable.
    """
    try:
        with (test_dir / "test.desc").open() as f:
            for raw in f:
                token = raw.strip()
                if token:
                    return token.split()[0]
    except FileNotFoundError:
        return None
    except OSError:
        return None
    return None


def audit_row(row: Row) -> list[Issue]:
    issues: list[Issue] = []

    # Status sanity.
    if row.status and row.status not in ALLOWED_STATUSES:
        # Strip combining variation selectors that sometimes appear
        # next to emoji.
        bare = row.status.replace("\ufe0f", "")
        if bare not in ALLOWED_STATUSES:
            issues.append(Issue(
                "warn", row,
                f"unrecognised status marker {row.status!r}; "
                f"expected one of {sorted(ALLOWED_STATUSES)}"))

    # Skip rows with no test names (often `—` or `(covered
    # implicitly in all tests)`).
    if not row.tests:
        return issues

    test_labels: dict[str, Optional[str]] = {
        name: _read_test_label(TESTS_DIR / name) for name in row.tests
    }

    # Missing tests are always errors — broken cross-reference.
    for name, label in test_labels.items():
        if label is None:
            issues.append(Issue(
                "error", row,
                f"references test {name!r} but no "
                f"regression/typescript/{name}/test.desc found"))

    # Status / label coherence.
    has_core = any(lbl == LABEL_CORE for lbl in test_labels.values())
    has_knownbug = any(
        lbl == LABEL_KNOWNBUG for lbl in test_labels.values())
    bare_status = row.status.replace("\ufe0f", "")

    if bare_status == NOT_SUPPORTED and has_core:
        # Matrix claims feature is broken, but a CORE test passes.
        # Probably a stale row that was never updated when the
        # feature was fixed.
        issues.append(Issue(
            "error", row,
            f"status {NOT_SUPPORTED} but at least one referenced "
            f"test is CORE: "
            f"{[n for n, l in test_labels.items() if l == LABEL_CORE]}"))

    if bare_status == SUPPORTED and has_knownbug and not has_core:
        # Matrix claims supported, but only KNOWNBUG tests cited.
        issues.append(Issue(
            "error", row,
            f"status {SUPPORTED} but only KNOWNBUG tests cited: "
            f"{[n for n, l in test_labels.items() if l == LABEL_KNOWNBUG]}"))

    # `[KNOWNBUG]` suffix in the test cell should match reality.
    if "[KNOWNBUG]" in row.raw:
        # Find which test name(s) carried the suffix and verify each
        # is actually KNOWNBUG.
        cell = row.raw.split("|")[4] if row.raw.count("|") >= 4 else ""
        for name in row.tests:
            # The suffix attaches to the immediately preceding name.
            pattern = re.escape("`" + name + "`") + r"\s*\[KNOWNBUG\]"
            if re.search(pattern, cell):
                lbl = test_labels.get(name)
                if lbl is not None and lbl != LABEL_KNOWNBUG:
                    issues.append(Issue(
                        "error", row,
                        f"test {name!r} is tagged [KNOWNBUG] in the "
                        f"matrix but its test.desc label is {lbl!r}"))

    return issues


def find_orphan_tests(rows: list[Row]) -> list[str]:
    """Return CORE test directory names that exist on disk but are
    not referenced by any matrix row. This is informational only —
    the matrix does not need to mention every test.
    """
    referenced: set[str] = set()
    for row in rows:
        referenced.update(row.tests)
    on_disk: list[str] = []
    if TESTS_DIR.is_dir():
        for child in sorted(TESTS_DIR.iterdir()):
            if child.is_dir() and (child / "test.desc").is_file():
                on_disk.append(child.name)
    return [name for name in on_disk if name not in referenced]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict", action="store_true",
        help="exit 1 on warnings as well as errors")
    parser.add_argument(
        "--verbose", action="store_true",
        help="print every audited row, not just issues")
    parser.add_argument(
        "--orphans", action="store_true",
        help="list CORE tests that no matrix row references "
             "(informational; not a failure)")
    args = parser.parse_args()

    if not MATRIX_PATH.is_file():
        print(f"error: matrix not found at {MATRIX_PATH}", file=sys.stderr)
        return 2

    text = MATRIX_PATH.read_text(encoding="utf-8")
    rows = parse_matrix(text)
    if args.verbose:
        print(f"Parsed {len(rows)} matrix rows from {MATRIX_PATH.name}")

    all_issues: list[Issue] = []
    for row in rows:
        all_issues.extend(audit_row(row))

    errors = [i for i in all_issues if i.severity == "error"]
    warns = [i for i in all_issues if i.severity == "warn"]

    for issue in all_issues:
        print(issue.format())

    if args.orphans:
        orphans = find_orphan_tests(rows)
        if orphans:
            print()
            print(
                f"Informational: {len(orphans)} test(s) on disk are not "
                f"referenced by the matrix:")
            for name in orphans:
                print(f"  {name}")

    print()
    print(f"Summary: {len(errors)} error(s), {len(warns)} warning(s) "
          f"across {len(rows)} matrix rows.")

    if errors:
        return 1
    if args.strict and warns:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
