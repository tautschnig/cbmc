interface Config { host: string; port: number; }
type FrozenConfig = Readonly<Config>;
const c: FrozenConfig = { host: "localhost", port: 8080 };
console.assert(c.port === 8080);
