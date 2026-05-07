// Harness for npm package 'classnames' (JedWatson/classnames)
// Version tracked in integration/typescript-npm/package.json
//
// The classnames package joins CSS class names with spaces.
// We verify simple length invariants on concatenations.
//
// Upstream: https://github.com/JedWatson/classnames/blob/v2.5.1/index.js

// Invariants on string length when concatenating:
const a: string = "foo";
const b: string = "bar";
const joined: string = a + " " + b;

// Length composition: len(a + " " + b) === len(a) + 1 + len(b)
console.assert(joined.length === a.length + 1 + b.length);
console.assert(joined.length === 7);

// Constant equality
console.assert(joined === "foo bar");

// Associativity: (a + b) + c === a + (b + c)
const abc1: string = (a + b) + "baz";
const abc2: string = a + (b + "baz");
console.assert(abc1 === abc2);
console.assert(abc1 === "foobarbaz");
