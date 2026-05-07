// Simplified semver compare
class Version {
  major: number;
  minor: number;
  patch: number;
  constructor(M: number, m: number, p: number) {
    this.major = M; this.minor = m; this.patch = p;
  }
  compareTo(other: Version): number {
    if (this.major !== other.major) return this.major < other.major ? -1 : 1;
    if (this.minor !== other.minor) return this.minor < other.minor ? -1 : 1;
    if (this.patch !== other.patch) return this.patch < other.patch ? -1 : 1;
    return 0;
  }
}
const v1 = new Version(1, 2, 3);
const v2 = new Version(1, 2, 4);
const v3 = new Version(2, 0, 0);
console.assert(v1.compareTo(v2) === -1);
console.assert(v2.compareTo(v1) === 1);
console.assert(v1.compareTo(v3) === -1);
console.assert(v1.compareTo(v1) === 0);
