//use -D IREP_HASH_STATS

#include <iostream>

#ifdef IREP_HASH_STATS
extern unsigned long long irep_hash_cnt;
extern unsigned long long irep_cmp_cnt;
extern unsigned long long irep_cmp_ne_cnt;

extern unsigned long long merge_hash_cnt;
extern unsigned long long merge_cmp_cnt;
extern unsigned long long merge_cmp_ne_cnt;
#endif

#include <util/merge_irep.h>
#include <util/std_expr.h>

int main()
{
  {
    symbol_exprt a("a");
    or_exprt o(a, a);
    merge_irept m;
    m(o);
  }

  #ifdef IREP_HASH_STATS
  // r5301: 22, 4, 2
  std::cout << "merge_irept:" << std::endl;
  std::cout << "IREP_HASH_CNT=" << irep_hash_cnt << std::endl;
  std::cout << "IREP_CMP_CNT=" << irep_cmp_cnt << std::endl;
  std::cout << "IREP_CMP_NE_CNT=" << irep_cmp_ne_cnt << std::endl;
  irep_hash_cnt=0;
  irep_cmp_cnt=0;
  irep_cmp_ne_cnt=0;
  #endif

  {
    symbol_exprt a("a");
    or_exprt o(a, a);
    merged_irepst m;
    m(o);
  }

  #ifdef IREP_HASH_STATS
  std::cout << std::endl << "merged_irepst:" << std::endl;
  std::cout << "IREP_HASH_CNT=" << irep_hash_cnt << std::endl;
  std::cout << "IREP_CMP_CNT=" << irep_cmp_cnt << std::endl;
  std::cout << "IREP_CMP_NE_CNT=" << irep_cmp_ne_cnt << std::endl;
  std::cout << "MERGE_HASH_CNT=" << merge_hash_cnt << std::endl;
  std::cout << "MERGE_CMP_CNT=" << merge_cmp_cnt << std::endl;
  std::cout << "MERGE_CMP_NE_CNT=" << merge_cmp_ne_cnt << std::endl;
  #endif
}

