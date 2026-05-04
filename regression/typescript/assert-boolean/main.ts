// ES2024 sec-ecmascript-language-types-boolean-type: The Boolean Type
// "The Boolean type represents a logical entity having two values,
//  called true and false."
//
// ES2024 sec-logical-not-operator: Logical NOT Operator (!)
// ES2024 sec-isstrictlyequal: IsStrictlyEqual (===)
const a: boolean = true;
const b: boolean = false;
console.assert(a === true);
console.assert(b === false);
console.assert(a && !b);
console.assert(a || b);
