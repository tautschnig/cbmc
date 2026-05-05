const x: number = nondet_number();
__CPROVER_assume(x >= 0 && x <= 3);
let result: number = 0;
switch (x) {
  case 0: result = 10; break;
  case 1: result = 20; break;
  case 2: result = 30; break;
  default: result = 40; break;
}
console.assert(result >= 10);
console.assert(result <= 40);
