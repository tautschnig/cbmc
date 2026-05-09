"""
Verification model of the `textwrap` module.

Wrapping, filling, dedenting helpers. We model each with a
minimal Python implementation — good enough for verification
of code that just uses textwrap to reformat strings. Precise
line-breaking semantics (e.g. drop_whitespace, break_on_hyphens)
aren't enforced.
"""


class TextWrapper:
    def __init__(self, width=70, initial_indent="", subsequent_indent="",
                 expand_tabs=True, replace_whitespace=True,
                 fix_sentence_endings=False, break_long_words=True,
                 drop_whitespace=True, break_on_hyphens=True,
                 tabsize=8, max_lines=None, placeholder=" [...]"):
        self.width = width
        self.initial_indent = initial_indent
        self.subsequent_indent = subsequent_indent

    def wrap(self, text):
        # Split on spaces; group until width reached.
        words = text.split()
        lines = []
        current = self.initial_indent
        for w in words:
            if len(current) + len(w) + 1 <= self.width:
                if current:
                    current = current + " " + w
                else:
                    current = w
            else:
                if current:
                    lines.append(current)
                current = self.subsequent_indent + w
        if current:
            lines.append(current)
        return lines

    def fill(self, text):
        return "\n".join(self.wrap(text))


def wrap(text, width=70, **kwargs):
    return TextWrapper(width=width, **kwargs).wrap(text)


def fill(text, width=70, **kwargs):
    return TextWrapper(width=width, **kwargs).fill(text)


def shorten(text, width, **kwargs):
    if len(text) <= width:
        return text
    return text[:width - 4] + " ..."


def dedent(text):
    # CPython computes the common leading whitespace and removes
    # it from every line. Our model: strip leading spaces from
    # each line (coarse over-approximation).
    lines = text.split("\n")
    stripped = []
    for line in lines:
        i = 0
        while i < len(line) and line[i] == " ":
            i = i + 1
        stripped.append(line[i:])
    return "\n".join(stripped)


def indent(text, prefix, predicate=None):
    if predicate is None:
        def predicate(line):
            return len(line) > 0 and line.strip() != ""
    lines = text.split("\n")
    result = []
    for line in lines:
        if predicate(line):
            result.append(prefix + line)
        else:
            result.append(line)
    return "\n".join(result)
