// KNOWNBUG: Array.prototype.sort with a custom comparator doesn't
// actually reorder the elements. Without comparator, also limited.
// ES2024 sec-array.prototype.sort
const a: number[] = [3, 1, 2];
a.sort((x: number, y: number) => x - y);
console.assert(a[0] === 1);
console.assert(a[1] === 2);
console.assert(a[2] === 3);
