# PLR flow-sensitivity: 'if K not in D: D[K] = default'
# idiom guarantees K is in D afterwards. Without tracking,
# our KeyError check fires spuriously on the subsequent
# subscript read.

from typing import Any, List, Dict


def group_by(items: List[Dict[str, Any]]) -> None:
    providers: dict = {}
    for item in items:
        key = item.get('name', 'Unknown')
        if key not in providers:
            providers[key] = []
        # Previously FP: dict subscript read emits a KeyError
        # check even though the guard just inserted the key.
        providers[key].append(item)


ms: List[Dict[str, Any]] = [{"name": "X"}]
group_by(ms)
