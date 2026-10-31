// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: a subset of package math (go1.27), with
// the pure-Go implementations (which give the same results as gc's
// assembly ones) and the bit conversions of package cprover/rt.

// Package math models functions of the standard library's package math.
package math

import "cprover/rt"

const (
	uvnan    = 0x7FF8000000000001
	uvinf    = 0x7FF0000000000000
	uvneginf = 0xFFF0000000000000
	mask     = 0x7FF
	shift    = 64 - 11 - 1
	bias     = 1023
	signMask = 1 << 63

	maxFloat64 = 0x1p1023 * (1 + (1 - 0x1p-52))
)

// Float64bits returns the IEEE 754 binary representation of f.
func Float64bits(f float64) uint64 { return rt.Float64bits(f) }

// Float64frombits returns the floating-point number with representation b.
func Float64frombits(b uint64) float64 { return rt.Float64frombits(b) }

// Float32bits returns the IEEE 754 binary representation of f.
func Float32bits(f float32) uint32 { return rt.Float32bits(f) }

// Float32frombits returns the floating-point number with representation b.
func Float32frombits(b uint32) float32 { return rt.Float32frombits(b) }

// Inf returns positive infinity if sign >= 0, negative infinity if sign < 0.
func Inf(sign int) float64 {
	var v uint64
	if sign >= 0 {
		v = uvinf
	} else {
		v = uvneginf
	}
	return Float64frombits(v)
}

// NaN returns an IEEE 754 “not-a-number” value.
func NaN() float64 { return Float64frombits(uvnan) }

// IsNaN reports whether f is an IEEE 754 “not-a-number” value.
func IsNaN(f float64) (is bool) {
	// IEEE 754 says that only NaNs satisfy f != f.
	return f != f
}

// IsInf reports whether f is an infinity, according to sign.
func IsInf(f float64, sign int) bool {
	if sign == 0 {
		f = Abs(f)
	} else if sign < 0 {
		f = -f
	}
	return f > maxFloat64
}

// Abs returns the absolute value of x.
func Abs(x float64) float64 {
	return Float64frombits(Float64bits(x) &^ signMask)
}

// Copysign returns a value with the magnitude of f and the sign of sign.
func Copysign(f, sign float64) float64 {
	return Float64frombits(Float64bits(f)&^signMask | Float64bits(sign)&signMask)
}

// Signbit reports whether x is negative or negative zero.
func Signbit(x float64) bool {
	return int64(Float64bits(x)) < 0
}

// Max returns the larger of x or y. Unlike the built-in max, Max(x, +Inf)
// = Max(+Inf, x) = +Inf also for a NaN x.
func Max(x, y float64) float64 {
	// special cases
	switch {
	case IsInf(x, 1) || IsInf(y, 1):
		return Inf(1)
	case IsNaN(x) || IsNaN(y):
		return NaN()
	case x == 0 && x == y:
		if Signbit(x) {
			return y
		}
		return x
	}
	if x > y {
		return x
	}
	return y
}

// Min returns the smaller of x or y. Unlike the built-in min, Min(x, -Inf)
// = Min(-Inf, x) = -Inf also for a NaN x.
func Min(x, y float64) float64 {
	// special cases
	switch {
	case IsInf(x, -1) || IsInf(y, -1):
		return Inf(-1)
	case IsNaN(x) || IsNaN(y):
		return NaN()
	case x == 0 && x == y:
		if Signbit(x) {
			return x
		}
		return y
	}
	if x < y {
		return x
	}
	return y
}

// Floor returns the greatest integer value less than or equal to x.
func Floor(x float64) float64 {
	if x == 0 || IsNaN(x) || IsInf(x, 0) {
		return x
	}
	if x < 0 {
		d, fract := Modf(-x)
		if fract != 0.0 {
			d = d + 1
		}
		return -d
	}
	d, _ := Modf(x)
	return d
}

// Ceil returns the least integer value greater than or equal to x.
func Ceil(x float64) float64 {
	return -Floor(-x)
}

// Trunc returns the integer value of x.
func Trunc(x float64) float64 {
	if Abs(x) < 1 {
		return Copysign(0, x)
	}

	b := Float64bits(x)
	e := uint(b>>shift)&mask - bias

	// Keep the top 12+e bits, the integer part; clear the rest.
	if e < 64-12 {
		b &^= 1<<(64-12-e) - 1
	}
	return Float64frombits(b)
}

// Modf returns integer and fractional floating-point numbers that sum to
// f. Both values have the same sign as f.
func Modf(f float64) (integer float64, fractional float64) {
	integer = Trunc(f)
	fractional = Copysign(f-integer, f)
	return
}
