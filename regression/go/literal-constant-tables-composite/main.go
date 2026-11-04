// Generated tables of composite constants (sent by the helper as their
// values): arrays of structs with blank fields, of arrays and of
// floating-point numbers (e.g., strconv's powers of ten), keyed elements
// and keyed fields.
package main

import "cprover"

type hiLo struct {
	Hi uint64
	_  uint8
	Lo uint64
}

var table = [...]hiLo{
	{0x0, 0, 0x0},
	{0x1eef, 0, 0x19919},
	{0x3dde, 0, 0x33232},
	{0x5ccd, 0, 0x4cb4b},
	{0x7bbc, 0, 0x66464},
	{0x9aab, 0, 0x7fd7d},
	{0xb99a, 0, 0x99696},
	{0xd889, 0, 0xb2faf},
	{0xf778, 0, 0xcc8c8},
	{0x1667, 0, 0xe61e1},
	{0x3556, 0, 0xffafa},
	{0x5445, 0, 0x119413},
	{0x7334, 0, 0x132d2c},
	{0x9223, 0, 0x14c645},
	{0xb112, 0, 0x165f5e},
	{0xd001, 0, 0x17f877},
	{0xeef0, 0, 0x199190},
	{0xddf, 0, 0x1b2aa9},
	{0x2cce, 0, 0x1cc3c2},
	{0x4bbd, 0, 0x1e5cdb},
}

var squares = [][2]int{{0, 0}, {1, 1}, {2, 4}, {3, 9}, {4, 16}, {5, 25}, {6, 36}, {7, 49}, {8, 64}, {9, 81}, {10, 100}, {11, 121}, {12, 144}, {13, 169}, {14, 196}, {15, 225}, {16, 256}, {17, 289}}

var scales = [...]float64{1e-10, 2.5e-9, -3.25e-8, 0.1e-7, 1e-6, 2.5e-5, -3.25e-4, 0.1e-3, 1e-2, 2.5e-1, -3.25e0, 0.1e1, 1e2, 2.5e3, -3.25e4, 0.1e5, 1e6, 2.5e7, -3.25e8, 0.1e9}

var keyed = [20]hiLo{3: {Lo: 9}, 19: {Hi: 1}, 0: {1, 0, 2}, 1: {}, 2: {}, 4: {}, 5: {}, 6: {}, 7: {}, 8: {}, 9: {}, 10: {}, 11: {}, 12: {}, 13: {}, 14: {}}

func main() {
	cprover.Assert(table[3].Hi == 0x5ccd && table[19].Lo == 0x1e5cdb, "structs")
	cprover.Assert(len(squares) == 18 && squares[17][1] == 289, "arrays")
	cprover.Assert(scales[1] == 2.5e-9 && scales[19] == 0.1e9, "floats")
	cprover.Assert(keyed[3].Lo == 9 && keyed[3].Hi == 0 && keyed[19].Hi == 1 && keyed[0].Lo == 2, "keys")
	table[0].Lo = 5
	cprover.Assert(table[0].Lo == 5, "mutable")
}
