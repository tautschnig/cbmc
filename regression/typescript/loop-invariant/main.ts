declare function __CPROVER_loop_invariant(cond: boolean): void;

let sum: number = 0;
for (let i = 0; i < 5; i++) {
  sum = sum + i;
  __CPROVER_loop_invariant(sum >= 0);
}
console.assert(sum === 10);
