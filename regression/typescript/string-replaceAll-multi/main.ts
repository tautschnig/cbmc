const s: string = "a.b.c.d";
const r: string = s.replaceAll(".", "-");
console.assert(r === "a-b-c-d");
