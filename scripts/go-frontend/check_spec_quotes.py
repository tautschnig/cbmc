#!/usr/bin/env python3
"""Check that quotations of the Go specification are verbatim.

The Go front-end cites "The Go Programming Language Specification",
go1.27, as `Go1.27 §<anchor>` (doc/architectural/go-frontend-plan.md).
This script checks, for every `//` comment block in the given files (by
default src/go and regression/go), that

  * each "double-quoted" phrase of at least MIN_LENGTH characters occurs
    in the specification or the Go Memory Model, after removing HTML
    markup and collapsing white space (an ellipsis, "..." or "…", may
    elide text between parts), and
  * each `§Anchor` names an existing section of the specification.

Preformatted comment lines and `code spans` are ignored. Other quoted
phrases that are not spec quotations can be listed in ALLOWED below.

Usage: check_spec_quotes.py [--spec-dir DIR] [FILE...]
DIR must contain go_spec.html and go_mem.html from the pinned Go release
(default: ~/go-spec), e.g. fetched with

  curl -O https://raw.githubusercontent.com/golang/go/go1.27.3/doc/go_spec.html
  curl -O https://raw.githubusercontent.com/golang/go/go1.27.3/doc/go_mem.html

and checked against the sha256 sums in doc/architectural/go-frontend-plan.md.
Exits 1 if anything is flagged.
"""

import argparse
import html
import pathlib
import re
import sys

MIN_LENGTH = 20

# Quoted strings in comments that are not quotations of the specification.
ALLOWED = {
    "file:line:col: message",
}


def normalise_html(text):
    text = re.sub(r'<[^>]+>', '', text)
    return re.sub(r'\s+', ' ', html.unescape(text))


def comment_blocks(path):
    """Yield (first line number, text) of each run of // comment lines.

    Preformatted lines (a tab after //, as in Go doc comments) end a block
    and are skipped, as are `code spans`."""
    block, start = [], 0
    lines = path.read_text(errors='replace').splitlines()
    for number, line in enumerate(lines + [''], 1):
        match = re.match(r'\s*//+ ?(?!\t)(.*)$', line)
        if match:
            match = re.sub(r'`[^`]*`', '`code`', match.group(1))
            if not block:
                start = number
            block.append(match)
        elif block:
            yield start, ' '.join(block)
            block = []


def default_files(root):
    files = []
    for directory in ('src/go', 'regression/go'):
        for path in sorted((root / directory).rglob('*')):
            if (path.suffix in ('.go', '.cpp', '.h')
                    and 'native' not in path.parts):
                files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--spec-dir', type=pathlib.Path,
                        default=pathlib.Path.home() / 'go-spec')
    parser.add_argument('files', nargs='*', type=pathlib.Path)
    args = parser.parse_args()

    spec_html = (args.spec_dir / 'go_spec.html').read_text()
    corpus = normalise_html(spec_html) + ' ' + normalise_html(
        (args.spec_dir / 'go_mem.html').read_text())
    anchors = set(re.findall(r'id="([^"]+)"', spec_html))

    root = pathlib.Path(__file__).resolve().parents[2]
    files = args.files or default_files(root)

    quotes = flagged = 0
    for path in files:
        for line, block in comment_blocks(path):
            # Spec anchors start with a capital letter; §4.3 etc. are
            # sections of the plan document.
            for anchor in re.findall(r'§([A-Z][A-Za-z0-9_.]*[A-Za-z0-9_])',
                                     block):
                if anchor not in anchors:
                    flagged += 1
                    print(f'{path}:{line}: unknown section §{anchor}')
            # Pair quotation marks in order, then keep the long phrases.
            for quote in re.findall(r'"([^"]*)"', block):
                quote = re.sub(r'\s+', ' ', quote).strip()
                if len(quote) < MIN_LENGTH or quote in ALLOWED:
                    continue
                quotes += 1
                parts = [p.strip() for p in re.split(r'\.\.\.|…', quote)
                         if p.strip()]
                if not all(p in corpus for p in parts):
                    flagged += 1
                    print(f'{path}:{line}: not verbatim: "{quote}"')
    print(f'{quotes} quotations checked, {flagged} problems')
    return 1 if flagged else 0


if __name__ == '__main__':
    sys.exit(main())
