function power(base: number, exp: number): number {
  let result: number = 1;
  for (let i = 0; i < exp; i++) {
    result = result * base;
  }
  return result;
}
console.assert(power(2, 0) === 1);
console.assert(power(2, 3) === 8);
console.assert(power(3, 2) === 9);
