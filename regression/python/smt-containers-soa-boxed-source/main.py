# STRUCTURE-OF-ARRAYS List[TypedDict] (the perf-study's flagship
# repro.py shape, previously the pinned boxed-source KNOWNBUG): a
# List[TD] with all-required scalar fields is parallel per-field
# infinite arrays; a comprehension binds its row as an INDEX and
# a['appId'] is a pure per-field select -- the KeyError vanishes for
# declared fields and the closed-form map applies at any length.
# The boxed field value is materialised ONCE per read region
# (td_field_read_memo): independent derefs through the infinite
# values array are unrelated failure objects, and independent
# lookup witnesses exceed solver quantifier budgets.
from typing import TypedDict, List


class App(TypedDict):
    appId: str


class ListAppsResponse(TypedDict):
    apps: List[App]


def list_apps() -> ListAppsResponse: ...


resp = list_apps()
ids = [app['appId'] for app in resp['apps']]
assert len(ids) == len(resp['apps'])
