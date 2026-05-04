// TSH: Object Types.md — readonly
interface Config {
  readonly host: string;
  readonly port: number;
}
const cfg: Config = { host: "localhost", port: 8080 };
console.assert(cfg.host === "localhost");
console.assert(cfg.port === 8080);
