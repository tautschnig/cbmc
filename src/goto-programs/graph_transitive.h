#ifndef GRAPH_TRANSITIVE_H
#define GRAPH_TRANSITIVE_H

#include "goto_functions.h"
#include "goto_program.h"

#include <util/graph.h>

class graph_transitivet
{
  public:
    graph_transitivet(
      namespacet _ns,
      goto_functionst _goto_functions
    ): g(), ns(_ns), goto_functions(_goto_functions)
    {}

    void operator()();

    void output();

  private:
    typedef goto_programt::instructiont instructiont;
    typedef goto_programt::instructionst instructionst;
    typedef goto_functionst::function_mapt function_mapt;

    typedef std::string namet;

    class nodet: public graph_nodet<empty_edget>
    {
        namet fun_name;
        bool visited;

      public:
        nodet()
          :visited(false){}

        namet &name(){ return fun_name; }
    };

    typedef graph<nodet> grapht;

    grapht g;

    namespacet ns;
    goto_functionst goto_functions;

    std::map<namet, unsigned> foo;

    void populate_initial();
    void propagate_calls();

    bool not_interested_in(namet fun_name);
};

#endif
