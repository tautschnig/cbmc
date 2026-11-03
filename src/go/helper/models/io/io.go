// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: only the error variables of package io
// (go1.27), which models of variables replace (see modelVar): the
// interfaces and functions of the real package mention its own types,
// which those of a model could not be identical to.

// Package io models the error variables of the standard library's io.
package io

import "errors"

// ErrShortWrite means that a write accepted fewer bytes than requested
// but failed to return an explicit error.
var ErrShortWrite = errors.New("short write")

// ErrShortBuffer means that a read required a longer buffer than was
// provided.
var ErrShortBuffer = errors.New("short buffer")

// EOF is the error returned by Read when no more input is available.
var EOF = errors.New("EOF")

// ErrUnexpectedEOF means that EOF was encountered in the middle of reading
// a fixed-size block or data structure.
var ErrUnexpectedEOF = errors.New("unexpected EOF")

// ErrNoProgress is returned by some clients of a Reader when many calls to
// Read have failed to return any data or error, usually the sign of a
// broken Reader implementation.
var ErrNoProgress = errors.New("multiple Read calls return no data or error")

// ErrClosedPipe is the error used for read or write operations on a closed
// pipe.
var ErrClosedPipe = errors.New("io: read/write on closed pipe")
