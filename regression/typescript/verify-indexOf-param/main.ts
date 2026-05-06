function findColon(s: string): number {
  return s.indexOf(":");
}
console.assert(findColon("key:value") === 3);
console.assert(findColon("nocolon") === -1);
