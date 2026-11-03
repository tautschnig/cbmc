// Packages encoding/hex, hash/fnv and unicode/utf16 are modelled by their
// own source, and so are the error variables of package io (see
// sourceModels in src/go/helper/models.go).
package main

import (
	"cprover"
	"encoding/hex"
	"hash/fnv"
	"io"
	"unicode/utf16"
)

type reader struct{ n int }

func (r *reader) Read(p []byte) (int, error) {
	if r.n == 0 {
		return 0, io.EOF
	}
	r.n--
	p[0] = 'x'
	return 1, nil
}

func main() {
	cprover.Assert(hex.EncodeToString([]byte{0xab, 1}) == "ab01", "hex")
	_, err := hex.DecodeString("a")
	cprover.Assert(err == hex.ErrLength, "hex.ErrLength")
	h := fnv.New32a()
	h.Write([]byte("a"))
	cprover.Assert(h.Sum32() == 0xe40c292c, "fnv")
	cprover.Assert(len(utf16.Encode([]rune{0x10437})) == 2, "utf16")

	r := &reader{n: 1}
	buf := make([]byte, 1)
	_, err = r.Read(buf)
	cprover.Assert(err == nil, "read")
	_, err = r.Read(buf)
	cprover.Assert(err == io.EOF && err.Error() == "EOF", "io.EOF")
}
