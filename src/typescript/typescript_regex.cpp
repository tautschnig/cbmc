/// \file
/// Implementation of the TypeScript RegExp Phase 2 engine.
///
/// Architecture:
///   1. Parser: regex source string → AST (regex_node)
///   2. Compiler: AST → NFA (states + transitions)
///   3. Simulator: NFA × input → bool (subset-construction-style
///      step-by-step state-set evolution)
///
/// The NFA uses Thompson's construction (one start state per
/// sub-expression, one accept state, epsilon transitions for
/// quantifier sutures). State storage is a flat std::vector with
/// stable indices; no dynamic memory per step.

#include "typescript_regex.h"

#include <cassert>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace
{

// --------------------------------------------------------------------------
// CharClass: a set of accepting characters, optionally negated.
// Represented as a 256-bit bitset (one bit per byte value).
// --------------------------------------------------------------------------
struct char_classt
{
  bool bits[256] = {};
  bool negated = false;

  bool matches(char c) const
  {
    bool b = bits[static_cast<unsigned char>(c)];
    return negated ? !b : b;
  }

  void add_char(char c)
  {
    bits[static_cast<unsigned char>(c)] = true;
  }
  void add_range(char lo, char hi)
  {
    for(int c = static_cast<unsigned char>(lo);
        c <= static_cast<unsigned char>(hi);
        ++c)
      bits[c] = true;
  }
  void add_digit()
  {
    for(int c = '0'; c <= '9'; ++c)
      bits[c] = true;
  }
  void add_word()
  {
    add_digit();
    for(int c = 'a'; c <= 'z'; ++c)
      bits[c] = true;
    for(int c = 'A'; c <= 'Z'; ++c)
      bits[c] = true;
    bits['_'] = true;
  }
  void add_space()
  {
    bits[' '] = true;
    bits['\t'] = true;
    bits['\n'] = true;
    bits['\r'] = true;
    bits['\f'] = true;
    bits['\v'] = true;
  }
  void add_any()
  {
    for(int c = 0; c < 256; ++c)
      if(c != '\n')
        bits[c] = true;
  }
};

// --------------------------------------------------------------------------
// NFA state and transition encoding.
// --------------------------------------------------------------------------
//
// Every state has at most two outgoing transitions:
//   - a class-consuming transition (advances input by one char)
//   - up to two epsilon transitions (do not consume input)
// The accept state is a sentinel index (kAccept).
//
// State kind:
//   - CONSUME: matches one char from `cls` and goes to `out1`
//   - EPSILON: branches to `out1` and (if != -1) `out2`
//   - ANCHOR_BEGIN: matches only at input position 0; epsilons to out1
//   - ANCHOR_END:   matches only at input position == input.size();
//                   epsilons to out1
struct nfa_statet
{
  enum class kind_t
  {
    CONSUME,
    EPSILON,
    ANCHOR_BEGIN,
    ANCHOR_END
  };

  kind_t kind = kind_t::EPSILON;
  char_classt cls; // valid for CONSUME
  int out1 = -1;
  int out2 = -1; // valid for EPSILON; -1 = unused
};

constexpr int K_ACCEPT = -2;

// --------------------------------------------------------------------------
// Parser
// --------------------------------------------------------------------------
//
// Grammar (minus alternation/grouping which are deferred):
//   regex   ::= concat
//   concat  ::= atom_with_quantifier*
//   atom_with_quantifier ::= atom (quant)?
//   quant   ::= '*' | '+' | '?'
//   atom    ::= '.' | '^' | '$' | charclass | escape | literal
//   charclass ::= '[' '^'? class_item* ']'
//   class_item ::= literal '-' literal | shorthand | literal
//   escape  ::= '\\' (metachar | shorthand)
//   shorthand ::= '\\d' | '\\D' | '\\w' | '\\W' | '\\s' | '\\S'
//
// Atoms produce a small NFA fragment with a single entry and a
// single dangling exit (out1 = -1). Quantifiers wire fragments
// together via epsilon transitions.

struct parsert
{
  const std::string &src;
  size_t pos = 0;
  std::vector<nfa_statet> states;
  bool error = false;

  explicit parsert(const std::string &s) : src{s}
  {
  }

  int new_state()
  {
    states.emplace_back();
    return static_cast<int>(states.size()) - 1;
  }

  int new_consume(char_classt cls)
  {
    int s = new_state();
    states[s].kind = nfa_statet::kind_t::CONSUME;
    states[s].cls = cls;
    return s;
  }

  // Connect a dangling fragment exit to a target index.
  void patch(int frag_exit, int target)
  {
    if(frag_exit < 0)
      return; // nothing to patch (already accept)
    nfa_statet &st = states[frag_exit];
    if(st.kind == nfa_statet::kind_t::CONSUME)
    {
      st.out1 = target;
    }
    else
    {
      // EPSILON / ANCHOR_*: out1 must be set; if out2 is dangling, set
      // it instead. Caller invariants ensure each fragment has a
      // single dangling exit, but we tolerate both.
      if(st.out1 == -1)
        st.out1 = target;
      else if(st.out2 == -1)
        st.out2 = target;
    }
  }

  // Frag = (entry state, dangling exit state). exit may equal entry
  // for unit fragments produced by atoms.
  struct fragt
  {
    int entry;
    int exit;
  };

  // Parse the whole pattern.
  fragt parse()
  {
    fragt f = parse_concat();
    if(pos != src.size())
      error = true;
    return f;
  }

  fragt parse_concat()
  {
    // Empty pattern matches everything trivially.
    if(pos == src.size())
    {
      // Single epsilon state.
      int s = new_state();
      states[s].kind = nfa_statet::kind_t::EPSILON;
      return fragt{s, s};
    }

    fragt acc = parse_atom_q();
    while(pos < src.size())
    {
      fragt next = parse_atom_q();
      if(error)
        return acc;
      // Suture: patch acc's dangling exit to next's entry.
      patch(acc.exit, next.entry);
      acc.exit = next.exit;
    }
    return acc;
  }

  fragt parse_atom_q()
  {
    fragt atom = parse_atom();
    if(error)
      return atom;
    if(pos < src.size())
    {
      char c = src[pos];
      if(c == '*' || c == '+' || c == '?')
      {
        ++pos;
        return apply_quant(atom, c);
      }
    }
    return atom;
  }

  // Apply * + ? to an existing fragment via Thompson-style epsilon
  // sutures. New states added; returns updated fragment.
  fragt apply_quant(fragt inner, char op)
  {
    int split = new_state();
    states[split].kind = nfa_statet::kind_t::EPSILON;
    int join = new_state();
    states[join].kind = nfa_statet::kind_t::EPSILON;

    if(op == '*')
    {
      // split -> inner.entry (consume) | join (skip)
      states[split].out1 = inner.entry;
      states[split].out2 = join;
      patch(inner.exit, split); // inner exits back to split (loop)
      return fragt{split, join};
    }
    if(op == '+')
    {
      // entry -> inner.entry; inner.exit -> split -> inner.entry | join
      states[split].out1 = inner.entry;
      states[split].out2 = join;
      patch(inner.exit, split);
      return fragt{inner.entry, join};
    }
    // op == '?'
    // split -> inner.entry | join
    states[split].out1 = inner.entry;
    states[split].out2 = join;
    patch(inner.exit, join);
    return fragt{split, join};
  }

  fragt parse_atom()
  {
    if(pos >= src.size())
    {
      error = true;
      return fragt{0, 0};
    }
    char c = src[pos];
    if(c == '.')
    {
      ++pos;
      char_classt cls;
      cls.add_any();
      int s = new_consume(cls);
      return fragt{s, s};
    }
    if(c == '^')
    {
      ++pos;
      int s = new_state();
      states[s].kind = nfa_statet::kind_t::ANCHOR_BEGIN;
      return fragt{s, s};
    }
    if(c == '$')
    {
      ++pos;
      int s = new_state();
      states[s].kind = nfa_statet::kind_t::ANCHOR_END;
      return fragt{s, s};
    }
    if(c == '[')
    {
      ++pos;
      return parse_class();
    }
    if(c == '\\')
    {
      ++pos;
      if(pos >= src.size())
      {
        error = true;
        return fragt{0, 0};
      }
      char e = src[pos++];
      char_classt cls;
      switch(e)
      {
      case 'd':
        cls.add_digit();
        break;
      case 'D':
        cls.add_digit();
        cls.negated = true;
        break;
      case 'w':
        cls.add_word();
        break;
      case 'W':
        cls.add_word();
        cls.negated = true;
        break;
      case 's':
        cls.add_space();
        break;
      case 'S':
        cls.add_space();
        cls.negated = true;
        break;
      case 'n':
        cls.add_char('\n');
        break;
      case 't':
        cls.add_char('\t');
        break;
      case 'r':
        cls.add_char('\r');
        break;
      // Literal escapes for metacharacters:
      default:
        cls.add_char(e);
        break;
      }
      int s = new_consume(cls);
      return fragt{s, s};
    }
    // Unsupported metacharacters fall through to error so the caller
    // returns std::nullopt and the dispatch falls back to nondet.
    if(c == '|' || c == '(' || c == ')' || c == '{')
    {
      error = true;
      return fragt{0, 0};
    }
    // Plain literal.
    ++pos;
    char_classt cls;
    cls.add_char(c);
    int s = new_consume(cls);
    return fragt{s, s};
  }

  fragt parse_class()
  {
    // We are positioned just past the '['.
    char_classt cls;
    if(pos < src.size() && src[pos] == '^')
    {
      cls.negated = true;
      ++pos;
    }
    while(pos < src.size() && src[pos] != ']')
    {
      char c = src[pos];
      if(c == '\\')
      {
        ++pos;
        if(pos >= src.size())
        {
          error = true;
          break;
        }
        char e = src[pos++];
        switch(e)
        {
        case 'd':
          cls.add_digit();
          break;
        case 'w':
          cls.add_word();
          break;
        case 's':
          cls.add_space();
          break;
        case 'n':
          cls.add_char('\n');
          break;
        case 't':
          cls.add_char('\t');
          break;
        case 'r':
          cls.add_char('\r');
          break;
        default:
          cls.add_char(e);
          break;
        }
        continue;
      }
      // Range like a-z?
      if(pos + 2 < src.size() && src[pos + 1] == '-' && src[pos + 2] != ']')
      {
        cls.add_range(src[pos], src[pos + 2]);
        pos += 3;
        continue;
      }
      cls.add_char(c);
      ++pos;
    }
    if(pos >= src.size() || src[pos] != ']')
    {
      error = true;
      return fragt{0, 0};
    }
    ++pos; // consume ']'
    int s = new_consume(cls);
    return fragt{s, s};
  }
};

// --------------------------------------------------------------------------
// Simulator: tracks the set of reachable NFA states after each input
// position. epsilon-closure expands a state set across EPSILON and
// ANCHOR_* transitions (anchors gated on input position).
// --------------------------------------------------------------------------
//
// Given the bounded NFA size, we use std::set<int> for state sets;
// performance is fine for any pattern the frontend will produce.

void epsilon_close(
  const std::vector<nfa_statet> &states,
  std::set<int> &set,
  size_t input_pos,
  size_t input_len)
{
  std::vector<int> stack(set.begin(), set.end());
  while(!stack.empty())
  {
    int s = stack.back();
    stack.pop_back();
    if(s == K_ACCEPT)
      continue;
    const nfa_statet &st = states[s];
    auto add = [&](int t)
    {
      if(t == -1)
        return;
      if(set.insert(t).second)
        stack.push_back(t);
    };
    if(st.kind == nfa_statet::kind_t::EPSILON)
    {
      add(st.out1);
      add(st.out2);
    }
    else if(st.kind == nfa_statet::kind_t::ANCHOR_BEGIN)
    {
      if(input_pos == 0)
        add(st.out1);
    }
    else if(st.kind == nfa_statet::kind_t::ANCHOR_END)
    {
      if(input_pos == input_len)
        add(st.out1);
    }
    // CONSUME states do not have epsilons.
  }
}

bool simulate(
  const std::vector<nfa_statet> &states,
  int entry,
  int accept_idx,
  const std::string &input,
  bool anchored_start)
{
  // RegExp.prototype.test semantics: match anywhere in input unless
  // pattern starts with '^' (handled implicitly by ANCHOR_BEGIN
  // being unreachable from input_pos > 0). To get "match anywhere"
  // we add an implicit `.*?` prefix by trying every starting offset.
  size_t lo = 0;
  size_t hi = anchored_start ? 1 : input.size() + 1;
  for(size_t start = lo; start < hi; ++start)
  {
    std::set<int> cur{entry};
    epsilon_close(states, cur, start, input.size());
    if(cur.count(accept_idx))
      return true;
    for(size_t i = start; i < input.size(); ++i)
    {
      std::set<int> next;
      for(int s : cur)
      {
        if(s == K_ACCEPT)
          continue;
        const nfa_statet &st = states[s];
        if(st.kind == nfa_statet::kind_t::CONSUME && st.cls.matches(input[i]))
        {
          if(st.out1 == accept_idx)
            return true;
          if(st.out1 != -1)
            next.insert(st.out1);
        }
      }
      epsilon_close(states, next, i + 1, input.size());
      if(next.count(accept_idx))
        return true;
      if(next.empty())
        break;
      cur = std::move(next);
    }
  }
  return false;
}

} // namespace

namespace typescript_regex
{

bool is_supported(const std::string &pattern)
{
  parsert p(pattern);
  // Quick scan for unsupported metacharacters.
  for(size_t i = 0; i < pattern.size(); ++i)
  {
    char c = pattern[i];
    if(c == '|' || c == '(' || c == ')' || c == '{')
      return false;
    if(c == '\\' && i + 1 < pattern.size())
    {
      // Skip the escaped char so we don't re-scan it.
      ++i;
    }
  }
  // Try a parse to catch syntax errors not caught by the scan.
  parsert p2(pattern);
  p2.parse();
  return !p2.error;
}

std::optional<bool> match(const std::string &pattern, const std::string &input)
{
  if(!is_supported(pattern))
    return std::nullopt;

  parsert p(pattern);
  parsert::fragt frag = p.parse();
  if(p.error)
    return std::nullopt;
  // Patch the fragment's dangling exit to a sentinel accept index.
  // K_ACCEPT is encoded as a state-id distinct from any real index;
  // simulate() recognises it.
  // Add a real "accept" epsilon state so the simulator can detect
  // acceptance via index lookup.
  int accept_idx = static_cast<int>(p.states.size());
  p.states.emplace_back();
  p.states[accept_idx].kind = nfa_statet::kind_t::EPSILON;
  p.patch(frag.exit, accept_idx);

  // If the original pattern starts with '^', the simulator only
  // tries start offset 0; otherwise it tries every offset.
  bool anchored_start = !pattern.empty() && pattern[0] == '^';
  return simulate(p.states, frag.entry, accept_idx, input, anchored_start);
}

} // namespace typescript_regex
