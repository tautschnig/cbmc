#!/usr/bin/env python3
"""cmplogs.py A.log B.log: compares two logs of regression/test.pl (e.g. of
partest.sh): the total time of the tests in both, the tests whose status
changed, and those whose time changed most. test.pl reports whole seconds,
so differences of 1 s are noise; re-time candidates with cmpenv.sh."""
import collections
import re
import sys


def tests(path):
    result = {}
    for line in open(path, errors='replace'):
        m = re.search(
            r'Running (\S+)\s+\S*(OK|FAILED)\S*(?: in (\d+) seconds)?', line)
        if m:
            result[m.group(1)] = (
                m.group(2), int(m.group(3)) if m.group(3) else None)
    return result


a, b = tests(sys.argv[1]), tests(sys.argv[2])
common = [k for k in a if k in b and a[k][1] is not None
          and b[k][1] is not None]
print('tests', len(common), 'total', sum(a[k][1] for k in common), '->',
      sum(b[k][1] for k in common))
print('status changes:',
      [(k, a[k][0], b.get(k, ('?',))[0]) for k in a
       if b.get(k, (None,))[0] != a[k][0]])
print('only in', sys.argv[2], [k for k in b if k not in a])
deltas = collections.Counter(b[k][1] - a[k][1] for k in common)
print('time differences (s: tests):', sorted(deltas.items()))
by_delta = sorted(common, key=lambda k: b[k][1] - a[k][1])
print('better:', [(k, a[k][1], b[k][1]) for k in by_delta[:8]])
print('worse:', [(k, a[k][1], b[k][1]) for k in by_delta[-8:][::-1]])
