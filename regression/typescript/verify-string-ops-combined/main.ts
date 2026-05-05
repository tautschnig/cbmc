const s: string = "Hello, World!";
console.assert(s.length === 13);
console.assert(s.indexOf("World") === 7);
console.assert(s.slice(0, 5) === "Hello");
console.assert(s.replace("World", "TypeScript") === "Hello, TypeScript!");
