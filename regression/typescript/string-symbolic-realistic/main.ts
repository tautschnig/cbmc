// End-to-end: composite symbolic-string operations that require
// the refined-string solver to verify precisely.
function format(prefix: string, value: string): string {
  return prefix + ": " + value.trim();
}

const p: string = nondet_string();
const v: string = nondet_string();
__CPROVER_assume(p === "foo");
__CPROVER_assume(v === "  bar  ");
const result: string = format(p, v);
console.assert(result === "foo: bar");

// Predicates on the symbolic result
console.assert(result.startsWith("foo:"));
console.assert(result.endsWith("bar"));
console.assert(!result.includes("baz"));
console.assert(result.toUpperCase() === "FOO: BAR");
