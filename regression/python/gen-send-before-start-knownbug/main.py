# KNOWNBUG: gen.send(non-None) on a just-started generator raises TypeError
# ("can't send non-None value to a just-started generator"). Generators are
# modelled as eager lists with no generator-object priming state, so .send() is
# not modelled. Desired: VERIFICATION FAILED. Needs a generator state machine.
def g():
    x = yield 1
    yield x


it = g()
it.send(5)
