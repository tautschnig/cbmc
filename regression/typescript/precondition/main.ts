declare function __CPROVER_requires(cond: boolean): void;
declare function __CPROVER_ensures(cond: boolean): void;

function safeSqrt(x: number): number {
  __CPROVER_requires(x >= 0);
  const result: number = x;
  __CPROVER_ensures(result >= 0);
  return result;
}
const r: number = safeSqrt(4);
console.assert(r >= 0);
