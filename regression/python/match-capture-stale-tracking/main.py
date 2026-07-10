# PLR §11.6: a match capture pattern rebinds the name to the subject, so its
# tracking must be invalidated. `b = 1` tracks b=1; `case int() as b` rebinds b
# to 3, but the match handler restores the pre-match snapshot per arm (undoing
# any in-pattern invalidation), so a stale b=1 survived and `t[b]` folded to the
# stale t[1] (wrong element + masked IndexError; t has 3 elements, b is 3). Found
# by the reassignment-invariant audit. Fixed by invalidating captured names after
# restore_tracking, so both the arm body and the merged post-match state are sound.
b = 1
t = (6, 3, 9)
match 3:
    case int() as b:
        pass
r = t[b]
