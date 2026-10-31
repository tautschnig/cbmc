// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: a subset of package strconv (go1.27),
// formatting digit by digit.

// Package strconv models functions of the standard library's strconv.
package strconv

const digits = "0123456789abcdefghijklmnopqrstuvwxyz"

// FormatBool returns "true" or "false" according to the value of b.
func FormatBool(b bool) string {
	if b {
		return "true"
	}
	return "false"
}

// FormatUint returns the string representation of i in the given base, for
// 2 <= base <= 36. The result uses the lower-case letters 'a' to 'z' for
// digit values >= 10.
func FormatUint(i uint64, base int) string {
	return formatBits(i, base, false)
}

// FormatInt returns the string representation of i in the given base, for
// 2 <= base <= 36. The result uses the lower-case letters 'a' to 'z' for
// digit values >= 10.
func FormatInt(i int64, base int) string {
	if i < 0 {
		return formatBits(uint64(-i), base, true)
	}
	return formatBits(uint64(i), base, false)
}

// Itoa is equivalent to FormatInt(int64(i), 10).
func Itoa(i int) string {
	return FormatInt(int64(i), 10)
}

func formatBits(u uint64, base int, neg bool) string {
	if base < 2 || base > len(digits) {
		panic("strconv: illegal AppendInt/FormatInt base")
	}
	var a [64 + 1]byte // +1 for sign of 64bit value in base 2
	i := len(a)
	b := uint64(base)
	for u >= b {
		i--
		a[i] = digits[u%b]
		u /= b
	}
	i--
	a[i] = digits[u]
	if neg {
		i--
		a[i] = '-'
	}
	return string(a[i:])
}
