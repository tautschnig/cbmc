enum GameState { Playing = 0, Won = 1, Lost = 2 }
class Game {
  state: GameState;
  score: number;
  health: number;
  constructor() { this.state = GameState.Playing; this.score = 0; this.health = 100; }
  hit(damage: number): void {
    this.health = this.health - damage;
    if (this.health <= 0) this.state = GameState.Lost;
  }
  collect(points: number): void {
    this.score = this.score + points;
    if (this.score >= 100) this.state = GameState.Won;
  }
}
const g = new Game();
g.collect(50);
console.assert(g.state === GameState.Playing);
g.collect(60);
console.assert(g.state === GameState.Won);

const g2 = new Game();
g2.hit(50);
console.assert(g2.state === GameState.Playing);
g2.hit(60);
console.assert(g2.state === GameState.Lost);
