#include "Galois/Runtime/sync_structures.h"

////////////////////////////////////////////////////////////////////////////
// ToAdd
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_ADD(to_add, uint32_t);
GALOIS_SYNC_STRUCTURE_REDUCE_SET(to_add, uint32_t);
GALOIS_SYNC_STRUCTURE_BROADCAST(to_add, uint32_t);
GALOIS_SYNC_STRUCTURE_BITSET(to_add);

////////////////////////////////////////////////////////////////////////////
// ToAddFloat
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_ADD(to_add_float, float);
GALOIS_SYNC_STRUCTURE_REDUCE_SET(to_add_float, float);
GALOIS_SYNC_STRUCTURE_BROADCAST(to_add_float, float);
GALOIS_SYNC_STRUCTURE_BITSET(to_add_float);

////////////////////////////////////////////////////////////////////////////
// # short paths
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_SET(num_shortest_paths, uint32_t);
GALOIS_SYNC_STRUCTURE_BROADCAST(num_shortest_paths, uint32_t);
GALOIS_SYNC_STRUCTURE_BITSET(num_shortest_paths);

////////////////////////////////////////////////////////////////////////////
// Succ
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_ADD(num_successors, uint32_t);
GALOIS_SYNC_STRUCTURE_REDUCE_SET(num_successors, uint32_t);
GALOIS_SYNC_STRUCTURE_BROADCAST(num_successors, uint32_t);
GALOIS_SYNC_STRUCTURE_BITSET(num_successors);

////////////////////////////////////////////////////////////////////////////
// Pred
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_ADD(num_predecessors, uint32_t);
GALOIS_SYNC_STRUCTURE_REDUCE_SET(num_predecessors, uint32_t);
GALOIS_SYNC_STRUCTURE_BROADCAST(num_predecessors, uint32_t);
GALOIS_SYNC_STRUCTURE_BITSET(num_predecessors);

////////////////////////////////////////////////////////////////////////////
// Trim
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_ADD(trim, uint32_t);
GALOIS_SYNC_STRUCTURE_REDUCE_SET(trim, uint32_t);
GALOIS_SYNC_STRUCTURE_BROADCAST(trim, uint32_t);
GALOIS_SYNC_STRUCTURE_BITSET(trim);

////////////////////////////////////////////////////////////////////////////
// Current Lengths
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_MIN(current_length, uint32_t);
GALOIS_SYNC_STRUCTURE_REDUCE_SET(current_length, uint32_t);
GALOIS_SYNC_STRUCTURE_BROADCAST(current_length, uint32_t);
GALOIS_SYNC_STRUCTURE_BITSET(current_length);

////////////////////////////////////////////////////////////////////////////
// Flag
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_SET(propogation_flag, uint8_t);
GALOIS_SYNC_STRUCTURE_BROADCAST(propogation_flag, uint8_t);
GALOIS_SYNC_STRUCTURE_BITSET(propogation_flag);

////////////////////////////////////////////////////////////////////////////
// Dependency
////////////////////////////////////////////////////////////////////////////

GALOIS_SYNC_STRUCTURE_REDUCE_SET(dependency, float);
GALOIS_SYNC_STRUCTURE_BROADCAST(dependency, float);
GALOIS_SYNC_STRUCTURE_BITSET(dependency);
