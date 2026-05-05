function extractByte(value: number, byteIndex: number): number {
  return (value >> (byteIndex * 8)) & 0xFF;
}
console.assert(extractByte(0x12345678, 0) === 0x78);
console.assert(extractByte(0x12345678, 1) === 0x56);
