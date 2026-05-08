# Regression test for typing.py-style solver crash.
#
# Three issues used to make typing.py crash the string refinement
# loop after parsing completed cleanly:
#
#  1. Large list literals (|elts| > PYTHON_MAX_LIST_LENGTH) produced
#     an inconsistent array IR (declared size 64, operand count N).
#  2. Known-nondet built-ins (e.g. frozenset) returned a typet{}
#     with an empty id; symbols holding such default values then
#     tripped the solver's width lookup.
#  3. BitOr/BitAnd/BitXor on mismatched operand types (e.g. nondet
#     int | set-literal-as-list) produced ill-typed bit operations.
#
# Each of the three patterns in isolation used to crash; together
# they do the same. The test verifies that each construct now runs
# to completion.

# 1. Large list literal: more than 64 string elements.
names = [
    'n00', 'n01', 'n02', 'n03', 'n04', 'n05', 'n06', 'n07', 'n08', 'n09',
    'n10', 'n11', 'n12', 'n13', 'n14', 'n15', 'n16', 'n17', 'n18', 'n19',
    'n20', 'n21', 'n22', 'n23', 'n24', 'n25', 'n26', 'n27', 'n28', 'n29',
    'n30', 'n31', 'n32', 'n33', 'n34', 'n35', 'n36', 'n37', 'n38', 'n39',
    'n40', 'n41', 'n42', 'n43', 'n44', 'n45', 'n46', 'n47', 'n48', 'n49',
    'n50', 'n51', 'n52', 'n53', 'n54', 'n55', 'n56', 'n57', 'n58', 'n59',
    'n60', 'n61', 'n62', 'n63', 'n64', 'n65', 'n66', 'n67', 'n68', 'n69',
    'n70',
]


# 2. frozenset() (known-nondet built-in) used as a default argument.
def _eval_type(t, recursive_guard=frozenset()):
    return t


# 3. Bitwise operator with mismatched operand types (nondet | set).
_TYPING_INTERNALS = frozenset({'a', 'b'})
_SPECIAL_NAMES = frozenset({'c', 'd'})
EXCLUDED_ATTRIBUTES = _TYPING_INTERNALS | _SPECIAL_NAMES | {'e'}

# An assertion so the test has a property to check.
assert len(names) == 71
