#ifndef _MYHASHMAP_H
#define _MYHASHMAP_H
#include "common/defines.h"
#define HASHSIZE 128
struct hash_node_ {
    struct hash_node_* next;
};
typedef struct hash_node_ *hash_node;
struct hash_map_ {
    hash_node bullet[HASHSIZE];
};
typedef struct hash_map_ *hash_map;
/* NOTE:You should add lock when use */
void _hashmap_init(hash_map map);
int _hashmap_insert(hash_node node, hash_map map, int (*hash)(hash_node node));
void _hashmap_erase(hash_node node, hash_map map, int (*hash)(hash_node node));
hash_node _hashmap_lookup(hash_node node, hash_map map, int (*hash)(hash_node node), bool (*hashcmp)(hash_node node1, hash_node node2));
#endif