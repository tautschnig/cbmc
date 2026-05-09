// ES2024 §20.1.2.8: Object.hasOwn(obj, "key") — newer alternative
// to obj.hasOwnProperty("key").
const o = { a: 1, b: 2 };
console.assert(Object.hasOwn(o, "a"));
console.assert(Object.hasOwn(o, "b"));
console.assert(!Object.hasOwn(o, "z"));

// Via variable
const key: string = "a";
console.assert(Object.hasOwn(o, key));
