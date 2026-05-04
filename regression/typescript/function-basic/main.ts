// ES2024 sec-function-definitions: Function Definitions
// "A function definition defines a function object."
//
// ES2024 sec-function-calls: Function Calls
// "The production CallExpression : CoverCallExpressionAndAsyncArrowHead
//  is evaluated as follows..."
function add(a: number, b: number): number {
  return a + b;
}
console.assert(add(2, 3) === 5);
console.assert(add(-1, 1) === 0);
