const s: string = "TypeScript";
console.assert(s.length === 10);
console.assert(s.slice(0, 4) === "Type");
console.assert(s.slice(4) === "Script");
console.assert(s.includes("Script") === true);
