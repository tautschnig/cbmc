const obj = { a: 10, b: 20, c: 30 };
let count: number = 0;
for (const key in obj) { count += 1; }
console.assert(count === 3);
