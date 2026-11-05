// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the standard logger writes to stderr,
// which stands for os.Stderr; output reads the time only if the header
// shows it.

// Package log overrides the standard logger of the standard library's log
// (go1.27), whose source is the model otherwise: it writes to stderr
// rather than os.Stderr (package os has no model), so that the package's
// initialisation does not need package os. The Fatal functions call
// os.Exit, and are reported where they are reached.
package log

import (
	"runtime"
	"time"
)

var std = New(stderr{}, "", LstdFlags)

// stderr stands for os.Stderr: writes succeed; the output is not
// modelled.
type stderr struct{}

func (stderr) Write(p []byte) (int, error) { return len(p), nil }

func (l *Logger) output(pc uintptr, calldepth int, appendOutput func([]byte) []byte) error {
	if l.isDiscard.Load() {
		return nil
	}

	// Load prefix and flag once so that their value is consistent within
	// this call regardless of any concurrent changes to their value.
	prefix := l.Prefix()
	flag := l.Flags()

	// The time is read only if the header shows it.
	var now time.Time
	if flag&(Ldate|Ltime|Lmicroseconds) != 0 {
		now = time.Now()
	}

	var file string
	var line int
	if flag&(Lshortfile|Llongfile) != 0 {
		if pc == 0 {
			var ok bool
			_, file, line, ok = runtime.Caller(calldepth)
			if !ok {
				file = "???"
				line = 0
			}
		} else {
			fs := runtime.CallersFrames([]uintptr{pc})
			f, _ := fs.Next()
			file = f.File
			if file == "" {
				file = "???"
			}
			line = f.Line
		}
	}

	buf := getBuffer()
	defer putBuffer(buf)
	formatHeader(buf, now, prefix, flag, file, line)
	*buf = appendOutput(*buf)
	if len(*buf) == 0 || (*buf)[len(*buf)-1] != '\n' {
		*buf = append(*buf, '\n')
	}

	l.outMu.Lock()
	defer l.outMu.Unlock()
	_, err := l.out.Write(*buf)
	return err
}
