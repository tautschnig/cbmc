// ES2024 sec-if-statement (§14.6): The if Statement
// "The production IfStatement : if ( Expression ) Statement else Statement
//  is evaluated as follows:
//  1. Let exprRef be ? Evaluation of Expression.
//  2. Let exprValue be ToBoolean(? GetValue(exprRef)).
//  3. If exprValue is true, then ... Else ..."
//
// ES2024 sec-relational-operators: Relational Operators (<, >, <=, >=)
function abs(x: number): number {
  if (x < 0) {
    return -x;
  }
  return x;
}
console.assert(abs(5) === 5);
console.assert(abs(-3) === 3);
console.assert(abs(0) === 0);
