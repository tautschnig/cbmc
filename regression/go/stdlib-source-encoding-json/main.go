// encoding/json from its source (go1's implementation), on the model of
// reflect: a struct with tags marshalled and unmarshalled back, and a
// syntax error.
package main

import (
	"cprover"
	"encoding/json"
)

type Item struct {
	ID   int      `json:"id"`
	Tags []string `json:"tags,omitempty"`
	Note string   `json:"-"`
}

func main() {
	b, err := json.Marshal(Item{ID: 2, Tags: []string{"x"}, Note: "n"})
	cprover.Assert(err == nil && string(b) == `{"id":2,"tags":["x"]}`, "marshal")
	var it Item
	err = json.Unmarshal(b, &it)
	cprover.Assert(err == nil && it.ID == 2 && len(it.Tags) == 1 && it.Tags[0] == "x", "unmarshal")
	err = json.Unmarshal([]byte(`{"id":`), &it)
	_, isSyntax := err.(*json.SyntaxError)
	cprover.Assert(isSyntax, "syntax error")
}
