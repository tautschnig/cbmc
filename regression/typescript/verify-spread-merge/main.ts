const a = { x: 1, y: 2 };
const b = { y: 20, z: 30 };
const merged = { ...a, ...b };
console.assert(merged.x === 1);
console.assert(merged.y === 20);
console.assert(merged.z === 30);
