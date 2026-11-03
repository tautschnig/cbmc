// Packages io and encoding/binary are their real source: io.WriteString,
// io.ReadFull, the byte orders and varints.
package main

import (
	"bytes"
	"cprover"
	"encoding/binary"
	"errors"
	"io"
	"strings"
)

func main() {
	var b bytes.Buffer
	n, err := io.WriteString(&b, "abc")
	cprover.Assert(n == 3 && err == nil && b.String() == "abc", "WriteString")
	buf := make([]byte, 4)
	_, err = io.ReadFull(strings.NewReader("ab"), buf)
	cprover.Assert(errors.Is(err, io.ErrUnexpectedEOF), "ReadFull")

	out := make([]byte, 4)
	binary.BigEndian.PutUint32(out, 0x01020304)
	cprover.Assert(out[0] == 1 && binary.LittleEndian.Uint16(out) == 0x0201,
		"byte orders")
	vb := binary.AppendUvarint(nil, 300)
	v, k := binary.Uvarint(vb)
	cprover.Assert(v == 300 && k == 2, "varint")
}
