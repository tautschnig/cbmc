// ES2024 sec-addition-operator-plus: when one operand is a string,
// the other (number/boolean) is coerced via ToString.
const s1: string = "x" + 1;
console.assert(s1 === "x1");

const s2: string = 1 + "x";
console.assert(s2 === "1x");

const s3: string = "x" + true;
console.assert(s3 === "xtrue");

const s4: string = "v=" + 42 + ",b=" + false;
console.assert(s4 === "v=42,b=false");

// Float formatting - integer values should not show decimal point
const s5: string = "n=" + 100;
console.assert(s5 === "n=100");
