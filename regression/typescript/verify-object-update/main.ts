const original = { x: 1, y: 2, z: 3 };
const updated = { ...original, y: 20 };
console.assert(updated.x === 1);
console.assert(updated.y === 20);
console.assert(updated.z === 3);
