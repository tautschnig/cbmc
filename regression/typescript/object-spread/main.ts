const a = { x: 1, y: 2 };
const b = { ...a, z: 3 };
console.assert(b.x === 1);
console.assert(b.z === 3);
