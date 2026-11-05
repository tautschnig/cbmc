// Package bufio, modelled by its own source (plan §4.5).
package main

import (
	"bufio"
	"bytes"
	"cprover"
	"strings"
)

func main() {
	sc := bufio.NewScanner(strings.NewReader("ab c\nd"))
	var lines []string
	for sc.Scan() {
		lines = append(lines, sc.Text())
	}
	cprover.Assert(sc.Err() == nil, "no error")
	cprover.Assert(len(lines) == 2 && lines[0] == "ab c" && lines[1] == "d",
		"ScanLines splits at newlines")

	ws := bufio.NewScanner(strings.NewReader(" x  yz "))
	ws.Split(bufio.ScanWords)
	n := 0
	for ws.Scan() {
		n++
	}
	cprover.Assert(n == 2, "ScanWords skips spaces")

	r := bufio.NewReader(strings.NewReader("k=v;rest"))
	s, err := r.ReadString(';')
	cprover.Assert(err == nil && s == "k=v;", "ReadString includes the delimiter")
	b, err := r.Peek(2)
	cprover.Assert(err == nil && string(b) == "re", "Peek does not advance")
	c, size, _ := r.ReadRune()
	cprover.Assert(c == 'r' && size == 1, "ReadRune")
	rest, err := r.ReadString(0)
	cprover.Assert(rest == "est" && err != nil, "at the end, the error is set")

	var buf bytes.Buffer
	w := bufio.NewWriter(&buf)
	w.WriteString("hi")
	w.WriteByte('!')
	cprover.Assert(buf.Len() == 0 && w.Buffered() == 3, "writes are buffered")
	cprover.Assert(w.Available() == 4096-3, "in a buffer of the default size")
	w.Flush()
	cprover.Assert(buf.String() == "hi!", "until Flush")
}
