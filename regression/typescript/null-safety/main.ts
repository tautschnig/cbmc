// TypeScript Handbook: Narrowing
function greet(name: string | null): string {
  if (name === null) {
    return "Hello, stranger!";
  }
  return "Hello, " + name + "!";
}
console.assert(greet("Alice") === "Hello, Alice!");
console.assert(greet(null) === "Hello, stranger!");
