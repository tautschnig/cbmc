# Regression: complex() string parsing must not throw a C++
# std::invalid_argument out of the python frontend.
# Inputs like complex("+j"), complex("-j"), and other Python-valid
# but stod-unfriendly forms used to crash CBMC with what()=="stod".

# Coefficient-only imaginary parts: "+", "-", "" all map to ±1.0/+1.0
z1 = complex("+j")
z2 = complex("-j")
z3 = complex("j")

# Combined real+unit-imag: "3+j" must not feed "+" to stod.
z4 = complex("3+j")
z5 = complex("3-j")

# Parenthesized form
z6 = complex("(3+4j)")
