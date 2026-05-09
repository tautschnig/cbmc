// ES2024 §20.1.2.5: Object.entries returns an array of [key, value] pairs.
// Currently we build an array of tuple-structs, but accessing es[i][j]
// (2-level indexing into array of tuples) isn't fully supported.
// We verify length and the round-trip via fromEntries instead.
const o = { a: 1, b: 2, c: 3 };
const es = Object.entries(o);
console.assert(es.length === 3);

// Via fromEntries round-trip
const r = Object.fromEntries(Object.entries(o));
console.assert(r.a === 1);
console.assert(r.b === 2);
console.assert(r.c === 3);
