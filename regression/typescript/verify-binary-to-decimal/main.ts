function binToDec(bits: number[]): number {
  let result: number = 0;
  for (let i = 0; i < bits.length; i++) {
    result = result * 2 + bits[i];
  }
  return result;
}
const b: number[] = [1, 0, 1, 1]; // 11
console.assert(binToDec(b) === 11);
