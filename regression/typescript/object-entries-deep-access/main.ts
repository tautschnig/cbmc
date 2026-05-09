// KNOWNBUG: Deep indexing into Object.entries result (array of tuples).
// ES2024 §20.1.2.5. Object.entries returns an array of 2-tuples,
// but es[i][j] 2-level indexing doesn't resolve the tuple struct
// at array slot i to extract field _j.
const o = { a: 1, b: 2 };
const es = Object.entries(o);
console.assert(es[0][0] === "a");
console.assert(es[0][1] === 1);
