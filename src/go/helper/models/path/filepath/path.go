// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: lstat calls os.Lstat.

// Package filepath overrides the variable lstat of the standard library's
// path/filepath (go1.27), whose source is the model otherwise: a function
// that calls os.Lstat rather than its function value, so that the
// package's initialisation does not need package os, which has no model;
// calls of os.Lstat are reported where they are reached.
package filepath

import (
	"io/fs"
	"os"
)

var lstat = func(name string) (fs.FileInfo, error) { return os.Lstat(name) }
