// Package text/template, modelled by its own source (plan §4.5), executing
// on the model of reflect. Functions that read files (ParseFiles,
// ParseGlob) call package os, and urlquery calls net/url, which have no
// models: they are reported where they are reached.
package main

import (
	"cprover"
	"strings"
	"text/template"
)

func main() {
	t := template.Must(template.New("t").Parse("hi {{.}}!"))
	var b strings.Builder
	err := t.Execute(&b, "bob")
	cprover.Assert(err == nil, "Execute succeeds")
	cprover.Assert(b.String() == "hi bob!", "and writes the text")
}
