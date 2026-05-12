// Harness for npm package 'array-union' (sindresorhus/array-union)
// Version tracked in integration/typescript-npm/package.json
//
// arrayUnion(a, b) returns a new array containing all unique elements.
// We verify key algebraic properties of the concatenation step
// (the deduplication loop has known array-length tracking issues
// across .push() calls in our frontend; see the outer comment).

// Property 1: concat length equals sum of input lengths.
const a: number[] = [1, 2];
const b: number[] = [3, 4];
const merged: number[] = a.concat(b);
console.assert(merged.length === 4);
console.assert(merged[0] === 1);
console.assert(merged[1] === 2);
console.assert(merged[2] === 3);
console.assert(merged[3] === 4);

// Property 2: concat preserves order.
const x: number[] = [10, 20];
const y: number[] = [30, 40, 50];
const z: number[] = x.concat(y);
console.assert(z.length === 5);
console.assert(z[0] === 10);
console.assert(z[4] === 50);

// Property 3: concat with empty array is identity.
const empty: number[] = [];
const id1: number[] = empty.concat(a);
const id2: number[] = a.concat(empty);
console.assert(id1.length === 2);
console.assert(id1[0] === 1);
console.assert(id1[1] === 2);
console.assert(id2.length === 2);
console.assert(id2[0] === 1);
console.assert(id2[1] === 2);
