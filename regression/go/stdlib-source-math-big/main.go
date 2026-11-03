// Package math/big is its real source (the pure Go arithmetic): the
// arithmetic and comparisons of big.Int.
package main

import (
	"cprover"
	"math/big"
)

func main() {
	a := big.NewInt(1 << 40)
	b := new(big.Int).Mul(a, a)
	cprover.Assert(b.BitLen() == 81, "BitLen")
	c := new(big.Int).Add(b, big.NewInt(1))
	cprover.Assert(c.Cmp(b) > 0 && c.Sign() == 1, "Cmp")
	d := new(big.Int).Sub(c, b)
	cprover.Assert(d.IsInt64() && d.Int64() == 1, "Sub")
	m := new(big.Int).Mod(big.NewInt(17), big.NewInt(5))
	cprover.Assert(m.Int64() == 2, "Mod")
	cprover.Assert(new(big.Int).SetBytes([]byte{1, 0}).Int64() == 256, "SetBytes")
	cprover.Assert(new(big.Int).Neg(m).Sign() == -1, "Neg")
}
