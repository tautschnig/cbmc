function isPowerOfTwo(n: number): boolean {
  if (n <= 0) return false;
  return (n & (n - 1)) === 0;
}
console.assert(isPowerOfTwo(1) === true);
console.assert(isPowerOfTwo(2) === true);
console.assert(isPowerOfTwo(4) === true);
console.assert(isPowerOfTwo(8) === true);
console.assert(isPowerOfTwo(3) === false);
console.assert(isPowerOfTwo(6) === false);
