/*!
 * \brief  Creation of DOT-formatted graphs
 * \author Kareem Khazem <karkhaz@karkhaz.com>
 * \date   2014
 */

#ifndef _DOT_NODET_H_
#define _DOT_NODET_H_

#include <stdexcept>
#include <map>
#include <list>
#include <set>
#include <string>
#include <iostream>
#include <cassert>

/*! \brief A list of attributes for DOT graph nodes and edges.
 *
 * \sa dot_factoryt for an overview of this api.
 */
class attribute_list
{
public:
  /*! \brief Set attribute `key` to value `value`.
   *
   * See [this
   * documentation](http://www.graphviz.org/doc/info/attrs.html) for
   * the list of available keys and values. Setting two values for a
   * single key, one after the other, results in the first one being
   * overridden.
   */
  attribute_list  &set(std::string key, std::string value)
  {
    std::pair<std::string, std::string> pair(key, value);
    attributes.erase(key);
    attributes.insert(pair);
    attribute_list &node = *this;
    return node;
  }

  /*! \brief Returns the value of the attribute `key`.
   *
   * If no value has been set for `key`, then an invalid_argument
   * exception is thrown. Clients can avoid this by checking whether
   * the key has been set using is_defined().
   */
  std::string get(std::string key)
  {
    std::map<std::string, std::string>::iterator it
      = attributes.find(key);
    if(it == attributes.end())
      throw std::invalid_argument("Unknown key '" + key + "'");

    return it->second;
  }

  /* ! \brief Has the attribute `key` been set for this attribute
   * list, explicitly or by default value.
   */
  bool is_defined(std::string key)
  {
    std::map<std::string, std::string>::iterator it
      = attributes.find(key);
    return !(it == attributes.end());
  }

  /*! Initialises this attribute lists's attributes with
   * `initial_attributes`.
   */
  attribute_list(std::map<std::string, std::string>
      &initial_attributes)
  {
    std::map<std::string, std::string> tmp(initial_attributes);
    attributes = tmp;
  }

  std::string to_s()
  {
    std::string ret = "";
    std::map<std::string, std::string>::iterator it;
    for(it = attributes.begin(); it != attributes.end(); it++)
    {
      ret += it->first + " = \"" + it->second + "\"";
      if(it != attributes.end()) ret += ", ";
    }
    ret += "";
    return ret.substr(0, ret.length() - 2);
  }

protected:
  std::map<std::string, std::string> attributes;
};

/*! \brief Base class for DOT graph creators.
 *
 * This virtual class is the interface for the concrete derived
 * classes, \ref digraph_factoryt and \ref graph_factoryt (for
 * creating directed and undirected DOT graphs).
 *
 * This class is intended to offer a simple and uniform interface for
 * creating DOT graphs. It can easily be used for dumping CFGs and
 * other objects derived from \ref graph, but is very flexible and can
 * be used to create any ad-hoc DOT graph you like.
 *
 * The following code demonstrates a typical use-case of dot factory,
 * and acts as a brief regression test. It is located in
 * `src/demos/dot_factory.cpp`.
 *
 * We can generate the following graph with just 20 lines of
 * uncommented code, shown below:
 *
 * ![The resulting DOT graph](/home/kareem/doc/cbmc/src/demos/dot.png)
 *
 * \include "dot_factory.cpp"
 *
 * dot_factory makes it easy to add useful information to graphs. For
 * example, if you are looking for thread spawns/kills in a huge
 * graph, it is useful to be able to colour those nodes in. Doing this
 * manually is kludgy and wasteful.
 *
 * ![A CFG](/home/kareem/doc/cbmc/src/demos/krcu.png)
 *
 * \tparam node_id The type of the node identifiers. %Node identifiers
 * in DOT files are typically integers, like `0 -> 1`, or strings,
 * like `foo -> bar`. Derived classes of dot_factoryt should probably
 * thus be instantiated with either `int`s or `std::string`s.
 */
template<class node_id> class dot_factoryt
{
public:
  //! Subgraph labels in DOT files
  typedef std::string subgraph_id;

protected:
  typedef std::map<node_id, attribute_list> node_mapt;
  node_mapt node_map;

  typedef std::map<std::pair<node_id, node_id>,
            std::list<attribute_list> > edge_mapt;
  edge_mapt edge_map;

  typedef std::map<subgraph_id, attribute_list> subgraph_mapt;
  subgraph_mapt subgraph_map;

  typedef std::set<node_id> node_sett;
  typedef std::map<subgraph_id, node_sett> subgraph2nodes_mapt;
  subgraph2nodes_mapt sub2node;

public:
  //! @{ \name Nodes and edges

  /*! \brief Retrieves or creates a node with a particular id.
   *
   * If this dot factory does not contain a node with id `id`, then
   * that node is created with default attributes and a reference to
   * the node is returned. If this dot factory does contain a node
   * with id `id`, then a reference to that node is returned. In
   * either case, setter methods can be called on the returned
   * reference in a chain to set the node's attributes:
   *
   *     factory.node(3).set("shape", "box").set("label", "foo");
   */
  attribute_list &node(node_id id)
  {
    typename node_mapt::iterator n = node_map.find(id);

    if(n != node_map.end())
      return n->second;

    // Node `id` did not already exist. Create it
    attribute_list node(node_defaults);
    std::pair<typename node_mapt::iterator, bool> ret;
    ret = node_map.insert(std::pair<node_id, attribute_list>(id, node));

    typename node_mapt::iterator it = node_map.find(id);
    attribute_list &ref = it->second;
    return ref;
  }

  /*! \brief Retrieves or creates an edge between two nodes.
   *
   * If this dot factory does not contain an edge between the source
   * and the sink, then that edge is created with default attributes
   * and a reference to the edge is returned. If this dot factory does
   * contain an edge between the source and the sink, then a reference
   * to that edge is returned. In either case, setter methods can be
   * called on the returned reference in a chain to set the edge's
   * attributes:
   *
   *     factory.edge(42, 18).set("color", "red").set("label", "foo");
   *
   * If either `source` and/or `sink` are not part of the graph, they
   * will be added to the graph.
   *
   * If a client really wants to create a second edge between two
   * nodes when an edge already exists, they can call the
   * create_edge() function.
   *
   * If two edges exist between two nodes, then the edge that shall be
   * returned by this function shall be the last edge created.
   *
   * \attention If this dot factory is a digraph, then the order of
   * `source` and `sink` matters. Else, it does not. \sa digraph().
   */
  virtual attribute_list &edge(node_id source, node_id sink) = 0;

  /*! \brief Explicitly creates a new edge between `source` and
   * `sink`---does not return an old edge if one already exists.
   *
   * \return A reference to the new edge that was created.
   *
   * \attention If a client creates a new edge between two nodes when
   * an old edge already existed, then the client loses the ability to
   * modify the attributes of the old node using edge().
   */
  attribute_list &create_edge(node_id source, node_id sink)
  {
    attribute_list attr_list(edge_defaults);

    node(source);
    node(sink);
    std::pair<node_id, node_id> ends(source, sink);
    typename edge_mapt::iterator found = edge_map.find(ends);

    if(found == edge_map.end())
    {
      std::list<attribute_list> edge_set;
      edge_set.push_back(attr_list);

      std::pair<std::pair<node_id, node_id>,
                std::list<attribute_list> >   edge(ends, edge_set);
      edge_map.insert(edge);
    }
    else
    {
      (found->second).push_back(attr_list);
    }

    typename edge_mapt::iterator it;
    it = edge_map.find(ends);
    attribute_list &ref = (it->second).back();
    return ref;
  }
  //! @}

  //! @{ \name Subgraphs


  /*! \brief Retrieves or creates a subgraph with a particular id.
   *
   * If this dot factory does not contain a subgraph with id `id`,
   * then that subgraph is created and a reference to the subgraph is
   * returned. If this dot factory does contain a subgraph with id
   * `id`, then a reference to that subgraph is returned. In either
   * case, setter methods can be called on the returned reference in a
   * chain to set the subgraph's attributes:
   *
   *     factory.subgraph(3).set("label", "foo");
   */
  attribute_list &subgraph(subgraph_id id)
  {
    subgraph_mapt::iterator n = subgraph_map.find(id);
    if(n != subgraph_map.end()) return n->second;

    // Subgraph `id' did not alreeady exist. Create it
    std::map<std::string, std::string> defs;
    attribute_list atts(defs);
    std::pair<subgraph_id, attribute_list> p1(id, atts);
    subgraph_map.insert(p1);

    // Subgraph consists of two maps, create the second one also
    node_sett node_set;
    std::pair<subgraph_id, node_sett> p2(id, node_set);
    sub2node.insert(p2);

    subgraph_mapt::iterator it = subgraph_map.find(id);
    attribute_list &ref = it->second;
    return ref;
  }

  /*! \brief Adds a node to a subgraph.
   *
   * If no subgraph with id `sid` exists prior to a call to this
   * method, then that subgraph will be created (as if `subgraph(sid)`
   * had been called).
   *
   * If no node with id `nid` exists prior to a call to this method,
   * then that node will be created with the default node attributes
   * (as if `node(nid)` had been called).
   *
   * The return value is the attributes of the node, so clients can
   * set node attributes in a chain on the returned value:
   *
   *      fact.add_node_to_sub(1, 4).set("label", "node 1");
   *
   *  \attention It is the client's responsibility to check for adding
   *  a node to two different subgraphs. Such a thing is allowed in
   *  the DOT language, with some caveats. Be aware that if a node is
   *  added to subgraph A and then to subgraph B, that node will not
   *  be removed from subgraph A.
   */
  attribute_list &add_node_to_sub(node_id nid, subgraph_id sid)
  {
    subgraph(sid);
    typename subgraph2nodes_mapt::iterator it = sub2node.find(sid);
    // Node set should have been initialised by call to subgraph().
    assert(it != sub2node.end());
    (it->second).insert(nid);
    return node(nid);
  }

  //! @}

  //! @{ \name Graph attributes

  //! \brief Is this a digraph factory?
  virtual bool digraph() = 0;

  /*! \brief Sets the default attributes for all nodes created in this
   * dot factory.
   *
   * A reference to this dot factory is returned, so default
   * attribute-setting can be chained in the following style:
   *
   *     factory.node_default("shape", "box")
   *            .node_default("color", "red")
   *            .edge_default("color", "black");
   *
   * This function affects the default attributes for those nodes that
   * are created _after a call to this function_. For this reason, the
   * following usage is recommended:
   * - Set the default attributes just after constructing a new
   *   factory
   * - Avoid setting new defaults after creating new nodes or edges,
   *   as this can make your code hard to follow.
   */
  dot_factoryt &node_default(std::string key, std::string value)
  {
    std::pair<std::string, std::string> attr(key, value);
    node_defaults.erase(key);
    node_defaults.insert(attr);
    return *this;
  }

  /*! \brief Sets the default attributes for all edges created in this
   * dot factory.
   *
   * A reference to this dot factory is returned, so default
   * attribute-setting can be chained in the following style:
   *
   *     factory.node_default("shape", "box")
   *            .node_default("color", "red")
   *            .edge_default("color", "black");
   *
   * This function affects the default attributes for those edges that
   * are created _after a call to this function_. For this reason, the
   * following usage is recommended:
   * - Set the default attributes just after constructing a new
   *   factory
   * - Avoid setting new defaults after creating new nodes or edges,
   *   as this can make your code hard to follow.
   */
  dot_factoryt &edge_default(std::string key, std::string value)
  {
    std::pair<std::string, std::string> attr(key, value);
    edge_defaults.erase(key);
    edge_defaults.insert(attr);
    return *this;
  }
  //! @}

  //! @{ \name Output

  //! \brief Writes a complete DOT-formatted file to `out`.
  void output(std::ostream &out)
  {
    if(digraph()) out << "digraph G{\n";
    else          out << "graph G{\n";

    // Nodes that are part of subgraphs, so we don't want to print
    // them at the top level
    std::set<node_id> sub_nodes;

    // First, print subgraphs.

    subgraph_mapt::iterator sub_it;
    for(sub_it =  subgraph_map.begin();
        sub_it != subgraph_map.end();
        sub_it++)
    {
      subgraph_id id = sub_it->first;
      attribute_list atts = sub_it->second;
      out << "  subgraph \"cluster_" << id << "\"{\n";
      out << "    " << atts.to_s() << ";\n";

      typename subgraph2nodes_mapt::iterator s2n_it;
      s2n_it = sub2node.find(id);
      // Node set should have been initialised by call to subgraph().
      assert(s2n_it != sub2node.end());
      typename std::set<node_id> node_set;
      node_set = s2n_it->second;
      typename std::set<node_id>::iterator nodes_it;
      for(nodes_it =  node_set.begin();
          nodes_it != node_set.end();
          nodes_it++)
      {
        node_id node = *nodes_it;
        sub_nodes.insert(node);

        typename std::map<node_id, attribute_list>::iterator node_it;
        node_it = node_map.find(node);

        out <<  "    " << node << " [" << node_it->second.to_s()
            << "];\n";
      }
      out << "  }\n";
    }

    // Now print top-level nodes (not in subgraph)

    typename std::map<node_id, attribute_list>::iterator node_it;
    for(node_it =  node_map.begin();
        node_it != node_map.end();
        node_it++)
    {
      node_id id = node_it->first;
      typename std::set<node_id>::iterator sub_it;
      sub_it = sub_nodes.find(id);
      if(sub_it != sub_nodes.end())
        continue; // This node has already been printed in a subgraph

      out  << "  " << id << " [" << node_it->second.to_s()
           << "];\n";
    }

    typename edge_mapt::iterator edge_it;
    for(edge_it = edge_map.begin();
        edge_it != edge_map.end();
        edge_it++)
    {
      std::pair<node_id, node_id> ends = edge_it->first;
      std::list<attribute_list> attr_set = edge_it->second;
      std::list<attribute_list>::iterator attr_it = attr_set.begin();
      for(; attr_it != attr_set.end(); attr_it++)
      {
        attribute_list list = *attr_it;
        out  << "  " << ends.first << arrow() << ends.second << " "
             << "[" << list.to_s() << "]" << ";\n";
      }
    }

    out << "}\n"; // } <--- (for syntax highlighting, ignore).
  }
  //! @}

protected:
  std::map<std::string, std::string> node_defaults;
  std::map<std::string, std::string> edge_defaults;

  attribute_list &return_edge(node_id source, node_id sink)
  {
    std::pair<node_id, node_id> ends(source, sink);
    typename edge_mapt::iterator it;
    it = edge_map.find(ends);
    attribute_list &ref = (it->second).back();
    return ref;
  }

  inline std::string arrow()
  {
    if(digraph()) return " -> ";
    return " -- ";
  }
};

/*! \brief A dot factory that outputs directed graphs.
 *
 * \sa dot_factoryt for an overview of this class' usage.
 */
template<class node_id> class digraph_factoryt:
                        public dot_factoryt<node_id>
{
public:
  //! \brief `true`.
  inline bool digraph() { return true; }

  /*! \brief Retrieves or creates an edge between two nodes.
   *
   * If this dot factory does not contain an edge between the source
   * and the sink, then that edge is created with default attributes
   * and a reference to the edge is returned. If this dot factory does
   * contain an edge between the source and the sink, then a reference
   * to that edge is returned. In either case, setter methods can be
   * called on the returned reference in a chain to set the edge's
   * attributes:
   *
   *     factory.edge(42, 18).set("color", "red").set("label", "foo");
   *
   * \attention The order of `source` and `sink` matters for this
   * implementation.
   */
  attribute_list &edge(node_id source, node_id sink)
  {
    std::pair<node_id, node_id> pair(source, sink);
    if(this->edge_map.find(pair) != this->edge_map.end())
      return this->return_edge(source, sink);
    else
      return this->create_edge(source, sink);
  }
};

/*! \brief A dot factory that outputs undirected graphs.
 *
 * \sa dot_factoryt for an overview of this class' usage.
 */
template<class node_id> class graph_factoryt:
                        public dot_factoryt<node_id>
{
public:
  //! \brief `false`.
  inline bool digraph() { return false; }

  /*! \brief Retrieves or creates an edge between two nodes.
   *
   * If this dot factory does not contain an edge between the source
   * and the sink, then that edge is created with default attributes
   * and a reference to the edge is returned. If this dot factory does
   * contain an edge between the source and the sink, then a reference
   * to that edge is returned. In either case, setter methods can be
   * called on the returned reference in a chain to set the edge's
   * attributes:
   *
   *     factory.edge(42, 18).set("color", "red").set("label", "foo");
   *
   * \attention The order of `source` and `sink` does not matter for
   * this implementation.
   */
  attribute_list &edge(node_id source, node_id sink)
  {
    std::pair<node_id, node_id> pair1(source, sink);
    std::pair<node_id, node_id> pair2(sink, source);

    if(this->edge_map.find(pair1)      != this->edge_map.end())
      return return_edge(source, sink);
    else if(this->edge_map.find(pair2) != this->edge_map.end())
      return return_edge(sink, source);
    else
      return create_edge(source, sink);
  }
};

#endif
