interface User { name: string; age: number; email: string; }
type UserSummary = Pick<User, "name" | "age">;
const s: UserSummary = { name: "Alice", age: 30 };
console.assert(s.age === 30);
console.assert(s.name === "Alice");
