// Symbolic-input review: Map and Set handle symbolic values correctly.
// The linear-scan implementation naturally handles symbolic scenarios
// because equality comparisons are emitted as symbolic predicates.
const m: Map<string, number> = new Map();
const v: number = nondet_number();
__CPROVER_assume(v >= 0 && v <= 100);

m.set("a", v);
m.set("b", v + 1);

console.assert(m.has("a"));
console.assert(m.get("a") === v);
console.assert(m.get("b") === v + 1);
console.assert(m.size === 2);

// Set with symbolic values — dedup relies on symbolic equality
const s: Set<number> = new Set();
s.add(v);
s.add(v + 1);
console.assert(s.has(v));
console.assert(s.has(v + 1));
console.assert(s.size === 2);

// Re-adding v doesn't change size (dedup works symbolically)
s.add(v);
console.assert(s.size === 2);

// Delete a symbolic-valued element
s.delete(v);
console.assert(!s.has(v));
console.assert(s.has(v + 1));
console.assert(s.size === 1);
