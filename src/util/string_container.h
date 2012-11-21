/*******************************************************************\

Module: Container for C-Strings

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef STRING_CONTAINER_H
#define STRING_CONTAINER_H

#include <list>
#include <vector>

#include "hash_cont.h"
#include "string_hash.h"

//#define REF_COUNT

struct string_ptrt
{
  const char *s;
  size_t len;
  
  inline const char *c_str() const
  {
    return s;
  }
  
  explicit string_ptrt(const char *_s);

  explicit string_ptrt(const std::string &_s):s(_s.c_str()), len(_s.size())
  {
  }

  friend bool operator==(const string_ptrt a, const string_ptrt b);
};

bool operator==(const string_ptrt a, const string_ptrt b);

class string_ptr_hash
{
public:
  size_t operator()(const string_ptrt s) const { return hash_string(s.s); }
};

class string_containert
{
public:
  inline unsigned operator[](const char *s)
  {
    return get(s);
  }
  
  inline unsigned operator[](const std::string &s)
  {
    return get(s);
  }
  
  // constructor and destructor
  string_containert();  
  ~string_containert();

  // the pointer is guaranteed to be stable  
  inline const char *c_str(size_t no) const
  {
#ifdef REF_COUNT
    return string_vector[no].first->c_str();
#else
    return string_vector[no]->c_str();
#endif
  }
  
  // the reference is guaranteed to be stable
  inline const std::string &get_string(size_t no) const
  {
#ifdef REF_COUNT
    return *string_vector[no].first;
#else
    return *string_vector[no];
#endif
  }
  
#ifdef REF_COUNT
  void inc_ref_count(unsigned no)
  {
    // no guarantees about order of initialization of objects
    if(n_static_strings==0) return;

    ++string_vector[no].second;
  }

  void dec_ref_count(unsigned no);
#else
  void inc_ref_count(unsigned no)
  {
  }

  void dec_ref_count(unsigned no)
  {
  }
#endif

protected:
  // the 'unsigned' ought to be size_t
  unsigned n_static_strings;

  typedef hash_map_cont<string_ptrt, unsigned, string_ptr_hash> hash_tablet;
  hash_tablet hash_table;
  
  unsigned get(const char *s);
  unsigned get(const std::string &s);
  
  typedef std::list<std::string> string_listt;
  string_listt string_list;
  
#ifdef REF_COUNT
  typedef std::vector<std::pair<string_listt::iterator, unsigned> > string_vectort;
  string_vectort string_vector;

  std::list<string_vectort::size_type> free_list;
#else
  typedef std::vector<std::string *> string_vectort;
  string_vectort string_vector;
#endif
};

// an ugly global object
extern string_containert string_container;

#endif
