x = 10


# PLR §8: using a name before its `global` declaration in the same function
# is a compile-time SyntaxError. ast.parse accepts it but the compiler
# rejects it; the AST server surfaces this (allow-listed) so CBMC reports a
# parsing error instead of silently verifying the program.
def modify():
    x = x + 1
    global x


modify()
