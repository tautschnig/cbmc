/*******************************************************************\

Module: Factory to ensure sharing between default-constructible objects

Author: Michael Tautschnig

\*******************************************************************/

#ifndef CPROVER_UTIL_SINGLETON_FACTORY_H
#define CPROVER_UTIL_SINGLETON_FACTORY_H

/// Return a reference to an object of type T constructed just once. This avoids
/// repeated construction, e.g., of expressions such as \ref true_exprt.
template<typename T>
inline const T &singleton_factory()
{
  static T object;
  return object;
}

#endif // CPROVER_UTIL_SINGLETON_FACTORY_H
