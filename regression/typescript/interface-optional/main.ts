interface Config {
  width: number;
  height?: number;
}
const cfg: Config = { width: 100 };
console.assert(cfg.width === 100);
