/** @file spawn_marker.h
 *  @author Kareem Khazem <karkhaz@karkhaz.com>
 *  @date   2014
 *  @brief  Marking thread spawns on CFG
 */

#ifndef CPROVER_SPAWN_MARKER_H
#define CPROVER_SPAWN_MARKER_H

#include "goto_functions.h"
#include "cfg.h"
#include <map>
#include <ostream>

/** @brief Marking thread spawns on CFG
 *
 * This class is used to link instructions to their spawn points on a
 * cfg. If an instruction is in a function that is spawned using a
 * call to pthread_create, then the cfg_base_nodet of that instruction
 * will have its spawn_points member updated to have an edge to the
 * pthread_create call.
 *
 */
class spawn_markert
{
  typedef concurrent_cfg_baset<empty_cfg_nodet> cfgt;

  public:
    /** @brief  Updates the spawn_point member of every instructiont
     *          in the cfg.
     *
     * @pre   This spawn_markert has been initialised with a CFG,
     *        which itself has been initialised with a goto_functionst.
     * @post  For each node `target_node` in the CFG: 
     *            - Suppose that `target_node`'s `PC` member is an
     *            instructiont called `node_ins`.
     *            - Suppose that `node_ins` is in a goto_program that
     *            gets spawned using a call to `pthread_create()`
     *            - Suppose that the CFG node that contains that
     *            `pthread_create` call is `spawn_node`.
     *            .
     *        The postcondition of this function is that the CFG shall
     *        be updated. Each `target_node` will have its
     *        `spawn_points` member updated to have an edge pointing
     *        to `spawn_node`
     */
    void operator()();

    /** @pre cfg has been initialised with a goto_functionst
     */
    spawn_markert(cfgt &cfg, namespacet &ns);

    /** @pre    operator() has been called on this spawn_markert.
     *  @return A JSON-formatted representation of what functions are
     *          in each thread.
     */
    std::string pretty_function_groups();

  private:
    typedef goto_programt::const_targett targett;
    typedef goto_programt::targett m_target;

    cfgt &cfg;
    namespacet &ns;

    typedef std::map<unsigned, std::list<unsigned> >
      thread_functions_mapt;

    /** @param  pthread_ins an instructiont for a call to
     *          pthread_create
     *  @return the name of the function that pthread_create is
     *          spawning.
     */
    std::string pthread_target(targett &pthread_ins);

    /** @brief  called when we find a node that is the start point of a
     *          pthread_create call.
     * @param   spawn_node The CFG node of the pthread_create call
     * @param   target_node The CFG node where the spawned function
     *          starts executing (looks like `start_routine == foo()'
     *          on the CFG
     * @post    All nodes from target_node up to END_THREAD nodes get
     *          marked by having their `spawn_points' member having an
     *          edge pointing to `spawn_node'.
     */
    void start_routine(
        unsigned spawn_node,
        unsigned target_node);

    /** @param node a node on the cfg
     *  @return a pretty representation of the instruction in the node
     */
    std::string instruction_of(unsigned node);

    /// For debugging. Returns the location_number of the instructiont
    /// in the CFG node `node`.
    inline unsigned loc_of(unsigned node);
};

#endif
