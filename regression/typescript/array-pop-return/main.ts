// KNOWNBUG: pop() correctly removes the element but the returned
// value isn't tracked through the assignment.
// ES2024 sec-array.prototype.pop
const a: number[] = [1, 2, 3];
const popped: number = a.pop() as number;
console.assert(popped === 3);
