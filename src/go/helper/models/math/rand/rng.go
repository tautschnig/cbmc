// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the generator's values are arbitrary,
// and the GODEBUG settings have their defaults.

// Package rand replaces rng.go of the standard library's math/rand
// (go1.27), whose other files are the model, and overrides the functions
// that use the runtime's generator or GODEBUG settings.
//
// The values of the generators are arbitrary: those of the source that
// NewSource returns cover every seed (a program that relies on the
// sequence of a particular seed may fail properties that gc satisfies),
// and those of the runtime's generator, which seeds the top-level
// functions randomly, are arbitrary anyway.
package rand

import "cprover"

const (
	rngLen   = 607
	rngTap   = 273
	rngMax   = 1 << 63
	rngMask  = rngMax - 1
	int32max = (1 << 31) - 1
)

// rngSource is the source that NewSource returns.
type rngSource struct{}

// Seed uses the provided seed value to initialize the generator to a
// deterministic state.
func (rng *rngSource) Seed(seed int64) {}

// Int63 returns a non-negative pseudo-random 63-bit integer as an int64.
func (rng *rngSource) Int63() int64 {
	return int64(rng.Uint64() & rngMask)
}

// Uint64 returns a non-negative pseudo-random 64-bit integer as a uint64.
func (rng *rngSource) Uint64() uint64 {
	return cprover.Nondet[uint64]()
}

func runtime_rand() uint64 {
	return cprover.Nondet[uint64]()
}

// The GODEBUG settings (internal/godebug) are not modelled: randautoseed
// and randseednop have their defaults, see globalRand and Seed.
var randautoseed, randseednop struct{}

// globalRand returns the generator to use for the top-level convenience
// functions.
func globalRand() *Rand {
	if r := globalRandGenerator.Load(); r != nil {
		return r
	}

	// This is the first call (with randautoseed=1).
	r := &Rand{
		src: &runtimeSource{},
		s64: &runtimeSource{},
	}

	if !globalRandGenerator.CompareAndSwap(nil, r) {
		// Two different goroutines called some top-level
		// function at the same time.
		return globalRandGenerator.Load()
	}

	return r
}

// Seed is a no-op (with randseednop=1, the default as of Go 1.24).
func Seed(seed int64) {}

// arbitrary reports whether r's values are arbitrary (its source is the
// model's): then resampling until a condition holds, as the methods
// below do, is the same as assuming the condition of the first sample.
// Other sources (the program's own) are sampled as gc does.
func (r *Rand) arbitrary() bool {
	switch r.src.(type) {
	case *rngSource, *runtimeSource:
		return true
	}
	return false
}

// Int63n returns, as an int64, a non-negative pseudo-random number in the half-open interval [0,n).
// It panics if n <= 0.
func (r *Rand) Int63n(n int64) int64 {
	if n <= 0 {
		panic("invalid argument to Int63n")
	}
	if n&(n-1) == 0 { // n is power of two, can mask
		return r.Int63() & (n - 1)
	}
	max := int64((1 << 63) - 1 - (1<<63)%uint64(n))
	v := r.Int63()
	if r.arbitrary() {
		cprover.Assume(v <= max)
		return v % n
	}
	for v > max {
		v = r.Int63()
	}
	return v % n
}

// Int31n returns, as an int32, a non-negative pseudo-random number in the half-open interval [0,n).
// It panics if n <= 0.
func (r *Rand) Int31n(n int32) int32 {
	if n <= 0 {
		panic("invalid argument to Int31n")
	}
	if n&(n-1) == 0 { // n is power of two, can mask
		return r.Int31() & (n - 1)
	}
	max := int32((1 << 31) - 1 - (1<<31)%uint32(n))
	v := r.Int31()
	if r.arbitrary() {
		cprover.Assume(v <= max)
		return v % n
	}
	for v > max {
		v = r.Int31()
	}
	return v % n
}

// int31n returns, as an int32, a non-negative pseudo-random number in the half-open interval [0,n).
// n must be > 0, but int31n does not check this; the caller must ensure it.
func (r *Rand) int31n(n int32) int32 {
	v := r.Uint32()
	prod := uint64(v) * uint64(n)
	low := uint32(prod)
	if low < uint32(n) {
		thresh := uint32(-n) % uint32(n)
		if r.arbitrary() {
			cprover.Assume(low >= thresh)
			return int32(prod >> 32)
		}
		for low < thresh {
			v = r.Uint32()
			prod = uint64(v) * uint64(n)
			low = uint32(prod)
		}
	}
	return int32(prod >> 32)
}

// Float64 returns, as a float64, a pseudo-random number in the half-open interval [0.0,1.0).
func (r *Rand) Float64() float64 {
again:
	f := float64(r.Int63()) / (1 << 63)
	if f == 1 {
		if r.arbitrary() {
			cprover.Assume(false)
		}
		goto again // resample; this branch is taken O(never)
	}
	return f
}

// Float32 returns, as a float32, a pseudo-random number in the half-open interval [0.0,1.0).
func (r *Rand) Float32() float32 {
again:
	f := float32(r.Float64())
	if f == 1 {
		if r.arbitrary() {
			cprover.Assume(false)
		}
		goto again // resample; this branch is taken O(very rarely)
	}
	return f
}
