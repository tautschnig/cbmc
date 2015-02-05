/**@file graph_specialisations.cpp
 * @author Kareem Khazem <karkhaz@karkhaz.com>
 * @date   2014
 * @brief  Template specialisations of util/graph.h functions
 */
#include "graph_specialisations.h"
#include "pretty_instruction.h"
#include <util/dot.h>
#include <iostream>
#include <sstream>

template<> void
graph<cfg_base_nodet<empty_cfg_nodet,goto_programt::const_targett> >
::output_dot(std::ostream &out) const
{
  typedef cfg_base_nodet<empty_cfg_nodet,
    goto_programt::const_targett> cfg_nodet;

  digraph_factoryt<int> fact;
  fact.node_default("shape", "box");

  for(unsigned n = 0; n < nodes.size(); n++)
  {
    cfg_nodet node = nodes[n];

    std::stringstream ss;

    ss << n << " / " << node.to_string();

    fact.node(n).set("label", ss.str());

    for(typename edgest::const_iterator
        it =  node.out.begin();
        it != node.out.end();
        it ++)
    {
      unsigned dst_nr = it->first;
      cfg_nodet dst_node = nodes[dst_nr];
      fact.edge(n, dst_nr);

      goto_programt::const_targett src = node.PC;
      goto_programt::const_targett dst = dst_node.PC;

      if(src->targets.size() == 1
      && src->get_target()->location_number == dst->location_number
      )
        fact.edge(n, dst_nr).set("color", "#859900");
    }

    for(typename edgest::const_iterator
        it =  node.spawn_points.begin();
        it != node.spawn_points.end();
        it ++)
    {
      unsigned dst_nr = it->first;
      cfg_nodet dst_node = nodes[dst_nr];
      fact.edge(n, dst_nr).set("color", "#cb4b16");
    }
  }

  fact.subgraph("legend").set("label", "Legend");

  fact.node_default("style", "filled")
      .node_default("color", "white");

  fact.node(-1).set("label", "");
  fact.node(-2).set("label", "");
  fact.edge(-1, -2)
    .set("label",
    "Node numbers:\ng / n where\ng = graph number\nn = location_number")
    .set("color", "white");
  fact.add_node_to_sub(-1, "legend");
  fact.add_node_to_sub(-2, "legend");

  fact.node(-3).set("label", "a");
  fact.node(-4).set("label", "b");
  fact.edge(-3, -4)
    .set("label", "Order in\ngoto\nprogram");
  fact.add_node_to_sub(-3, "legend");
  fact.add_node_to_sub(-4, "legend");

  fact.node(-5).set("label", "a");
  fact.node(-6).set("label", "b");
  fact.edge(-5, -6)
    .set("label", "b is a\ntarget\nof a")
    .set("color", "#859900")
    .set("fontcolor", "#859900");
  fact.add_node_to_sub(-5, "legend");
  fact.add_node_to_sub(-6, "legend");

  fact.node(-7).set("label", "a");
  fact.node(-8).set("label", "b");
  fact.edge(-7, -8)
    .set("label", "a is in\nfunction\nspawned\nat b")
    .set("color", "#cb4b16")
    .set("fontcolor", "#cb4b16");
  fact.add_node_to_sub(-7, "legend");
  fact.add_node_to_sub(-8, "legend");

  fact.output(out);
}
