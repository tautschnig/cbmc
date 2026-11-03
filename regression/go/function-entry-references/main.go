// With --function, parameters with references get arbitrary values as
// cprover.Nondet gives them: here a nil or non-nil pointer, a map and an
// interface value, so that the nil checks matter.
package main

import "cprover"

type Account struct {
	Owner   string
	Balance int
}

type Store interface{ Get(id int) *Account }

type memory map[int]*Account

func (m memory) Get(id int) *Account { return m[id] }

func Transfer(from, to *Account, amount int) {
	if from == nil || to == nil || amount <= 0 || from.Balance < amount {
		return
	}
	from.Balance -= amount
	to.Balance += amount
}

func Total(s Store, ids map[int]bool) int {
	t := 0
	for id := range ids {
		if a := s.Get(id); a != nil {
			t += a.Balance
		}
	}
	return t
}

func Check(from, to *Account, amount int) {
	cprover.Assume(from != to)
	cprover.Assume(from == nil || from.Balance < 1000)
	cprover.Assume(to == nil || to.Balance < 1000)
	if from != nil && to != nil {
		sum := from.Balance + to.Balance
		Transfer(from, to, amount)
		cprover.Assert(from.Balance+to.Balance == sum, "conserved")
	}
}

func main() {}
