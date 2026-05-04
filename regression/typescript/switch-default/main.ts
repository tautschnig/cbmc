const x: number = 3;
let result: number = 0;
switch (x) {
  case 1: result = 10; break;
  case 2: result = 20; break;
  default: result = 99; break;
}
console.assert(result === 99);
