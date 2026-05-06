type Greeting = `hello ${string}`;
const g: Greeting = "hello world" as Greeting;
console.assert(g === "hello world");
console.assert(g.length === 11);
