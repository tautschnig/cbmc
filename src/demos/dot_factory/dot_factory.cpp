/* Demo of util/dot.h dot_factoryt API.
 * author: Kareem Khazem <karkhaz@karkhaz.com>
 */

#include "util/dot.h"
#include <cassert>
#include <iostream>

int main()
{
  // The node identifiers will be ints; you could also use strings
  digraph_factoryt<int> fact;

  // All nodes will have these attributes when created
  fact.node_default("color", "red").node_default("shape", "box");

  fact.node(1);                               // The node() method can
                                              // be used to create a
                                              // new node...
  assert(fact.node(1).get("color") == "red"); // ...and also to
                                              // return an existing
                                              // node.

  // Similar thing happens here. Node is created and returned.
  assert(fact.node(2).get("color") == "red");

  // Since node() returns a node, we can call methods on it to set
  // attributes.
  fact.node(3).set("color", "blue");
  assert(fact.node(3).get("color") == "blue"); 

  // All edges will have these attributes when created
  fact.edge_default("color", "blue").edge_default("style", "bold");

  // We can create an edge between existing nodes...
  fact.edge(1, 3);
  // ... or create an edge to a new node (5 in this case).
  fact.edge(1, 5);

  // edge() returns edges as well as creating them, just line node()
  assert(fact.edge(5, 2).get("color") == "blue");

  fact.edge(1, 3).set("label", "thin").set("style", "solid")
                 .set("color", "pink").set("fontcolor", "purple");

  // We can explicitly create a new edge, rather than returning an old
  // one, by using create_edge. Note that we have already created an
  // edge between 1 and 3 above...
  fact.create_edge(1,3);

  // And again. create_edge returns the new edge, so you can set its
  // attributes as normal.
  fact.create_edge(1,3).set("color", "green");

  // We can change node attributes after they have been added to edges
  fact.node(5).set("label", "the red one").set("style", "filled");

  // Create a subgraph. The label of the subgraph must be globally
  // unique.
  fact.subgraph("subgraph_label");

  // Subgraphs can have attributes too
  fact.subgraph("subgraph_label").set("label", "A subgraph yaay");

  // A new node is created, since node 6 didn't exist before
  fact.add_node_to_sub(6, "subgraph_label");

  // add_node_to_sub returns the attributes of the node added, so you
  // can set attributes as normal.
  fact.add_node_to_sub(7, "subgraph_label").set("color", "orange")
      .set("style", "filled");

  fact.create_edge(6, 7);

  // Edges can extend in and out of subgraphs
  fact.create_edge(6, 5);

  // Finally, dump the graph to stdout
  fact.output(std::cout);

  return 0;
}
