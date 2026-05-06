const p = { x: 10, y: 20, z: 30 };
const q = { ...p, y: 99 };
console.assert(q.x === 10);
console.assert(q.y === 99);
console.assert(q.z === 30);
