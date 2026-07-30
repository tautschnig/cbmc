# --python-max-list-length must actually resize the container model
# (it used to be parsed but never read: the documented flag silently
# did nothing and recompiling with -DPYTHON_MAX_LIST_LENGTH was the
# only working override). 20 appends + 20 dict inserts exceed the
# default capacity of 16; under --python-max-list-length 32 both fit
# and the length assertions are provable.
def lists():
    xs = []
    for i in range(20):
        xs.append(i)
    assert len(xs) == 20


def dicts():
    d = {}
    for i in range(20):
        d[i] = i
    assert len(d) == 20


lists()
dicts()
