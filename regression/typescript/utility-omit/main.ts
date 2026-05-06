interface User { name: string; age: number; email: string; }
type UserBasic = Omit<User, "email">;
const u: UserBasic = { name: "Bob", age: 25 };
console.assert(u.name === "Bob");
console.assert(u.age === 25);
