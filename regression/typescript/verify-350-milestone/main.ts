// 350th CORE test — comprehensive verification
function clamp(val: number, min: number, max: number): number {
  if (val < min) return min;
  if (val > max) return max;
  return val;
}
const x: number = nondet_number();
__CPROVER_assume(x > -1000 && x < 1000);
const clamped: number = clamp(x, 0, 255);
console.assert(clamped >= 0);
console.assert(clamped <= 255);
