// ES2024 sec-ecmascript-language-types-string-type: The String Type
// "The String type is the set of all ordered sequences of zero or more
//  16-bit unsigned integer values."
//
// ES2024 sec-addition-operator-plus: String concatenation
// "If lprim is a String or rprim is a String, then let lstr be
//  ToString(lprim) ... return the string-concatenation of lstr and rstr."
//
// ES2024 sec-isstrictlyequal: String equality
// "If x is a String, then if x and y are exactly the same sequence of
//  code units, return true; otherwise, return false."
const s: string = "hello";
console.assert(s.length === 5);
console.assert(s === "hello");
const t: string = s + " world";
console.assert(t === "hello world");
