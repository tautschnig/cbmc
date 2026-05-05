const defaults = { timeout: 30, retries: 3 };
const custom = { ...defaults, timeout: 60 };
console.assert(custom.timeout === 60);
console.assert(custom.retries === 3);
