# A ternary whose branches are lists with different ELEMENT layouts
# (a python_value-element list from .get() vs a literal []) previously
# lowered to a reinterpreting typecast between structs of different
# widths, handing the SAT backend garbage literals (an invariant
# failure in lcnf, found on agent-generated AWS code).
def f():
    xs = nondet_dict().get('Images', [])
    ys = sorted(xs, key=lambda x: x['CreationDate'], reverse=True) \
        if isinstance(xs, list) else []
    zs = [i['ImageId'] for i in ys[:5]]


def keeps_length():
    # The coercion must preserve length (an empty literal flowing into
    # a differently-typed list slot keeps len == 0).
    ms = []
    ns = [d for d in ms if d]
    assert len(ns) == 0


f()
keeps_length()
