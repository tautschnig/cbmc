# PLR §6.10.1: list ==/!= across element types. xs is list[int] [6,0]; the
# literal [6, False] is list[python_value]. 0 == False, so `xs != [6, False]`
# is False in CPython and this assert raises. Previously the cross-element-type
# bridge tunnelled `not(all_equal)` for NotEq, which the pipeline's `!= true`
# double-negated -> `a != b` returned `a == b` (false proof). Now the bridge
# tunnels all_equal for both ops. Expected: VERIFICATION FAILED.
xs = [6]
xs.append(9 in xs)
assert xs != [6, False]
