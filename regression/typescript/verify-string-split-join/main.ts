const csv: string = "a,b,c";
const parts: string[] = csv.split(",");
console.assert(parts.length === 3);
const rejoined: string = parts.join("-");
console.assert(rejoined === "a-b-c");
