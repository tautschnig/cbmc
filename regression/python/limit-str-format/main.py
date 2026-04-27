# PLR §4.7.1: str.format() — modeled as nondet string
s = "hello {}".format("world")
# Content not tracked (nondet), but no crash
