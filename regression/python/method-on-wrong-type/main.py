# PLR §4: a method EXCLUSIVE to one built-in type, called on a concrete receiver
# of a DIFFERENT built-in type, is an AttributeError ((5).append, [1].keys,
# (1,2).add, "s".append). Generalises the str-method-on-non-str check to
# list/dict/set-only method names. Gated: only strictly-exclusive names (not
# count/index/pop/...), only concrete built-ins (not Any), not user classes
# (may define the name), and a list receiver is not flagged for list-only
# methods (so bytes=list[uint8] is unaffected).
def main() -> None:
    n = 5
    n.append(3)   # int has no append -> AttributeError


main()
