declare function __CPROVER_loop_invariant(cond: boolean): void;

let x: number = 10;
for (let i = 0; i < 5; i++) {
  x = x - 3;
  __CPROVER_loop_invariant(x > 0); // fails when x goes negative
}
