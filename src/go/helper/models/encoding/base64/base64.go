// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the loop of NewEncoding unrolled.

// Package base64 overrides NewEncoding of the standard library's
// encoding/base64 (go1.27), whose source is the model otherwise: the same
// code with its loop over the 64 symbols unrolled, so that the package's
// initialisation (StdEncoding, URLEncoding) needs no unwinding.
package base64

// NewEncoding returns a new padded Encoding defined by the given alphabet,
// which must be a 64-byte string that contains unique byte values and
// does not contain the padding character or CR / LF ('\r', '\n').
func NewEncoding(encoder string) *Encoding {
	if len(encoder) != 64 {
		panic("encoding alphabet is not 64-bytes long")
	}

	e := new(Encoding)
	e.padChar = StdPadding
	copy(e.encode[:], encoder)
	copy(e.decodeMap[:], decodeMapInitialize)

	symbol(e, encoder[0], 0)
	symbol(e, encoder[1], 1)
	symbol(e, encoder[2], 2)
	symbol(e, encoder[3], 3)
	symbol(e, encoder[4], 4)
	symbol(e, encoder[5], 5)
	symbol(e, encoder[6], 6)
	symbol(e, encoder[7], 7)
	symbol(e, encoder[8], 8)
	symbol(e, encoder[9], 9)
	symbol(e, encoder[10], 10)
	symbol(e, encoder[11], 11)
	symbol(e, encoder[12], 12)
	symbol(e, encoder[13], 13)
	symbol(e, encoder[14], 14)
	symbol(e, encoder[15], 15)
	symbol(e, encoder[16], 16)
	symbol(e, encoder[17], 17)
	symbol(e, encoder[18], 18)
	symbol(e, encoder[19], 19)
	symbol(e, encoder[20], 20)
	symbol(e, encoder[21], 21)
	symbol(e, encoder[22], 22)
	symbol(e, encoder[23], 23)
	symbol(e, encoder[24], 24)
	symbol(e, encoder[25], 25)
	symbol(e, encoder[26], 26)
	symbol(e, encoder[27], 27)
	symbol(e, encoder[28], 28)
	symbol(e, encoder[29], 29)
	symbol(e, encoder[30], 30)
	symbol(e, encoder[31], 31)
	symbol(e, encoder[32], 32)
	symbol(e, encoder[33], 33)
	symbol(e, encoder[34], 34)
	symbol(e, encoder[35], 35)
	symbol(e, encoder[36], 36)
	symbol(e, encoder[37], 37)
	symbol(e, encoder[38], 38)
	symbol(e, encoder[39], 39)
	symbol(e, encoder[40], 40)
	symbol(e, encoder[41], 41)
	symbol(e, encoder[42], 42)
	symbol(e, encoder[43], 43)
	symbol(e, encoder[44], 44)
	symbol(e, encoder[45], 45)
	symbol(e, encoder[46], 46)
	symbol(e, encoder[47], 47)
	symbol(e, encoder[48], 48)
	symbol(e, encoder[49], 49)
	symbol(e, encoder[50], 50)
	symbol(e, encoder[51], 51)
	symbol(e, encoder[52], 52)
	symbol(e, encoder[53], 53)
	symbol(e, encoder[54], 54)
	symbol(e, encoder[55], 55)
	symbol(e, encoder[56], 56)
	symbol(e, encoder[57], 57)
	symbol(e, encoder[58], 58)
	symbol(e, encoder[59], 59)
	symbol(e, encoder[60], 60)
	symbol(e, encoder[61], 61)
	symbol(e, encoder[62], 62)
	symbol(e, encoder[63], 63)
	return e
}

// symbol adds symbol c with index i to the decoding map of e (an
// iteration of the loop of NewEncoding).
func symbol(e *Encoding, c byte, i uint8) {
	switch {
	case c == '\n' || c == '\r':
		panic("encoding alphabet contains newline character")
	case e.decodeMap[c] != invalidIndex:
		panic("encoding alphabet includes duplicate symbols")
	}
	e.decodeMap[c] = i
}
