def f(query, safe, encoding, errors, quote_via):
    # A chain of nested 2-operand expressions (string concatenations
    # via '+'). Before the fix, try_eval_double walked each such
    # expression three times per level, which became exponential in
    # the depth and timed out during type-checking.
    l = []
    for k, v in query:
        if isinstance(k, bytes):
            k = quote_via(k, safe)
        else:
            k = quote_via(str(k), safe, encoding, errors)
        if isinstance(v, bytes):
            v = quote_via(v, safe)
        else:
            v = quote_via(str(v), safe, encoding, errors)
        l.append(k + '=' + v)
    return l
