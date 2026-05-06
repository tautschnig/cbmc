// Simplified config-parser pattern (common in CLI tools)
interface Config { host: string; port: number; debug: boolean; }

function mergeConfig(defaults: Config, overrides: Config): Config {
  return overrides; // simplified merge
}

function validateConfig(c: Config): boolean {
  return c.port > 0 && c.port < 65536 && c.host.length > 0;
}

const defaults: Config = { host: "localhost", port: 8080, debug: false };
const user: Config = { host: "server.com", port: 443, debug: true };
const merged: Config = mergeConfig(defaults, user);
console.assert(validateConfig(merged) === true);
console.assert(merged.host === "server.com");
console.assert(merged.port === 443);

const bad: Config = { host: "", port: 0, debug: false };
console.assert(validateConfig(bad) === false);
