// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: a subset of package unicode/utf8
// (go1.27), with decoding and encoding from package cprover/rt (which
// implement the same Unicode rules).

// Package utf8 models functions of the standard library's unicode/utf8.
package utf8

import "cprover/rt"

const (
	runeError    = '\uFFFD'
	maxRune      = '\U0010FFFF'
	surrogateMin = 0xD800
	surrogateMax = 0xDFFF
)

// DecodeRuneInString unpacks the first UTF-8 encoding in s and returns the
// rune and its width in bytes. If s is empty it returns (RuneError, 0).
// If the encoding is invalid, it returns (RuneError, 1).
func DecodeRuneInString(s string) (r rune, size int) {
	if len(s) == 0 {
		return runeError, 0
	}
	return rt.DecodeRune(s, 0)
}

// DecodeRune is like DecodeRuneInString but its input is a byte slice.
func DecodeRune(p []byte) (r rune, size int) {
	return DecodeRuneInString(string(p))
}

// RuneCountInString is like RuneCount but its input is a string.
func RuneCountInString(s string) (n int) {
	for i := 0; i < len(s); n++ {
		_, size := rt.DecodeRune(s, i)
		i += size
	}
	return n
}

// RuneCount returns the number of runes in p. Erroneous and short
// encodings are treated as single runes of width 1 byte.
func RuneCount(p []byte) int {
	return RuneCountInString(string(p))
}

// ValidString reports whether s consists entirely of valid UTF-8-encoded
// runes.
func ValidString(s string) bool {
	for i := 0; i < len(s); {
		r, size := rt.DecodeRune(s, i)
		if r == runeError && size == 1 {
			return false
		}
		i += size
	}
	return true
}

// Valid reports whether p consists entirely of valid UTF-8-encoded runes.
func Valid(p []byte) bool {
	return ValidString(string(p))
}

// ValidRune reports whether r can be legally encoded as UTF-8. Code points
// that are out of range or a surrogate half are illegal.
func ValidRune(r rune) bool {
	switch {
	case 0 <= r && r < surrogateMin:
		return true
	case surrogateMax < r && r <= maxRune:
		return true
	}
	return false
}

// RuneLen returns the number of bytes in the UTF-8 encoding of the rune.
// It returns -1 if the rune is not a valid value to encode in UTF-8.
func RuneLen(r rune) int {
	switch {
	case r < 0:
		return -1
	case r < 0x80:
		return 1
	case r < 0x800:
		return 2
	case surrogateMin <= r && r <= surrogateMax:
		return -1
	case r < 0x10000:
		return 3
	case r <= maxRune:
		return 4
	}
	return -1
}

// EncodeRune writes into p (which must be large enough) the UTF-8 encoding
// of the rune. If the rune is out of range, it writes the encoding of
// RuneError. It returns the number of bytes written.
func EncodeRune(p []byte, r rune) int {
	e := rt.EncodeRune(r)
	_ = p[len(e)-1] // the panic for a short p, before writing
	return copy(p, e)
}

// AppendRune appends the UTF-8 encoding of r to the end of p and returns
// the extended buffer. If the rune is out of range, it appends the
// encoding of RuneError.
func AppendRune(p []byte, r rune) []byte {
	return append(p, rt.EncodeRune(r)...)
}
