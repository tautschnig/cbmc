#!/usr/bin/env python3
"""Count the string operations that shell scripts use.

Parses each script listed in a corpus file with shfmt --to-json and counts,
per operation, how many occurrences there are and in how many scripts they
appear. The operations are those a string representation must support:
comparisons in test, case patterns (literal or glob), concatenation, length,
pattern removal, substring and other non-POSIX expansions, conversions
between strings and integers, and sources of unknown strings (command
substitution, read, positional parameters).

Usage: string_ops_census.py CORPUS_FILE [SHFMT]
CORPUS_FILE lists one script per line.
"""

import collections
import json
import subprocess
import sys

TEST_STRING_OPS = {'=', '==', '!='}
TEST_INT_OPS = {'-eq', '-ne', '-lt', '-le', '-gt', '-ge'}
TEST_UNARY_STRING = {'-n', '-z'}
TEST_ORDER = {'<', '>'}


def literal(word):
    """The text of a word without expansions, or None."""
    if not word:
        return None
    text = ''
    for part in word.get('Parts', []):
        t = part.get('Type')
        if t == 'Lit':
            text += part.get('Value', '')
        elif t == 'SglQuoted':
            text += part.get('Value', '')
        elif t == 'DblQuoted':
            inner = literal(part)
            if inner is None:
                return None
            text += inner
        else:
            return None
    return text


def has_glob(word):
    """Whether a word has unquoted pattern characters."""
    for part in word.get('Parts', []):
        if part.get('Type') == 'Lit':
            value = part.get('Value', '')
            if any(c in value for c in '*?['):
                return True
    return False


def glob_shape(word):
    """Classify a pattern without expansions by the encodings it permits.

    'star-literal': literal text and '*' only, which prefix, suffix and
    index_of can decide. 'star-literal-1' is the subset with a single '*'
    at one end ('L*' or '*L'), and 'star-only' is '*' alone. 'question'
    adds '?', and 'bracket' has a bracket expression, which need a regular
    expression or a per-character encoding. 'expansion' patterns contain
    parameter expansions or command substitutions.
    """
    if has_expansion(word):
        return 'expansion'
    text = ''
    for part in word.get('Parts', []):
        t = part.get('Type')
        if t == 'Lit':
            # A backslash quotes the next character.
            value = part.get('Value', '')
            i = 0
            while i < len(value):
                if value[i] == '\\' and i + 1 < len(value):
                    text += 'L'
                    i += 2
                    continue
                text += value[i] if value[i] in '*?[' else 'L'
                i += 1
        else:
            text += 'L' * max(1, len(literal(part) or ''))
    if '[' in text:
        return 'bracket'
    if '?' in text:
        return 'question'
    if text == '*':
        return 'star-only'
    if text.count('*') == 1 and (text[0] == '*' or text[-1] == '*'):
        return 'star-literal-1'
    return 'star-literal' if '*' in text else 'literal'


def has_expansion(word):
    for part in word.get('Parts', []):
        t = part.get('Type')
        if t in ('ParamExp', 'CmdSubst', 'ArithmExp'):
            return True
        if t == 'DblQuoted' and has_expansion(part):
            return True
    return False


class Census:
    def __init__(self):
        self.count = collections.Counter()
        self.scripts = collections.Counter()
        self.current = set()

    def add(self, key, n=1):
        self.count[key] += n
        self.current.add(key)

    def end_script(self):
        for key in self.current:
            self.scripts[key] += 1
        self.current = set()

    # ------------------------------------------------------------------

    def walk(self, node):
        if isinstance(node, list):
            for item in node:
                self.walk(item)
            return
        if not isinstance(node, dict):
            return
        t = node.get('Type')
        if t == 'CallExpr':
            self.call(node)
        elif t == 'CaseClause':
            for item in node.get('Items', []):
                for pattern in item.get('Patterns', []):
                    self.add('case pattern shape: ' + glob_shape(pattern))
                    if has_glob(pattern):
                        self.add('case pattern: glob')
                    elif has_expansion(pattern):
                        self.add('case pattern: expansion')
                    else:
                        self.add('case pattern: literal')
        elif t == 'TestClause':
            self.add('[[ ]] (bash)')
        elif t == 'ParamExp':
            self.param(node)
        elif t == 'ArithmExp':
            self.add('$((...)): integer to string')
        elif t == 'CmdSubst':
            self.add('$(...): unknown string')
        elif t == 'Assign':
            value = node.get('Value')
            if value and len(value.get('Parts', [])) > 1:
                self.add('concatenation in assignment')
            if value and value.get('Parts') and \
               value['Parts'][0].get('Type') == 'DblQuoted' and \
               len(value['Parts'][0].get('Parts', [])) > 1:
                self.add('concatenation in assignment')
        elif t == 'DblQuoted':
            if len(node.get('Parts', [])) > 1 and has_expansion(node):
                self.add('concatenation in quotes')
        for key, value in node.items():
            if key not in ('Pos', 'End', 'ValuePos', 'ValueEnd'):
                self.walk(value)

    def call(self, node):
        args = node.get('Args', [])
        if not args:
            return
        name = literal(args[0])
        if name in ('[', 'test'):
            words = [literal(a) for a in args[1:]]
            if name == '[' and words and words[-1] == ']':
                words = words[:-1]
            for w in words:
                if w in TEST_STRING_OPS:
                    self.add('test: string equality')
                elif w in TEST_INT_OPS:
                    self.add('test: integer comparison (string to integer)')
                elif w in TEST_UNARY_STRING:
                    self.add('test: -n/-z (length)')
                elif w in TEST_ORDER:
                    self.add('test: string order < >')
            if len(words) == 1:
                self.add('test: one argument (non-empty)')
        elif name == 'read':
            self.add('read: unknown string')
        elif name in ('expr',):
            self.add('expr utility')
        elif name in ('printf', 'echo'):
            self.add('echo/printf')
        elif name == 'let':
            self.add('let (bash)')

    def param(self, node):
        name = node.get('Param', {}).get('Value', '')
        if node.get('Length'):
            self.add('${#x}: length')
        if node.get('Slice'):
            self.add('${x:o:l}: substring (bash/ksh/zsh)')
        if node.get('Repl'):
            self.add('${x/p/r}: replace (bash/ksh/zsh)')
        if node.get('Index'):
            self.add('array subscript')
        exp = node.get('Exp')
        if exp:
            op = exp.get('Op')
            if op in ('#', '##', '%', '%%'):
                word = exp.get('Word') or {}
                kind = 'glob' if has_glob(word) else 'literal'
                self.add(f'${{x{op}p}}: pattern removal, {kind}')
                self.add('pattern removal shape: ' + glob_shape(word))
            elif op in (',', ',,', '^', '^^'):
                self.add('${x^^}: case conversion (bash)')
            else:
                self.add('${x:-w} and similar')
        if name.isdigit() or name in ('@', '*', '#'):
            self.add('positional parameters')


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    shfmt = sys.argv[2] if len(sys.argv) > 2 else 'shfmt'
    census = Census()
    parsed = 0
    files = [l.strip() for l in open(sys.argv[1]) if l.strip()]
    for path in files:
        try:
            with open(path, 'rb') as f:
                source = f.read()
        except OSError:
            continue
        first = source.split(b'\n', 1)[0]
        lang = 'bash' if b'bash' in first else 'mksh' if b'ksh' in first \
            else 'zsh' if b'zsh' in first else 'posix'
        result = subprocess.run([shfmt, '-ln', lang, '--to-json'],
                                input=source, capture_output=True)
        if result.returncode != 0:
            # retry as bash, which accepts more
            result = subprocess.run([shfmt, '-ln', 'bash', '--to-json'],
                                    input=source, capture_output=True)
            if result.returncode != 0:
                continue
        census.walk(json.loads(result.stdout))
        census.end_script()
        parsed += 1
    print(f'{parsed} scripts parsed of {len(files)}')
    print(f'{"operation":56} {"uses":>7} {"scripts":>8}')
    for key, n in sorted(census.count.items(), key=lambda kv: -kv[1]):
        print(f'{key:56} {n:7} {census.scripts[key]:8}')


if __name__ == '__main__':
    main()
