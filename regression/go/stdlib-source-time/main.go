// Package time is its real source, with the runtime's clock, timers and
// the local time zone modelled: the monotonic clock, durations, formatting.
package main

import (
	"cprover"
	"time"
)

func main() {
	start := time.Now()
	time.Sleep(10 * time.Millisecond)
	cprover.Assert(time.Since(start) >= 10*time.Millisecond, "monotonic")
	d, err := time.ParseDuration("1h30m")
	cprover.Assert(err == nil && d == 90*time.Minute, "ParseDuration")
	cprover.Assert(d.String() == "1h30m0s", "Duration.String")
	t := time.Unix(86400, 0).UTC()
	cprover.Assert(t.Year() == 1970 && t.Day() == 2, "Unix")
	cprover.Assert(t.Add(time.Hour).After(t), "After")
	cprover.Assert(t.Format(time.RFC3339) == "1970-01-02T00:00:00Z", "Format")
}
