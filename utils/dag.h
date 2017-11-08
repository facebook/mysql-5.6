#ifndef DAG_H
#define DAG_H

#include <my_global.h>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

/**
  @class DAG

  This class is generic container that represents a Directed Acyclic Graph (DAG)

  It provides an API to add nodes, add dependencies between nodes and to remove
  nodes from the head/top of the DAG. It also provides methods to traverse the
  DAG in topological order and assign special sync nodes on which every future
  parent-less node will depend on.

  It maintains dependencies in both directions internally for quick parent and
  children lookups.

  Note: This class does NOT guarantee thread safety
*/
template <class T>
class DAG {

public:
  /** Typedefs **/

  typedef std::unordered_set<T> node_set;
  typedef node_set& node_set_ref;
  typedef const node_set& const_node_set_ref;

  typedef std::unordered_map<T, node_set> dep_map;


  /** Callbacks **/

  /**
    Should return true to continue walking
    See @DAG::walk
  */
  typedef bool walk_func(const T& node, void* context);

  /**
    Callback for @DAG::topological_walk
    This function is called for every level of the DAG, @nodes are the nodes
    which belong to the same level
  */
  typedef void topological_walk_func(const_node_set_ref nodes);

  DAG() { }

private:
  dep_map forward_deps;
  dep_map backward_deps;

  /* Number of nodes in the DAG */
  size_t num_nodes= 0;

  /* Used to return references to empty sets in functions */
  const node_set empty_node_set;

  /* Sets representing the head and tail of the DAG */
  node_set head_set;
  node_set tail_set;

  /* Last sync node which was added to the DAG, this should only be accessed
     when @sync_node_exists is true */
  T last_sync_node;
  bool sync_node_exists= false;

  /* These functions deal with raw maps */
  inline void add_link(const T& from, const T& to)
  {
    DBUG_ASSERT(from != to);
    forward_deps[from].insert(to);
    backward_deps[to].insert(from);
  }

public:

  /**
    Adds a dependency between @from and @to
    @from will appear first in topological order i.e. @to depends on @from
  */
  void add_dependency(const T& from, const T& to)
  {
    // case: @from not found, do nothing
    if (!exists(from)) return;
    // case: @to not found, add it to the tail
    if (!exists(to))
    {
      tail_set.insert(to);
      // new nodes can only be added to the tail,
      // so inc only when adding to tail
      ++num_nodes;
    }
    else
    {
      // @to has a parent now, so remove from head
      head_set.erase(to);
    }

    add_link(from, to);

    // @from has a child now, so remove from tail
    tail_set.erase(from);
    DBUG_ASSERT(is_symmetric());
  }

  /**
    Make @node a sync node. Every node added without a parent will depend on the
    sync node if it exists. It makes sense to set only nodes at the bottom/tail
    of the DAG as sync nodes
  */
  void set_sync_node(const T& node)
  {
    last_sync_node= node;
    sync_node_exists= true;
  }

  /**
    Adds @node to the DAG by making it dependent on either the head or the last
    sync node (if it exists)
  */
  void add_node(const T& node)
  {
    DBUG_ASSERT(!exists(node));

    if (sync_node_exists)
    {
      add_link(last_sync_node, node);
      tail_set.erase(last_sync_node);
    }
    else
      head_set.insert(node);

    tail_set.insert(node);
    // new nodes can only be added to the tail,
    // so inc only when adding to tail
    ++num_nodes;

    DBUG_ASSERT(is_symmetric());
  }

  /**
    Removes @node from the DAG along with all its links
  */
  void remove_head_node(const T& node)
  {
    // case: not a head node
    if (get_head().find(node) == get_head().end())
    {
      DBUG_ASSERT(false);
      return;
    }

    if (sync_node_exists && node == last_sync_node) sync_node_exists= false;

    // remove child links
    if (!get_children(node).empty())
    {
      for (auto itr= forward_deps[node].begin();
           itr != forward_deps[node].end();)
      {
        DBUG_ASSERT(backward_deps.find(*itr) != backward_deps.end());
        // remove reverse link
        backward_deps[*itr].erase(node);
        // case: add to head if no parents
        if (backward_deps[*itr].empty())
        {
          head_set.insert(*itr);
          backward_deps.erase(*itr);
        }
        // remove link
        itr= forward_deps[node].erase(itr);
      }
    }

    // remove from head and tail set
    head_set.erase(node);
    tail_set.erase(node);

    // finally erase the node
    forward_deps.erase(node);
    backward_deps.erase(node);

    // this is the only function that removes nodes
    DBUG_ASSERT(num_nodes > 0);
    --num_nodes;

    DBUG_ASSERT(!exists(node));
    DBUG_ASSERT(is_symmetric());
  }

  /**
    Walks through all the nodes in the DAG and calls @func
    See @walk_func
  */
  void walk(walk_func *func, void *context) const
  {
    // case: send head nodes that are not in the dependency maps
    for (auto& node : head_set)
    {
      if (forward_deps.find(node) == forward_deps.end())
        if (!func(node, context)) return;
    }

    // case: send nodes in the dependency maps
    for (auto& entry : forward_deps)
      if (!func(entry.first, context)) return;

    // case: send tail nodes that are not in the dependency maps and head
    for (auto& node : tail_set)
    {
      if (forward_deps.find(node) == forward_deps.end() &&
          head_set.find(node) == head_set.end())
        if (!func(node, context)) return;
    }
  }

  /**
    Walks through all the nodes in the DAG in topological order and sends set of
    nodes in each level to @func
    See @topological_walk_func
  */
  void topological_walk(topological_walk_func *func) const
  {
    node_set nodes= get_head();
    node_set next_nodes;
    node_set visited;

    while (!nodes.empty())
    {
      func(nodes);
      visited.insert(nodes.begin(), nodes.end());
      for (auto& node : nodes)
      {
        for (auto& child : get_children(node))
        {
          bool insert= true;
          for (auto& parent : get_parents(child))
            // case: this node has a parent which is not visited yet
            if (!visited.count(parent)) { insert= false; break; }
          if (insert) next_nodes.insert(child);
        }
      }
      nodes= next_nodes;
      next_nodes.clear();
    }
    DBUG_ASSERT(size() == visited.size());
  }

  /* Returns the set of nodes that depend on @node */
  inline const_node_set_ref get_children(const T& node) const
  {
    if (forward_deps.find(node) == forward_deps.end()) return empty_node_set;
    return forward_deps.at(node);
  }

  /* Returns the set of nodes that @node depends on */
  inline const_node_set_ref get_parents(const T& node) const
  {
    if (backward_deps.find(node) == backward_deps.end()) return empty_node_set;
    return backward_deps.at(node);
  }

  /* Returns the set of nodes at the top of the DAG */
  inline const_node_set_ref get_head() const
  {
    return head_set;
  }

  /* Returns the set of nodes at the bottom of the DAG */
  inline const_node_set_ref get_tail() const
  {
    return tail_set;
  }

  inline bool exists(const T& node) const
  {
    return head_set.find(node) != head_set.end() ||
           forward_deps.find(node) != forward_deps.end() ||
           tail_set.find(node) != tail_set.end();
  }

  inline bool is_empty() const
  {
    return size() == 0;
  }

  inline size_t size() const
  {
#ifndef DBUG_OFF
    // check if num_nodes represent the real number of nodes
    node_set all_nodes;
    all_nodes.insert(head_set.begin(), head_set.end());
    all_nodes.insert(tail_set.begin(), tail_set.end());
    for (auto& entry : forward_deps) all_nodes.insert(entry.first);
    DBUG_ASSERT(all_nodes.size() == num_nodes);
#endif
    return num_nodes;
  }

  /**
    Determines if @to depend on @from, either directly or indirectly
  */
  bool path_exists(const T& from, const T& to) const
  {
    // case: one or both nodes don't exist
    if (forward_deps.find(from) == forward_deps.end() ||
        backward_deps.find(to) == backward_deps.end() ||
        from == to)
      return false;

    node_set visited;       // tracks visited nodes for cycle detection
    std::stack<T> checking; // stack for DFS traversal

    visited.insert(from);
    checking.push(from);

    while (!checking.empty())
    {
      auto node= checking.top();
      checking.pop();

      if (node == to) return true;

      for (auto& child : get_children(node))
        // case: child was successfully inserted i.e. there were no duplicates
        if (visited.insert(child).second) checking.push(child);
    }

    return false;
  }

private:

  /**
    This function checks if the forward and backward dependencies are
    symmetrical. It serves as a sanity check
  */
  bool is_symmetric() const
  {
    node_set forward;
    node_set backward;
    for (auto& entry : forward_deps)
    {
      forward.insert(entry.first);
      for (auto& child : entry.second)
      {
        forward.insert(child);
        if (!backward_deps.count(child) ||
            !backward_deps.at(child).count(entry.first))
          return false;
      }
    }

    for (auto& entry : backward_deps)
    {
      backward.insert(entry.first);
      for (auto& parent : entry.second)
      {
        backward.insert(parent);
        if (!forward_deps.count(parent) ||
            !forward_deps.at(parent).count(entry.first))
          return false;
      }
    }
    return forward.size() == backward.size();
  }
};

#endif /* DAG_H */
