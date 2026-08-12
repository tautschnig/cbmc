# Query isolation (schema-study finding, verified on k5): a file
# with MANY witness-encoded comprehension sites can be intractable
# as ONE query while every property solves instantly in ISOLATION
# (--property): unrelated sites' witness quantifiers poison
# E-matching, and the effect is structural, not formula size.
# This test pins the pattern: 4 dictcomp sites + a site-0 contents
# fact, verified as a SINGLE property.
from typing import TypedDict, List


class Item(TypedDict):
    ident: int


def get_items() -> List[Item]: ...


xs = get_items()
by = {it['ident']: it['ident'] * 2 for it in xs if it['ident'] > 500}
ys = {it['ident'] + 1: it['ident'] for it in xs if it['ident'] > 100}
zs = {it['ident'] + 2: it['ident'] for it in xs if it['ident'] > 200}
ws = {it['ident'] + 3: it['ident'] for it in xs if it['ident'] > 300}
for k in by:
    assert k > 500
