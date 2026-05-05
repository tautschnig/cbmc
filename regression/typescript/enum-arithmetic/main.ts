enum Op { Add = 1, Sub = 2, Mul = 3 }
function calc(a: number, b: number, op: Op): number {
  if (op === Op.Add) return a + b;
  if (op === Op.Sub) return a - b;
  return a * b;
}
console.assert(calc(3, 4, Op.Add) === 7);
console.assert(calc(10, 3, Op.Sub) === 7);
console.assert(calc(3, 4, Op.Mul) === 12);
