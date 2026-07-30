# PLR §6.10.2 membership: a string that flows through an UNTYPED call
# boundary (parameter, **kwargs, dict value) must still compare by
# CONTENT against a list-of-strings allow-list. Three regressions
# pinned here (all were false ALARMS -- membership wrongly refuted):
#  - the In-needle was unwrapped to the CONTAINER's type (deref of
#    __list_ptr on a STR-tagged value),
#  - a value-typed needle against typed string elements compared
#    representations, not content,
#  - the pv-container list scan unwrapped both sides to int.
ALLOWED = ["auth-service", "backup-service", "data-service"]
V = []


def direct(name):
    if name not in ALLOWED:
        V.append(name)


def via_kwargs(**kwargs):
    if kwargs.get('params', {}).get('Bucket') not in ALLOWED:
        V.append('kw')


for b in ALLOWED:
    direct(b)
    via_kwargs(params={'Bucket': b})

assert len(V) == 0
