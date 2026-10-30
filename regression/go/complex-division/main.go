// Go1.27 §Floating_point_operators: "The result of a floating-point or
// complex division by zero is not specified beyond the IEEE 754 standard".
// The results are those of gc, the reference implementation (Smith's
// algorithm with the C99 Annex G corrections for infinities and zeros).
package main

import "cprover"

func isNaN(f float64) bool { return f != f }

func main() {
	zero := 0.0
	inf := 1 / zero
	one := complex(1, 1)
	q := one / complex(zero, zero)
	cprover.Assert(real(q) == inf && imag(q) == inf, "finite / 0 is infinite")
	q = complex(inf, 1) / complex(2, 3)
	cprover.Assert(real(q) == inf && imag(q) == -inf, "infinite / finite")
	q = one / complex(inf, 1)
	cprover.Assert(real(q) == 0 && imag(q) == 0, "finite / infinite is zero")
	q = complex(zero, zero) / complex(zero, zero)
	cprover.Assert(isNaN(real(q)) && isNaN(imag(q)), "0 / 0 is NaN")

	var c64 complex64 = 1 + 2i
	cprover.Assert(c64/c64 == 1 && (6+2i)/c64 == 2-2i, "complex64 division")
}
