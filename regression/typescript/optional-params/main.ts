// ES2024 sec-function-definitions: default parameters
function greet(name: string, greeting: string = "Hello"): string {
  return greeting + " " + name;
}
console.assert(greet("Alice") === "Hello Alice");
console.assert(greet("Bob", "Hi") === "Hi Bob");
