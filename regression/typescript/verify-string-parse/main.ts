const csv: string = "a,b,c,d";
const parts: string[] = csv.split(",");
console.assert(parts.length === 4);
console.assert(parts[0] === "a");
console.assert(parts[3] === "d");
