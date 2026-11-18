# Set operations on a variable holding a set of strings, which the refined
# string solver does not finish (test.desc, FUTURE); native SMT strings
# prove it in seconds (native.desc).
#
# Analysis (refined strings, cbmc 6.10 + plan item 12):
# - The program is small: symex takes 0.3 s and --show-vcc prints 4 MB
#   (longest line about 1,100 characters).
# - A set of strings is a list of 16 slots. |= and -= on a variable (whose
#   length has no static bound) append conditionally and then put the set
#   in any order (permute_list_set, as CPython orders sets by string
#   hashes): each slot becomes a choice between several strings, nested at
#   each join point. The slots are DAGs of shared subexpressions, much
#   larger when walked as trees.
# - The refined solver's preprocessing walks them as trees. Samples with a
#   debug build stopped at:
#   1. simplify of the arguments of the string functions (dec_solve,
#      "Replacing string ids and simplifying arguments"): out of 8 GB;
#   2. with a DAG walk there (spike), the depth iterator of
#      add_dependency_to_string_subexprs (now a DAG walk);
#   3. then array_poolt::get_or_create_length's table of if-expressions,
#      comparing large equal trees structurally (now also looked up by
#      node); the last sample was still there, on first sight of a node.
#   Hash-consing the equations (spike) did not help. With the spikes the
#   run needs 52 MB but does not finish in 300 s.
#   The spikes: scripts/string-solver-timing/spikes/
#   dag-simplify-and-hash-cons.patch.
# - Not known: whether the remaining time is more walks of this kind or
#   the size of what the solver builds for the merged strings (the numbers
#   of string functions, lengths and index terms were not measured).
# - Native SMT strings (--cvc5/--z3 --python-smt-strings): z3 4.16 1.0 s,
#   z3 5.4.1 1.2 s, cvc5 1.2.1 5.3 s, cvc5 1.4.2 6.3 s. So the formula is
#   easy; the problem is the refined solver's handling of merged strings.
v = {"a"}
v |= {"b"}
v -= {"a"}
assert v == {"b"}
