/// \file
/// Weak Equivalence Graph for array theory.
///
/// Implements the forest-based data structure from Christ and Hoenicke,
/// "Weakly Equivalent Arrays" (arXiv:1405.6939), Section 7.
///
/// Each node has:
/// - A primary edge (p) pointing toward the WEG representative, labeled
///   with the store index (pi). Absent for the representative.
/// - A secondary edge (s) for weak equivalence modulo i queries.
///   When following primary edges and pi equals i, follow s instead.

#ifndef CPROVER_SOLVERS_FLATTENING_ARRAYS_WEG_H
#define CPROVER_SOLVERS_FLATTENING_ARRAYS_WEG_H

#include <util/numbering.h>
#include <util/std_expr.h>

#include <optional>
#include <set>
#include <vector>

struct weg_nodet
{
  /// Primary edge toward representative. nullopt for the representative.
  std::optional<std::size_t> p;
  /// Store index of the primary edge. nil for equality edges.
  exprt pi = nil_exprt{};
  /// Secondary edge for modulo-i queries.
  std::optional<std::size_t> s;
};

class weak_equivalence_grapht
{
public:
  std::size_t number(const exprt &array)
  {
    const std::size_t idx = numbering.number(array);
    if(idx >= nodes.size())
      nodes.resize(idx + 1);
    return idx;
  }

  std::size_t size() const { return nodes.size(); }
  const exprt &operator[](std::size_t idx) const { return numbering[idx]; }

  /// Find representative of weak equivalence class.
  /// Paper: get-rep(n)
  std::size_t get_rep(std::size_t n) const
  {
    while(nodes[n].p.has_value())
      n = *nodes[n].p;
    return n;
  }

  /// Find representative of weak equivalence modulo i class.
  /// Paper: get-repi(n, i)
  /// Follows primary edges, but when pi == i, follows secondary instead.
  std::size_t get_rep_mod(std::size_t n, const exprt &i) const
  {
    while(true)
    {
      const auto &node = nodes[n];
      if(!node.p.has_value())
        return n; // n is representative
      if(node.pi.is_not_nil() && node.pi == i)
      {
        // Primary edge stores at index i — follow secondary
        if(!node.s.has_value())
          return n; // no secondary — n is mod-i representative
        n = *node.s;
      }
      else
      {
        // Primary edge doesn't store at i (or is equality edge) — follow it
        n = *node.p;
      }
    }
  }

  bool weakly_equivalent(std::size_t a, std::size_t b) const
  {
    return get_rep(a) == get_rep(b);
  }

  bool weakly_equivalent_mod(
    std::size_t a,
    std::size_t b,
    const exprt &i) const
  {
    return get_rep_mod(a, i) == get_rep_mod(b, i);
  }

  /// Add store edge: b = store(a, i, v).
  /// Paper: add-store(a, b, i)
  void add_store(std::size_t a, std::size_t b, const exprt &i)
  {
    make_rep(b);
    if(get_rep(a) == b)
    {
      // Already in same class — add secondary edges
      add_secondary_edges(a, b, i);
    }
    else
    {
      // Different classes — add primary edge
      nodes[b].p = a;
      nodes[b].pi = i;
    }
  }

  /// Add equality edge: a = b (no store index).
  void add_equality(std::size_t a, std::size_t b)
  {
    if(get_rep(a) == get_rep(b))
      return;
    make_rep(b);
    nodes[b].p = a;
    nodes[b].pi = nil_exprt{};
  }

  /// Collect store indices on the path from a to b via the representative.
  std::vector<exprt> path_store_indices(std::size_t a, std::size_t b) const
  {
    if(!weakly_equivalent(a, b))
      return {};
    std::vector<exprt> indices;
    // Collect from a to rep
    for(std::size_t n = a; nodes[n].p.has_value(); n = *nodes[n].p)
    {
      if(nodes[n].pi.is_not_nil())
        indices.push_back(nodes[n].pi);
    }
    // Collect from b to rep
    for(std::size_t n = b; nodes[n].p.has_value(); n = *nodes[n].p)
    {
      if(nodes[n].pi.is_not_nil())
        indices.push_back(nodes[n].pi);
    }
    return indices;
  }

private:
  numberingt<exprt, irep_hash> numbering;
  std::vector<weg_nodet> nodes;

  /// Paper: make-rep(n) — make n the representative by inverting primary edges.
  void make_rep(std::size_t n)
  {
    if(!nodes[n].p.has_value())
      return;

    // Collect path to representative (iterative to avoid stack overflow)
    std::vector<std::size_t> path;
    std::set<std::size_t> visited;
    for(std::size_t cur = n; nodes[cur].p.has_value(); cur = *nodes[cur].p)
    {
      if(!visited.insert(cur).second)
        return; // cycle detected — bail out
      path.push_back(cur);
    }

    // Invert edges along the path.
    // path = [n, p1, p2, ..., pk] where pk.p = rep
    // After inversion: rep.p = pk, pk.p = pk-1, ..., p1.p = n, n.p = nullopt
    for(std::size_t k = path.size(); k > 0; --k)
    {
      std::size_t cur = path[k - 1];
      std::size_t parent = *nodes[cur].p;
      nodes[parent].p = cur;
      nodes[parent].pi = nodes[cur].pi;
      nodes[parent].s.reset();
    }
    nodes[n].p.reset();
    nodes[n].pi = nil_exprt{};
    nodes[n].s.reset();
  }

  /// Add secondary edges when a store creates a cycle in the WEG.
  /// Paper: add-secondary(S, a, b) where b is already the representative.
  void add_secondary_edges(
    std::size_t a,
    std::size_t b,
    const exprt &store_index)
  {
    // Walk from a toward b (the representative).
    // For each node whose primary index is NOT in the forbidden set
    // and whose mod-i representative is not b, add a secondary edge.
    std::set<irep_idt> forbidden;
    // The new store index is forbidden
    if(store_index.is_not_nil() && store_index.id() == ID_symbol)
      forbidden.insert(to_symbol_expr(store_index).get_identifier());

    std::size_t cur = a;
    while(cur != b && nodes[cur].p.has_value())
    {
      const auto &pi = nodes[cur].pi;
      if(pi.is_not_nil() && pi.id() == ID_symbol)
      {
        const auto &pi_id = to_symbol_expr(pi).get_identifier();
        if(forbidden.find(pi_id) == forbidden.end())
        {
          // Check if cur's mod-pi representative is already b
          if(get_rep_mod(cur, pi) != b)
          {
            // Add secondary edge from cur to b
            // First need to make cur the mod-pi representative
            // (simplified: just set secondary)
            nodes[cur].s = b;
          }
        }
        forbidden.insert(pi_id);
      }
      cur = *nodes[cur].p;
    }
  }
};

#endif // CPROVER_SOLVERS_FLATTENING_ARRAYS_WEG_H
