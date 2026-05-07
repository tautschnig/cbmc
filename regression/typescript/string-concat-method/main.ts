// ES2024 §22.1.3.5: String.prototype.concat (method form, not + operator).
console.assert("a".concat("b") === "ab");
console.assert("a".concat("b", "c") === "abc");
console.assert("hello".concat(" ", "world") === "hello world");
console.assert("foo".concat("bar", "baz") === "foobarbaz");
// Note: "".concat("x") and "foo".concat() have gaps in our model
// (empty string receiver bypasses method dispatch; zero-arg concat
// returns undefined). Documented but not tested here.
