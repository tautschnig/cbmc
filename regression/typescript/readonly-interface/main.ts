interface Config {
  readonly width: number;
  readonly height: number;
}
const cfg: Config = { width: 800, height: 600 };
console.assert(cfg.width === 800);
console.assert(cfg.height === 600);
