// Package strconv is modelled by its own source (with internal/strconv;
// its integer formatting is replaced, see sourceModels in
// src/go/helper/models.go). Its errors have the real types.
package main

import (
	"cprover"
	"errors"
	"strconv"
)

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(-1000 < n && n < 1000)
	m, err := strconv.Atoi(strconv.Itoa(n))
	cprover.Assert(err == nil && m == n, "round trip")
	b, err := strconv.ParseBool("true")
	cprover.Assert(err == nil && b, "ParseBool")
	_, err = strconv.Atoi("x")
	cprover.Assert(errors.Is(err, strconv.ErrSyntax), "ErrSyntax")
	ne, ok := err.(*strconv.NumError)
	cprover.Assert(ok && ne.Func == "Atoi" && ne.Num == "x", "NumError")
}
