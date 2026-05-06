// 475th test — comprehensive integration
function gcd(a: number, b: number): number {
  while (b !== 0) { const t = b; b = a % b; a = t; }
  return a;
}
const result: number = gcd(12, 8);
console.assert(result === 4);
const result2: number = gcd(17, 5);
console.assert(result2 === 1);
