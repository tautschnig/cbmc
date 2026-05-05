const s: string = "Hello, World!";
console.assert(s.slice(0, 5) === "Hello");
console.assert(s.includes("World") === true);
console.assert(s.indexOf("World") === 7);
console.assert(s.replace("World", "TS") === "Hello, TS!");
console.assert(s.length === 13);
