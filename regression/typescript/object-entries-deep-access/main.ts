// ES2024 §20.1.2.5: Object.entries returns an array of [key, value]
// pairs. Deep indexing es[i][j] resolves the tuple-struct at the
// array slot and extracts field _j.
const o = { a: 1, b: 2 };
const es = Object.entries(o);
console.assert(es[0][0] === "a");
console.assert(es[0][1] === 1);
console.assert(es[1][0] === "b");
console.assert(es[1][1] === 2);

// Array-of-tuples declared directly
const pairs: [string, number][] = [["x", 10], ["y", 20]];
console.assert(pairs[0][0] === "x");
console.assert(pairs[0][1] === 10);
console.assert(pairs[1][0] === "y");
console.assert(pairs[1][1] === 20);
