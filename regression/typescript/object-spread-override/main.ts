const defaults = { x: 0, y: 0, z: 0 };
const custom = { ...defaults, x: 10, y: 20 };
console.assert(custom.x === 10);
console.assert(custom.y === 20);
console.assert(custom.z === 0);
