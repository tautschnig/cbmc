const obj = { x: 1, y: 2, z: 3 };
let count: number = 0;
for (const key in obj) { count++; }
console.assert(count === 3);
