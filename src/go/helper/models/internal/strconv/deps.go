// Copyright 2025 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: package internal/strconv is the real
// one (go1.27, see sourceModels in models.go) except for these
// conversions, which reinterpret memory through package unsafe there.

package strconv

import "math"

func float64frombits(b uint64) float64 { return math.Float64frombits(b) }
func float32frombits(b uint32) float32 { return math.Float32frombits(b) }
func float64bits(f float64) uint64     { return math.Float64bits(f) }
func float32bits(f float32) uint32     { return math.Float32bits(f) }
