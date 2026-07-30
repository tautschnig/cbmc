# Vacuity twin of membership-value-needle: a NON-member flowing through
# the same untyped boundary must fail the membership check (CPython:
# AssertionError).
ALLOWED = ["auth-service", "backup-service", "data-service"]
V = []


def via_kwargs(**kwargs):
    if kwargs.get('params', {}).get('Bucket') not in ALLOWED:
        V.append('kw')


via_kwargs(params={'Bucket': 'prod-payments-ledger'})
assert len(V) == 0
