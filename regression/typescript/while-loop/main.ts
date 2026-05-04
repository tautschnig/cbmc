// ES2024 sec-while-statement (§14.7.3): The while Statement
// "1. Repeat,
//    a. Let exprRef be ? Evaluation of Expression.
//    b. Let exprValue be ToBoolean(? GetValue(exprRef)).
//    c. If exprValue is false, return V."
//
// ES2024 sec-assignment-operators: Compound assignment (+=)
let sum: number = 0;
let i: number = 1;
while (i <= 10) {
  sum += i;
  i++;
}
console.assert(sum === 55);
