/** @file graph_specialisations.h
 * @author Kareem Khazem <karkhaz@karkhaz.com>
 * @date   2014
 * @brief  Template specialisations of util/graph.h functions
 *
 * Some of the methods of graph<N> are too generic, and a more
 * specialised version is desirable for particular instantiations of
 * N. Such specialised functions should be written here.
 */

#ifndef CPROVER_GRAPH_SPECIALISATIONS_H
#define CPROVER_GRAPH_SPECIALISATIONS_H

#include <util/graph.h>
#include "cfg.h"

/** Dot output specifically for CFGs.
 *
 *  This function writes a pretty-printed version of each node's
 *  instruction into the node of the graph.
 */
template<> void
graph<cfg_base_nodet<empty_cfg_nodet,goto_programt::const_targett> >
::output_dot(std::ostream &out) const;

#endif
