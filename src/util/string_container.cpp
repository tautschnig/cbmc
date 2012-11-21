/*******************************************************************\

Module: Container for C-Strings

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>
#include <cstring>

#include "string_container.h"

string_containert string_container;

/*******************************************************************\

Function: string_ptrt::string_ptrt

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

string_ptrt::string_ptrt(const char *_s):s(_s), len(strlen(_s))
{
}

/*******************************************************************\

Function: operator==

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool operator==(const string_ptrt a, const string_ptrt b)
{
  if(a.len!=b.len) return false;
  if(a.len==0) return true;
  return memcmp(a.s, b.s, a.len)==0;
}

/*******************************************************************\

Function: string_containert::string_containert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void initialize_string_container();

string_containert::string_containert():n_static_strings(0)
{
  // pre-allocate empty string -- this gets index 0
  get("");

  // allocate strings
  initialize_string_container();
  n_static_strings=string_vector.size();
  assert(n_static_strings>0);
}

/*******************************************************************\

Function: string_containert::~string_containert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

string_containert::~string_containert()
{
  n_static_strings=0;
#ifdef REF_COUNT
  free_list.clear();
#endif
  string_vector.clear();
  string_list.clear();
}

/*******************************************************************\

Function: string_containert::get

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

unsigned string_containert::get(const char *s)
{
  string_ptrt string_ptr(s);

  hash_tablet::iterator it=hash_table.find(string_ptr);
  
  if(it!=hash_table.end())
    return it->second;

  // these are stable
  string_list.push_back(std::string(s));
  string_ptrt result(string_list.back());

#ifdef REF_COUNT
  if(free_list.empty())
  {
    free_list.push_back(string_vector.size());
    string_vector.push_back(std::make_pair(
          string_list.end(),
          0));
  }

  size_t r=free_list.back();
  assert(r<string_vector.size());
  assert(string_vector[r].first==string_list.end());
  assert(string_vector[r].second==0);
  free_list.pop_back();
  hash_table[result]=r;
  
  // these are not
  string_vector[r].first=--string_list.end();
  // reference count is still zero, users must update it
#else
  size_t r=hash_table.size();
  hash_table[result]=r;
  // these are not
  string_vector.push_back(&string_list.back());
#endif

  return r;
}

/*******************************************************************\

Function: string_containert::get

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

unsigned string_containert::get(const std::string &s)
{
  string_ptrt string_ptr(s);

  hash_tablet::iterator it=hash_table.find(string_ptr);
  
  if(it!=hash_table.end())
    return it->second;

  // these are stable
  string_list.push_back(s);
  string_ptrt result(string_list.back());

#ifdef REF_COUNT
  if(free_list.empty())
  {
    free_list.push_back(string_vector.size());
    string_vector.push_back(std::make_pair(
          string_list.end(),
          0));
  }

  size_t r=free_list.back();
  assert(r<string_vector.size());
  assert(string_vector[r].first==string_list.end());
  assert(string_vector[r].second==0);
  free_list.pop_back();
  hash_table[result]=r;
  
  // these are not
  string_vector[r].first=--string_list.end();
  // reference count is still zero, users must update it
#else
  size_t r=hash_table.size();
  hash_table[result]=r;
  // these are not
  string_vector.push_back(&string_list.back());
#endif

  return r;
}

#ifdef REF_COUNT
/*******************************************************************\

Function: string_containert::dec_ref_count

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void string_containert::dec_ref_count(unsigned no)
{
  // no guarantees about order of initialization of objects
  if(n_static_strings==0) return;
  // we never delete statically allocated strings
  if(no<n_static_strings) return;

  assert(no<string_vector.size());
  assert(string_vector[no].first!=string_list.end());
  assert(string_vector[no].second>0);
  --string_vector[no].second;

  if(string_vector[no].second==0)
  {
    string_ptrt string_ptr(*string_vector[no].first);
    hash_tablet::iterator it=hash_table.find(string_ptr);
    hash_table.erase(it);

    string_list.erase(string_vector[no].first);

    string_vector[no].first=string_list.end();

    free_list.push_back(no);
  }
}
#endif

