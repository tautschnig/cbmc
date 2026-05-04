// ES2024 sec-function-definitions: nested functions
function outer(x: number): number {
  function inner(y: number): number {
    return x + y;
  }
  return inner(10);
}
console.assert(outer(5) === 15);
