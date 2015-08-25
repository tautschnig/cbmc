/*******************************************************************
 Module: Learn environment instrumentation

 Author: Pascal Kesseli, pascal.kesseli@stx.ox.ac.uk

 \*******************************************************************/

#include <string>

void instrument_functions_for_learn(class symbol_tablet &st,
    class goto_functionst &gf, const std::string &ff);

void show_learn_word_length(class symbol_tablet &st, class goto_functionst &gf,
    const std::string &iff, const size_t function_length,
    const size_t call_sites);
