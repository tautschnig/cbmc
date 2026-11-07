/// Author: Diffblue Ltd.

/// \file
/// Symbol table

#ifndef CPROVER_UTIL_SYMBOL_TABLE_H
#define CPROVER_UTIL_SYMBOL_TABLE_H

#include "symbol_table_base.h"

#include <string>
#include <unordered_map>

/// \brief The symbol table
/// \ingroup gr_symbol_table
class symbol_tablet : public symbol_table_baset
{
private:
  /// Value referenced by \ref symbol_table_baset::symbols.
  symbolst internal_symbols;
  /// Value referenced by \ref symbol_table_baset::symbol_base_map.
  symbol_base_mapt internal_symbol_base_map;
  /// Value referenced by \ref symbol_table_baset::symbol_module_map.
  symbol_module_mapt internal_symbol_module_map;
  /// For each prefix of next_unused_suffix, a suffix below which all have
  /// been found in use, so that generating many names with one prefix does
  /// not test the existence of all earlier ones every time.
  mutable std::unordered_map<std::string, std::size_t>
    next_free_suffix_for_prefix;

public:
  symbol_tablet()
    : symbol_table_baset(
        internal_symbols,
        internal_symbol_base_map,
        internal_symbol_module_map)
  {
  }

  /// Copy constructor.
  symbol_tablet(const symbol_tablet &other)
    : symbol_table_baset(
        internal_symbols,
        internal_symbol_base_map,
        internal_symbol_module_map),
      internal_symbols(other.internal_symbols),
      internal_symbol_base_map(other.internal_symbol_base_map),
      internal_symbol_module_map(other.internal_symbol_module_map),
      next_free_suffix_for_prefix(other.next_free_suffix_for_prefix)
  {
  }

  /// Copy assignment operator.
  symbol_tablet &operator=(const symbol_tablet &other)
  {
    // Copy to temp and then call move assignment
    return *this=symbol_tablet(other);
  }

  /// Move constructor.
  symbol_tablet(symbol_tablet &&other)
    : symbol_table_baset(
        internal_symbols,
        internal_symbol_base_map,
        internal_symbol_module_map),
      internal_symbols(std::move(other.internal_symbols)),
      internal_symbol_base_map(std::move(other.internal_symbol_base_map)),
      internal_symbol_module_map(std::move(other.internal_symbol_module_map)),
      next_free_suffix_for_prefix(std::move(other.next_free_suffix_for_prefix))
  {
  }

  /// Move assignment operator.
  symbol_tablet &operator=(symbol_tablet &&other)
  {
    internal_symbols = std::move(other.internal_symbols);
    internal_symbol_base_map = std::move(other.internal_symbol_base_map);
    internal_symbol_module_map = std::move(other.internal_symbol_module_map);
    next_free_suffix_for_prefix = std::move(other.next_free_suffix_for_prefix);
    return *this;
  }

  /// Swap symbol maps between two symbol tables.
  /// \param other: The second symbol table to swap values with.
  void swap(symbol_tablet &other)
  {
    internal_symbols.swap(other.internal_symbols);
    internal_symbol_base_map.swap(other.internal_symbol_base_map);
    internal_symbol_module_map.swap(other.internal_symbol_module_map);
    next_free_suffix_for_prefix.swap(other.next_free_suffix_for_prefix);
  }

public:
  virtual const symbol_tablet &get_symbol_table() const override
  {
    return *this;
  }

  /// Find a symbol in the symbol table for read-write access.
  /// \param name: The name of the symbol to look for
  /// \return A pointer to the found symbol if it exists, nullptr otherwise.
  virtual symbolt *get_writeable(const irep_idt &name) override
  {
    symbolst::iterator it = internal_symbols.find(name);
    return it != internal_symbols.end() ? &it->second : nullptr;
  }

  virtual std::pair<symbolt &, bool> insert(symbolt symbol) override;
  virtual bool move(symbolt &symbol, symbolt *&new_symbol) override;

  virtual void erase(const symbolst::const_iterator &entry) override;
  /// Wipe internal state of the symbol table.
  virtual void clear() override
  {
    internal_symbols.clear();
    internal_symbol_base_map.clear();
    internal_symbol_module_map.clear();
    next_free_suffix_for_prefix.clear();
  }

  /// The smallest suffix of \p prefix not in use that is at least the one
  /// this function returned last for \p prefix plus one: suffixes freed by
  /// erasing symbols are not reused.
  std::size_t next_unused_suffix(const std::string &prefix) const override
  {
    std::size_t &next = next_free_suffix_for_prefix[prefix];
    next = symbol_table_baset::next_unused_suffix(prefix, next);
    return next++;
  }

  virtual iteratort begin() override
  {
    return iteratort(internal_symbols.begin());
  }
  virtual iteratort end() override
  {
    return iteratort(internal_symbols.end());
  }

  using symbol_table_baset::begin;
  using symbol_table_baset::end;

  /// Check that the symbol table is well-formed
  void validate(
    const validation_modet vm = validation_modet::INVARIANT) const override;

  bool operator==(const symbol_tablet &other) const;
};

#endif // CPROVER_UTIL_SYMBOL_TABLE_H
