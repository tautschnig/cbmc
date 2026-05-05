import { Config, getPort } from "./config";
const cfg: Config = { host: "localhost", port: 3000 };
console.assert(getPort(cfg) === 3000);
