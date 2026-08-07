# KNOWNBUG: comprehension over a BOXED List[TypedDict] read out of a
# stub dict value (the perf-study's flagship repro.py shape; also
# d1/d4). Elements are python_value boxes by design (an inline dict
# struct would nest infinite arrays -- no byte-lowering width), so
# the representative's KeyError obligation on a['appId'] is honestly
# unprovable: the element pv's target dict cannot be constrained
# without per-element heap objects, and aliasing all elements to one
# representative object would prove FALSE equalities. LOUD (fails in
# ~1s), never a false proof, never a hang.
#
# The whole-group fix is the STRUCTURE-OF-ARRAYS representation for
# List[TD-with-known-fields] (parallel infinite arrays per field;
# a['f'] at index j reads f_data[j]) -- the encoding the perf-study
# validated in C with flat per-field arrays. See the containers
# plan doc.
from typing import TypedDict, List


class App(TypedDict):
    appId: str


class ListAppsResponse(TypedDict):
    apps: List[App]


def list_apps() -> ListAppsResponse: ...


resp = list_apps()
ids = [app['appId'] for app in resp['apps']]
assert len(ids) == len(resp['apps'])
