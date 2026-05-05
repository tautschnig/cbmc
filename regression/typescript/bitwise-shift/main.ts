const x: number = 0xFF;
console.assert((x & 0x0F) === 15);
console.assert((x >> 4) === 15);
console.assert((1 << 8) === 256);
