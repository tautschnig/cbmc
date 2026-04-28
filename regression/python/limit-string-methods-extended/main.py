# PLR §4.7.1: extended string methods — modeled as nondet
s: str = "hello"
z: str = s.zfill(10)
# Content not tracked, but no crash
