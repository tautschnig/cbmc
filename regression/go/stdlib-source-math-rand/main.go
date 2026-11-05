// Package math/rand, modelled by its own source but with arbitrary values
// for its generators (plan §7.1): the properties hold for every value that
// a seed or the runtime's random seeding could produce.
package main

import (
	"cprover"
	"math/rand"
	"time"
)

// As go-cmp does when its package is initialised.
var randBool = rand.New(rand.NewSource(time.Now().Unix())).Intn(2) == 0

// counter is a program's own Source, which math/rand samples as gc does.
type counter struct{ n int64 }

func (c *counter) Int63() int64    { c.n++; return c.n }
func (c *counter) Seed(seed int64) { c.n = seed }

func main() {
	n := rand.Intn(10)
	cprover.Assert(n >= 0 && n < 10, "Intn is in [0,n)")
	f := rand.Float64()
	cprover.Assert(f >= 0 && f < 1, "Float64 is in [0,1)")
	p := rand.Perm(3)
	seen := [3]bool{}
	for _, v := range p {
		seen[v] = true
	}
	cprover.Assert(seen[0] && seen[1] && seen[2], "Perm is a permutation")
	s := []int{1, 2, 3}
	rand.Shuffle(len(s), func(i, j int) { s[i], s[j] = s[j], s[i] })
	cprover.Assert(s[0]+s[1]+s[2] == 6, "Shuffle keeps the elements")

	r := rand.New(rand.NewSource(42))
	cprover.Assert(r.Int63n(5) < 5, "a seeded source")

	c := rand.New(&counter{})
	cprover.Assert(c.Int63() == 1 && c.Int63() == 2, "a program's source")
	cprover.Assert(c.Intn(2) == 0, "sampled as gc does: (3>>32)&1")

	cprover.Assert(randBool, "the values are arbitrary")
}
