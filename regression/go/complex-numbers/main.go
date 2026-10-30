// Go1.27 §Numeric_types: "complex64 the set of all complex numbers with
// float32 real and imaginary parts", complex128 with float64 parts.
// §Complex_numbers: "The built-in function complex constructs a complex
// value from a floating-point real and imaginary part, while real and imag
// extract the real and imaginary parts of a complex value." "for a value z
// of a complex type Z, z == Z(complex(real(z), imag(z)))".
// §Comparison_operators: "Two complex values u and v are equal if both
// real(u) == real(v) and imag(u) == imag(v)."
package main

import "cprover"

func main() {
	a := complex(2, -2)
	cprover.Assert(real(a) == 2 && imag(a) == -2, "complex, real and imag")
	b := 1 + 2i
	cprover.Assert(a+b == 3+0i && a-b == complex(1, -4), "addition, subtraction")
	cprover.Assert(a*b == 6+2i && -b == complex(-1, -2), "multiplication, negation")
	cprover.Assert(b/b == 1 && (6+2i)/b == a, "division")
	var c64 complex64 = complex(0.1, 0.2)
	cprover.Assert(complex128(c64) != complex(0.1, 0.2), "complex64 parts are float32")
	cprover.Assert(complex64(complex128(c64)) == c64, "round trip")
	z := complex(cprover.Nondet[float64](), cprover.Nondet[float64]())
	cprover.Assume(real(z) == real(z) && imag(z) == imag(z)) // not NaN
	cprover.Assert(z == complex128(complex(real(z), imag(z))), "z == Z(complex(real(z), imag(z)))")
	var zero complex128
	cprover.Assert(zero == 0 && real(zero) == 0, "zero value")
}
