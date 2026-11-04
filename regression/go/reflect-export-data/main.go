// Standard-library packages without a model whose signatures mention
// reflect's types (encoding/gob's Encoder.EncodeValue takes a
// reflect.Value): the program type-checks against the real package, and
// its uses of reflect denote the model.
package main

import (
	"bytes"
	"cprover"
	"encoding/gob"
	"reflect"
)

func main() {
	v := reflect.ValueOf(3)
	cprover.Assert(v.Int() == 3 && v.Kind() == reflect.Int, "model")
	if cprover.Nondet[bool]() {
		var buf bytes.Buffer
		gob.NewEncoder(&buf).EncodeValue(v)
	}
}
