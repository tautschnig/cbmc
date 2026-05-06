enum State { Anon = 0, Pending = 1, Auth = 2 }
function transition(s: State, ok: boolean): State {
  if (s === State.Anon) return State.Pending;
  if (s === State.Pending) return ok ? State.Auth : State.Anon;
  return s;
}
console.assert(transition(State.Anon, true) === State.Pending);
console.assert(transition(State.Pending, true) === State.Auth);
console.assert(transition(State.Pending, false) === State.Anon);
console.assert(transition(State.Auth, true) === State.Auth);
