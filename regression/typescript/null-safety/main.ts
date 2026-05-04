// TSH: Narrowing.md — Type narrowing via control flow analysis
// "TypeScript follows possible paths of execution that our programs can
//  take to analyze the most specific possible type of a value at a given
//  position."
//
// TSH: Everyday Types.md — Union types
// "A union type is a type formed from two or more other types,
//  representing values that may be any one of those types."
//
// ES2024 sec-ecmascript-language-types-null-type: The Null Type
// "The Null type has exactly one value, called null."
//
// ES2024 sec-isstrictlyequal: null === null is true
// ES2024 sec-addition-operator-plus: String concatenation
function greet(name: string | null): string {
  if (name === null) {
    return "Hello, stranger!";
  }
  return "Hello, " + name + "!";
}
console.assert(greet("Alice") === "Hello, Alice!");
console.assert(greet(null) === "Hello, stranger!");
