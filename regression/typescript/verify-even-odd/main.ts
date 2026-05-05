function isEven(n: number): boolean {
  return (n & 1) === 0;
}
console.assert(isEven(4) === true);
console.assert(isEven(7) === false);
console.assert(isEven(0) === true);
