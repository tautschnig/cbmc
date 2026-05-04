const a: number = 3;
const b: number = 7;
let result: number = 0;
if (a > 0) {
  if (b > 5) { result = 1; }
  else { result = 2; }
} else { result = 3; }
console.assert(result === 1);
