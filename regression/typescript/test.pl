#!/usr/bin/env perl
# Wrapper for TypeScript regression tests
# Uses the same test.pl framework as other CBMC regression tests
exec("perl", "../test.pl", @ARGV);
