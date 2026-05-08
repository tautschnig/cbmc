# Exercises the library's models for collections, datetime and json.
# Each call must resolve and every attribute read must succeed.

from collections import OrderedDict, defaultdict, Counter, deque
from datetime import datetime, date, timedelta, time
from json import dumps, loads

# collections
od = OrderedDict()
dd = defaultdict(int)
c = Counter()
q = deque()
q.append(1)
q.appendleft(2)
_ = q.pop()
_ = len(q)

# datetime
now = datetime.now()
_ = now.year
_ = now.month
_ = now.day
_ = now.hour
_ = now.minute
today = date.today()
_ = today.year
t = time(12, 0, 0)
_ = t.hour
td = timedelta(days=1, hours=2)
_ = td.days
_ = td.seconds

# json
obj = loads('{"x": 1}')
blob = dumps({"a": 1})

assert True
