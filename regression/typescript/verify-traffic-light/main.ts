enum Light { Red = 0, Yellow = 1, Green = 2 }
function next(l: Light): Light {
  if (l === Light.Red) return Light.Green;
  if (l === Light.Green) return Light.Yellow;
  return Light.Red;
}
console.assert(next(Light.Red) === Light.Green);
console.assert(next(Light.Green) === Light.Yellow);
console.assert(next(Light.Yellow) === Light.Red);
