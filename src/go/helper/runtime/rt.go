// Package rt is the runtime support of CBMC's Go front-end: operations on
// strings that need loops, written in the subset of Go the front-end
// converts. cbmc-go-helper type-checks it as package "cprover/rt" next to
// the program, and the front-end converts it with the program and calls
// these functions where the specification requires (src/go/go_convert.cpp).
//
// Each function cites the section of the Go specification (go1.27) that
// defines the operation. Loops run once per byte or rune, so verifying a
// program that uses them needs a loop bound at least the string length.
package rt

// StringEqual reports whether a and b are equal (Go1.27
// §Comparison_operators: "Two string values are compared lexically
// byte-wise.").
func StringEqual(a, b string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := 0; i < len(a); i++ {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// StringLess reports whether a < b (Go1.27 §Comparison_operators: "String
// types are comparable and ordered. Two string values are compared
// lexically byte-wise.").
func StringLess(a, b string) bool {
	n := min(len(a), len(b))
	for i := 0; i < n; i++ {
		if a[i] != b[i] {
			return a[i] < b[i]
		}
	}
	return len(a) < len(b)
}

const (
	runeError = 0xFFFD   // the Unicode replacement character
	maxRune   = 0x10FFFF // the largest Unicode code point
)

// DecodeRune decodes the UTF-8 encoded code point that starts at byte index
// i of s, returning it and its length in bytes. Go1.27 §For_range: "If the
// iteration encounters an invalid UTF-8 sequence, the second value will be
// 0xFFFD, the Unicode replacement character, and the next iteration will
// advance a single byte in the string." Valid sequences are those of the
// Unicode standard: shortest form, at most 0x10FFFF, and no surrogates.
func DecodeRune(s string, i int) (rune, int) {
	b0 := s[i]
	if b0 < 0x80 {
		return rune(b0), 1
	}
	var n int
	var r rune
	var lo byte = 0x80 // bounds of the second byte
	var hi byte = 0xBF
	switch {
	case b0 >= 0xC2 && b0 <= 0xDF:
		n, r = 2, rune(b0&0x1F)
	case b0 >= 0xE0 && b0 <= 0xEF:
		n, r = 3, rune(b0&0x0F)
		if b0 == 0xE0 {
			lo = 0xA0 // not overlong
		} else if b0 == 0xED {
			hi = 0x9F // not a surrogate
		}
	case b0 >= 0xF0 && b0 <= 0xF4:
		n, r = 4, rune(b0&0x07)
		if b0 == 0xF0 {
			lo = 0x90 // not overlong
		} else if b0 == 0xF4 {
			hi = 0x8F // at most 0x10FFFF
		}
	default:
		return runeError, 1
	}
	if i+n > len(s) {
		return runeError, 1
	}
	for k := 1; k < n; k++ {
		b := s[i+k]
		if (k == 1 && (b < lo || b > hi)) || (k > 1 && (b < 0x80 || b > 0xBF)) {
			return runeError, 1
		}
		r = r<<6 | rune(b&0x3F)
	}
	return r, n
}

// EncodeRune returns the UTF-8 encoding of r. Go1.27
// §Conversions_to_and_from_a_string_type: "an integer value may be converted
// to a string type. This form of conversion yields a string containing the
// (possibly multi-byte) UTF-8 representation of the Unicode code point with
// the given integer value. Values outside the range of valid Unicode code
// points are converted to "\uFFFD"." The front-end passes -1 for integers
// that do not fit in a rune. Surrogates are not valid code points either.
func EncodeRune(r rune) string {
	if r < 0 || r > maxRune || (r >= 0xD800 && r <= 0xDFFF) {
		r = runeError
	}
	switch {
	case r < 0x80:
		return string([]byte{byte(r)})
	case r < 0x800:
		return string([]byte{0xC0 | byte(r>>6), 0x80 | byte(r)&0x3F})
	case r < 0x10000:
		return string([]byte{0xE0 | byte(r>>12), 0x80 | byte(r>>6)&0x3F,
			0x80 | byte(r)&0x3F})
	default:
		return string([]byte{0xF0 | byte(r>>18), 0x80 | byte(r>>12)&0x3F,
			0x80 | byte(r>>6)&0x3F, 0x80 | byte(r)&0x3F})
	}
}

// StringToRunes converts s to its code points (Go1.27
// §Conversions_to_and_from_a_string_type).
func StringToRunes(s string) []rune {
	runes := []rune{}
	for i := 0; i < len(s); {
		r, n := DecodeRune(s, i)
		runes = append(runes, r)
		i += n
	}
	return runes
}

// RunesToString converts code points to a string: "Converting a slice of
// runes to a string type yields a string that is the concatenation of the
// individual rune values converted to strings."
func RunesToString(runes []rune) string {
	s := ""
	for _, r := range runes {
		s += EncodeRune(r)
	}
	return s
}
