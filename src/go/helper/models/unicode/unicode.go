// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the functions are the real ones for
// Latin-1 (with the real tables properties and asciiFold, go1.27), and
// IsSpace and IsControl for all runes; beyond Latin-1, the functions that
// need the Unicode tables have arbitrary results (a valid rune for the
// case mappings), as the tables are too large to convert.

// Package unicode models functions of the standard library's unicode.
package unicode

import "cprover"

const (
	MaxRune         = '\U0010FFFF' // Maximum valid Unicode code point.
	ReplacementChar = '\uFFFD'     // Represents invalid code points.
	MaxASCII        = '\u007F'     // maximum ASCII value.
	MaxLatin1       = '\u00FF'     // maximum Latin-1 value.
)

// Bit masks for each code point under U+0100, for fast lookup.
const (
	pC     = 1 << iota // a control character.
	pP                 // a punctuation character.
	pN                 // a numeral.
	pS                 // a symbolic character.
	pZ                 // a spacing character.
	pLu                // an upper-case letter.
	pLl                // a lower-case letter.
	pp                 // a printable character according to Go's definition.
	pg     = pp | pZ   // a graphical character according to the Unicode definition.
	pLo    = pLl | pLu // a letter that is neither upper nor lower case.
	pLmask = pLo
)

var properties = [MaxLatin1 + 1]uint8{
	0x00: pC,       // '\x00'
	0x01: pC,       // '\x01'
	0x02: pC,       // '\x02'
	0x03: pC,       // '\x03'
	0x04: pC,       // '\x04'
	0x05: pC,       // '\x05'
	0x06: pC,       // '\x06'
	0x07: pC,       // '\a'
	0x08: pC,       // '\b'
	0x09: pC,       // '\t'
	0x0A: pC,       // '\n'
	0x0B: pC,       // '\v'
	0x0C: pC,       // '\f'
	0x0D: pC,       // '\r'
	0x0E: pC,       // '\x0e'
	0x0F: pC,       // '\x0f'
	0x10: pC,       // '\x10'
	0x11: pC,       // '\x11'
	0x12: pC,       // '\x12'
	0x13: pC,       // '\x13'
	0x14: pC,       // '\x14'
	0x15: pC,       // '\x15'
	0x16: pC,       // '\x16'
	0x17: pC,       // '\x17'
	0x18: pC,       // '\x18'
	0x19: pC,       // '\x19'
	0x1A: pC,       // '\x1a'
	0x1B: pC,       // '\x1b'
	0x1C: pC,       // '\x1c'
	0x1D: pC,       // '\x1d'
	0x1E: pC,       // '\x1e'
	0x1F: pC,       // '\x1f'
	0x20: pZ | pp,  // ' '
	0x21: pP | pp,  // '!'
	0x22: pP | pp,  // '"'
	0x23: pP | pp,  // '#'
	0x24: pS | pp,  // '$'
	0x25: pP | pp,  // '%'
	0x26: pP | pp,  // '&'
	0x27: pP | pp,  // '\''
	0x28: pP | pp,  // '('
	0x29: pP | pp,  // ')'
	0x2A: pP | pp,  // '*'
	0x2B: pS | pp,  // '+'
	0x2C: pP | pp,  // ','
	0x2D: pP | pp,  // '-'
	0x2E: pP | pp,  // '.'
	0x2F: pP | pp,  // '/'
	0x30: pN | pp,  // '0'
	0x31: pN | pp,  // '1'
	0x32: pN | pp,  // '2'
	0x33: pN | pp,  // '3'
	0x34: pN | pp,  // '4'
	0x35: pN | pp,  // '5'
	0x36: pN | pp,  // '6'
	0x37: pN | pp,  // '7'
	0x38: pN | pp,  // '8'
	0x39: pN | pp,  // '9'
	0x3A: pP | pp,  // ':'
	0x3B: pP | pp,  // ';'
	0x3C: pS | pp,  // '<'
	0x3D: pS | pp,  // '='
	0x3E: pS | pp,  // '>'
	0x3F: pP | pp,  // '?'
	0x40: pP | pp,  // '@'
	0x41: pLu | pp, // 'A'
	0x42: pLu | pp, // 'B'
	0x43: pLu | pp, // 'C'
	0x44: pLu | pp, // 'D'
	0x45: pLu | pp, // 'E'
	0x46: pLu | pp, // 'F'
	0x47: pLu | pp, // 'G'
	0x48: pLu | pp, // 'H'
	0x49: pLu | pp, // 'I'
	0x4A: pLu | pp, // 'J'
	0x4B: pLu | pp, // 'K'
	0x4C: pLu | pp, // 'L'
	0x4D: pLu | pp, // 'M'
	0x4E: pLu | pp, // 'N'
	0x4F: pLu | pp, // 'O'
	0x50: pLu | pp, // 'P'
	0x51: pLu | pp, // 'Q'
	0x52: pLu | pp, // 'R'
	0x53: pLu | pp, // 'S'
	0x54: pLu | pp, // 'T'
	0x55: pLu | pp, // 'U'
	0x56: pLu | pp, // 'V'
	0x57: pLu | pp, // 'W'
	0x58: pLu | pp, // 'X'
	0x59: pLu | pp, // 'Y'
	0x5A: pLu | pp, // 'Z'
	0x5B: pP | pp,  // '['
	0x5C: pP | pp,  // '\\'
	0x5D: pP | pp,  // ']'
	0x5E: pS | pp,  // '^'
	0x5F: pP | pp,  // '_'
	0x60: pS | pp,  // '`'
	0x61: pLl | pp, // 'a'
	0x62: pLl | pp, // 'b'
	0x63: pLl | pp, // 'c'
	0x64: pLl | pp, // 'd'
	0x65: pLl | pp, // 'e'
	0x66: pLl | pp, // 'f'
	0x67: pLl | pp, // 'g'
	0x68: pLl | pp, // 'h'
	0x69: pLl | pp, // 'i'
	0x6A: pLl | pp, // 'j'
	0x6B: pLl | pp, // 'k'
	0x6C: pLl | pp, // 'l'
	0x6D: pLl | pp, // 'm'
	0x6E: pLl | pp, // 'n'
	0x6F: pLl | pp, // 'o'
	0x70: pLl | pp, // 'p'
	0x71: pLl | pp, // 'q'
	0x72: pLl | pp, // 'r'
	0x73: pLl | pp, // 's'
	0x74: pLl | pp, // 't'
	0x75: pLl | pp, // 'u'
	0x76: pLl | pp, // 'v'
	0x77: pLl | pp, // 'w'
	0x78: pLl | pp, // 'x'
	0x79: pLl | pp, // 'y'
	0x7A: pLl | pp, // 'z'
	0x7B: pP | pp,  // '{'
	0x7C: pS | pp,  // '|'
	0x7D: pP | pp,  // '}'
	0x7E: pS | pp,  // '~'
	0x7F: pC,       // '\x7f'
	0x80: pC,       // '\u0080'
	0x81: pC,       // '\u0081'
	0x82: pC,       // '\u0082'
	0x83: pC,       // '\u0083'
	0x84: pC,       // '\u0084'
	0x85: pC,       // '\u0085'
	0x86: pC,       // '\u0086'
	0x87: pC,       // '\u0087'
	0x88: pC,       // '\u0088'
	0x89: pC,       // '\u0089'
	0x8A: pC,       // '\u008a'
	0x8B: pC,       // '\u008b'
	0x8C: pC,       // '\u008c'
	0x8D: pC,       // '\u008d'
	0x8E: pC,       // '\u008e'
	0x8F: pC,       // '\u008f'
	0x90: pC,       // '\u0090'
	0x91: pC,       // '\u0091'
	0x92: pC,       // '\u0092'
	0x93: pC,       // '\u0093'
	0x94: pC,       // '\u0094'
	0x95: pC,       // '\u0095'
	0x96: pC,       // '\u0096'
	0x97: pC,       // '\u0097'
	0x98: pC,       // '\u0098'
	0x99: pC,       // '\u0099'
	0x9A: pC,       // '\u009a'
	0x9B: pC,       // '\u009b'
	0x9C: pC,       // '\u009c'
	0x9D: pC,       // '\u009d'
	0x9E: pC,       // '\u009e'
	0x9F: pC,       // '\u009f'
	0xA0: pZ,       // '\u00a0'
	0xA1: pP | pp,  // '¡'
	0xA2: pS | pp,  // '¢'
	0xA3: pS | pp,  // '£'
	0xA4: pS | pp,  // '¤'
	0xA5: pS | pp,  // '¥'
	0xA6: pS | pp,  // '¦'
	0xA7: pP | pp,  // '§'
	0xA8: pS | pp,  // '¨'
	0xA9: pS | pp,  // '©'
	0xAA: pLo | pp, // 'ª'
	0xAB: pP | pp,  // '«'
	0xAC: pS | pp,  // '¬'
	0xAD: 0,        // '\u00ad'
	0xAE: pS | pp,  // '®'
	0xAF: pS | pp,  // '¯'
	0xB0: pS | pp,  // '°'
	0xB1: pS | pp,  // '±'
	0xB2: pN | pp,  // '²'
	0xB3: pN | pp,  // '³'
	0xB4: pS | pp,  // '´'
	0xB5: pLl | pp, // 'µ'
	0xB6: pP | pp,  // '¶'
	0xB7: pP | pp,  // '·'
	0xB8: pS | pp,  // '¸'
	0xB9: pN | pp,  // '¹'
	0xBA: pLo | pp, // 'º'
	0xBB: pP | pp,  // '»'
	0xBC: pN | pp,  // '¼'
	0xBD: pN | pp,  // '½'
	0xBE: pN | pp,  // '¾'
	0xBF: pP | pp,  // '¿'
	0xC0: pLu | pp, // 'À'
	0xC1: pLu | pp, // 'Á'
	0xC2: pLu | pp, // 'Â'
	0xC3: pLu | pp, // 'Ã'
	0xC4: pLu | pp, // 'Ä'
	0xC5: pLu | pp, // 'Å'
	0xC6: pLu | pp, // 'Æ'
	0xC7: pLu | pp, // 'Ç'
	0xC8: pLu | pp, // 'È'
	0xC9: pLu | pp, // 'É'
	0xCA: pLu | pp, // 'Ê'
	0xCB: pLu | pp, // 'Ë'
	0xCC: pLu | pp, // 'Ì'
	0xCD: pLu | pp, // 'Í'
	0xCE: pLu | pp, // 'Î'
	0xCF: pLu | pp, // 'Ï'
	0xD0: pLu | pp, // 'Ð'
	0xD1: pLu | pp, // 'Ñ'
	0xD2: pLu | pp, // 'Ò'
	0xD3: pLu | pp, // 'Ó'
	0xD4: pLu | pp, // 'Ô'
	0xD5: pLu | pp, // 'Õ'
	0xD6: pLu | pp, // 'Ö'
	0xD7: pS | pp,  // '×'
	0xD8: pLu | pp, // 'Ø'
	0xD9: pLu | pp, // 'Ù'
	0xDA: pLu | pp, // 'Ú'
	0xDB: pLu | pp, // 'Û'
	0xDC: pLu | pp, // 'Ü'
	0xDD: pLu | pp, // 'Ý'
	0xDE: pLu | pp, // 'Þ'
	0xDF: pLl | pp, // 'ß'
	0xE0: pLl | pp, // 'à'
	0xE1: pLl | pp, // 'á'
	0xE2: pLl | pp, // 'â'
	0xE3: pLl | pp, // 'ã'
	0xE4: pLl | pp, // 'ä'
	0xE5: pLl | pp, // 'å'
	0xE6: pLl | pp, // 'æ'
	0xE7: pLl | pp, // 'ç'
	0xE8: pLl | pp, // 'è'
	0xE9: pLl | pp, // 'é'
	0xEA: pLl | pp, // 'ê'
	0xEB: pLl | pp, // 'ë'
	0xEC: pLl | pp, // 'ì'
	0xED: pLl | pp, // 'í'
	0xEE: pLl | pp, // 'î'
	0xEF: pLl | pp, // 'ï'
	0xF0: pLl | pp, // 'ð'
	0xF1: pLl | pp, // 'ñ'
	0xF2: pLl | pp, // 'ò'
	0xF3: pLl | pp, // 'ó'
	0xF4: pLl | pp, // 'ô'
	0xF5: pLl | pp, // 'õ'
	0xF6: pLl | pp, // 'ö'
	0xF7: pS | pp,  // '÷'
	0xF8: pLl | pp, // 'ø'
	0xF9: pLl | pp, // 'ù'
	0xFA: pLl | pp, // 'ú'
	0xFB: pLl | pp, // 'û'
	0xFC: pLl | pp, // 'ü'
	0xFD: pLl | pp, // 'ý'
	0xFE: pLl | pp, // 'þ'
	0xFF: pLl | pp, // 'ÿ'
}

var asciiFold = [MaxASCII + 1]uint16{
	0x0000,
	0x0001,
	0x0002,
	0x0003,
	0x0004,
	0x0005,
	0x0006,
	0x0007,
	0x0008,
	0x0009,
	0x000A,
	0x000B,
	0x000C,
	0x000D,
	0x000E,
	0x000F,
	0x0010,
	0x0011,
	0x0012,
	0x0013,
	0x0014,
	0x0015,
	0x0016,
	0x0017,
	0x0018,
	0x0019,
	0x001A,
	0x001B,
	0x001C,
	0x001D,
	0x001E,
	0x001F,
	0x0020,
	0x0021,
	0x0022,
	0x0023,
	0x0024,
	0x0025,
	0x0026,
	0x0027,
	0x0028,
	0x0029,
	0x002A,
	0x002B,
	0x002C,
	0x002D,
	0x002E,
	0x002F,
	0x0030,
	0x0031,
	0x0032,
	0x0033,
	0x0034,
	0x0035,
	0x0036,
	0x0037,
	0x0038,
	0x0039,
	0x003A,
	0x003B,
	0x003C,
	0x003D,
	0x003E,
	0x003F,
	0x0040,
	0x0061,
	0x0062,
	0x0063,
	0x0064,
	0x0065,
	0x0066,
	0x0067,
	0x0068,
	0x0069,
	0x006A,
	0x006B,
	0x006C,
	0x006D,
	0x006E,
	0x006F,
	0x0070,
	0x0071,
	0x0072,
	0x0073,
	0x0074,
	0x0075,
	0x0076,
	0x0077,
	0x0078,
	0x0079,
	0x007A,
	0x005B,
	0x005C,
	0x005D,
	0x005E,
	0x005F,
	0x0060,
	0x0041,
	0x0042,
	0x0043,
	0x0044,
	0x0045,
	0x0046,
	0x0047,
	0x0048,
	0x0049,
	0x004A,
	0x212A,
	0x004C,
	0x004D,
	0x004E,
	0x004F,
	0x0050,
	0x0051,
	0x0052,
	0x017F,
	0x0054,
	0x0055,
	0x0056,
	0x0057,
	0x0058,
	0x0059,
	0x005A,
	0x007B,
	0x007C,
	0x007D,
	0x007E,
	0x007F,
}

// beyond is the arbitrary answer of a property that needs the tables.
func beyond() bool { return cprover.Nondet[bool]() }

// mapped is the arbitrary result of a case mapping of r beyond ASCII that
// needs the tables: r itself if it is not a valid code point (as there),
// else a valid code point.
func mapped(r rune) rune {
	if r < 0 || r > MaxRune {
		return r
	}
	m := cprover.Nondet[rune]()
	cprover.Assume(0 <= m && m <= MaxRune)
	return m
}

// IsDigit reports whether the rune is a decimal digit.
func IsDigit(r rune) bool {
	if r <= MaxLatin1 {
		return '0' <= r && r <= '9'
	}
	return beyond()
}

// IsGraphic reports whether the rune is defined as a Graphic by Unicode.
func IsGraphic(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pg != 0
	}
	return beyond()
}

// IsPrint reports whether the rune is defined as printable by Go.
func IsPrint(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pp != 0
	}
	return beyond()
}

// IsControl reports whether the rune is a control character.
func IsControl(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pC != 0
	}
	// All control characters are < MaxLatin1.
	return false
}

// IsLetter reports whether the rune is a letter (category L).
func IsLetter(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&(pLmask) != 0
	}
	return beyond()
}

// IsMark reports whether the rune is a mark character (category M).
func IsMark(r rune) bool {
	// There are no mark characters in Latin-1.
	if uint32(r) <= MaxLatin1 {
		return false
	}
	return beyond()
}

// IsNumber reports whether the rune is a number (category N).
func IsNumber(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pN != 0
	}
	return beyond()
}

// IsPunct reports whether the rune is a Unicode punctuation character
// (category P).
func IsPunct(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pP != 0
	}
	return beyond()
}

// IsSpace reports whether the rune is a space character as defined by
// Unicode's White Space property.
func IsSpace(r rune) bool {
	// This property isn't the same as Z; special-case it.
	if uint32(r) <= MaxLatin1 {
		switch r {
		case '\t', '\n', '\v', '\f', '\r', ' ', 0x85, 0xA0:
			return true
		}
		return false
	}
	// The White_Space table beyond Latin-1.
	switch {
	case r == 0x1680, 0x2000 <= r && r <= 0x200a, r == 0x2028, r == 0x2029,
		r == 0x202f, r == 0x205f, r == 0x3000:
		return true
	}
	return false
}

// IsSymbol reports whether the rune is a symbolic character.
func IsSymbol(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pS != 0
	}
	return beyond()
}

// IsUpper reports whether the rune is an upper case letter.
func IsUpper(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pLmask == pLu
	}
	return beyond()
}

// IsLower reports whether the rune is a lower case letter.
func IsLower(r rune) bool {
	if uint32(r) <= MaxLatin1 {
		return properties[uint8(r)]&pLmask == pLl
	}
	return beyond()
}

// IsTitle reports whether the rune is a title case letter.
func IsTitle(r rune) bool {
	if r <= MaxLatin1 {
		return false
	}
	return beyond()
}

// ToUpper maps the rune to upper case.
func ToUpper(r rune) rune {
	if r <= MaxASCII {
		if 'a' <= r && r <= 'z' {
			r -= 'a' - 'A'
		}
		return r
	}
	return mapped(r)
}

// ToLower maps the rune to lower case.
func ToLower(r rune) rune {
	if r <= MaxASCII {
		if 'A' <= r && r <= 'Z' {
			r += 'a' - 'A'
		}
		return r
	}
	return mapped(r)
}

// ToTitle maps the rune to title case.
func ToTitle(r rune) rune {
	if r <= MaxASCII {
		if 'a' <= r && r <= 'z' { // title case is upper case for ASCII
			r -= 'a' - 'A'
		}
		return r
	}
	return mapped(r)
}

// SimpleFold iterates over Unicode code points equivalent under
// the Unicode-defined simple case folding.
func SimpleFold(r rune) rune {
	if r < 0 || r > MaxRune {
		return r
	}
	if int(r) < len(asciiFold) {
		return rune(asciiFold[r])
	}
	return mapped(r)
}
