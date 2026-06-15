# Soundness guard for re.findall / re.finditer / re.split.
# These previously returned [] unconditionally, which is UNSOUND: code that
# iterates the result would silently check nothing (missed bugs). They now
# return a sound bounded nondet list, so a property that must hold for EVERY
# match is actually exercised. This assertion is expected to FAIL (the nondet
# tokens are arbitrary strings, not "ZZZ"); if findall regressed to [], the
# loop body would never run and this would spuriously pass.
import re

for tok in re.findall("[0-9]+", "a12b34c5"):
    assert tok == "ZZZ"
