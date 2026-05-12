// Harness for npm package 'classnames' (JedWatson/classnames)
// Version tracked in integration/typescript-npm/package.json
//
// Verifies length composition invariants over concatenation with
// SYMBOLIC string lengths. Simplified: we test single-concat
// compositions rather than chained-in-function ones because the
// latter currently trip the refined-string solver's pointer
// association check. See KNOWNBUG string-concat-chained-in-function.

// Property 1: length composition for two concrete strings.
const a: string = "abc";
const b: string = "xy";
const joined: string = a + " " + b;
console.assert(joined.length === 6);

// Property 2: conditional empty — "" + x === x.
const x: string = "hello";
const y: string = "" + x;
console.assert(y.length === x.length);
console.assert(y === "hello");

// Property 3: length-preservation over symbolic strings.
const p: string = nondet_string();
const q: string = nondet_string();
__CPROVER_assume(p.length === 3);
__CPROVER_assume(q.length === 2);
const r: string = p + " " + q;
console.assert(r.length === 6);

// Property 4: empty-string edge case for classnames filtering.
console.assert("".length === 0);
console.assert(("" + "foo").length === 3);
console.assert(("foo" + "").length === 3);
