/*******************************************************************\

Module: Hashing IREPs

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Hashing IREPs

#include "irep_hash_container.h"

#include "irep.h"
#include "irep_hash.h"

size_t irep_hash_container_baset::number(const irept &irep)
{
  // the ptr-hash provides a speedup of up to 3x

  auto entry = ptr_hash.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(&irep.read()),
    std::forward_as_tuple(0, irep));

  if(!entry.second && entry.first->second.final_number)
    return entry.first->second.number;

  const size_t id = numbering.number(pack(irep));

  entry.first->second.number = id;
  entry.first->second.final_number = true;

  return id;
}

size_t irep_hash_container_baset::vector_hasht::operator()(
  const packedt &p) const
{
  size_t result=p.size(); // seed
  for(auto elem : p)
    result=hash_combine(result, elem);
  return result;
}

irep_hash_container_baset::packedt irep_hash_container_baset::pack(const irept &irep)
{
  const irept::subt &sub=irep.get_sub();
  const irept::named_subt &named_sub=irep.get_named_sub();

  if(full)
  {
    // we pack: the irep id, the sub size, the subs, the named-sub size, and
    // each of the named subs with their ids
#ifdef NAMED_SUB_IS_FORWARD_LIST
    const std::size_t named_sub_size =
      std::distance(named_sub.begin(), named_sub.end());
#else
    const std::size_t named_sub_size = named_sub.size();
#endif
    packedt packed(1 + 1 + sub.size() + 1 + named_sub_size * 2);

    packed.push_back(irep_id_hash()(irep.id()));

    packed.push_back(sub.size());
    forall_irep(it, sub)
      packed.push_back(number(*it));

    packed.push_back(named_sub_size);
    for(const auto &sub_irep : named_sub)
    {
      packed.push_back(irep_id_hash()(sub_irep.first)); // id
      packed.push_back(number(sub_irep.second));        // sub-irep
    }

    return packed;
  }
  else
  {
    const std::size_t non_comment_count =
      irept::number_of_non_comments(named_sub);

    // we pack: the irep id, the sub size, the subs, the named-sub size, and
    // each of the non-comment named subs with their ids
    packedt packed(1 + 1 + sub.size() + 1 + non_comment_count * 2);

    packed.push_back(irep_id_hash()(irep.id()));

    packed.push_back(sub.size());
    forall_irep(it, sub)
      packed.push_back(number(*it));

    packed.push_back(non_comment_count);
    for(const auto &sub_irep : named_sub)
      if(!irept::is_comment(sub_irep.first))
      {
        packed.push_back(irep_id_hash()(sub_irep.first)); // id
        packed.push_back(number(sub_irep.second));        // sub-irep
      }

    return packed;
  }
}
