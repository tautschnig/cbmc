const x: number = 5;
const y: number = x > 3 ? x : 0;
console.assert(y === 5);
const z: number = x > 10 ? x : 0;
console.assert(z === 0);
