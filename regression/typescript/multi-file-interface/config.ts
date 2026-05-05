export interface Config { host: string; port: number; }
export function getPort(c: Config): number { return c.port; }
