#ifndef GRAPH_TRANSITIVE_H
#define GRAPH_TRANSITIVE_H

#include "goto_functions.h"
#include "goto_program.h"

#include <util/graph.h>

#include <iostream>

class graph_transitivet
{
  public:
    graph_transitivet(
      namespacet &_ns,
      goto_functionst &_goto_functions
    ): g(), ns(_ns), goto_functions(_goto_functions),
       fun2graph()
    {}

    void operator()();

    void output_dot(std::ostream &out);
    void output_json(std::ostream &out);

  private:
    typedef goto_programt::instructiont instructiont;
    typedef goto_programt::instructionst instructionst;
    typedef goto_functionst::function_mapt function_mapt;

    typedef std::string namet;
    typedef std::list<namet> namest;

    typedef code_function_callt::argumentst argumentst;

    class nodet: public graph_nodet<empty_edget>
    {
        namet fun_name;

      public:

        bool visited;

        nodet()
          :visited(false){}

        namet &name(){ return fun_name; }
    };

    typedef graph<nodet> grapht;

    grapht g;

    namespacet &ns;
    goto_functionst &goto_functions;

    std::map<namet, unsigned> fun2graph;

    void add_functions();
    void add_calls();

    namet name_of_call(const instructiont &instruction);

    bool not_interested_in(namet fun_name);

    void get_transitive_calls(namet &function, namest &calls);

    void clear_visited();
};

#endif
