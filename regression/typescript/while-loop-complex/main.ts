let n: number = 10;
let result: number = 1;
while (n > 1) {
  result *= n;
  n--;
}
// 10! = 3628800
console.assert(result === 3628800);
