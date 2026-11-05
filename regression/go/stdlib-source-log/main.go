// Package log, modelled by its own source (plan §4.5): the standard logger
// writes to a model of os.Stderr; the text that fmt formats is not
// modelled (arbitrary), but prefixes and flags are.
package main

import (
	"bytes"
	"cprover"
	"log"
	"strings"
)

func main() {
	log.SetFlags(0)
	log.SetPrefix("app: ")
	log.Println("hello")
	log.Printf("x=%d", 3)
	cprover.Assert(log.Prefix() == "app: " && log.Flags() == 0,
		"the standard logger's settings")

	var buf bytes.Buffer
	l := log.New(&buf, "pfx: ", 0)
	l.Print("msg")
	out := buf.String()
	cprover.Assert(strings.HasPrefix(out, "pfx: "), "the prefix is written")
	cprover.Assert(strings.HasSuffix(out, "\n"), "and a newline at the end")
	cprover.Assert(l.Writer() == &buf, "Writer")
}
