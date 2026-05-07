// KNOWNBUG: a ?? b returns a instead of b when a is undefined.
// ES2024 sec-logical-nullish-assignment (§13.13)
const a: number | undefined = undefined;
const b: number = a ?? 10;
console.assert(b === 10);
