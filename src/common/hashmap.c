#include "hashmap.h"

void _hashmap_init(hash_map map){
    for(int i=0; i<HASHSIZE; i++)
        map->bullet[i] = NULL;
}

int _hashmap_insert(hash_node node, hash_map map, int (*hash)(hash_node node)){
    node->next = NULL;
    int b = hash(node);
    hash_node t = map->bullet[b];
    if(t == NULL){
        map->bullet[b] = node;
        return 0;
    }
    while(t->next != NULL){
        if(t == node) return -1;
        t = t->next;
    }
    t->next = node;
    return 0;
}

void _hashmap_erase(hash_node node, hash_map map, int (*hash)(hash_node node)){
    int b = hash(node);
    hash_node pre = NULL;
    hash_node cur = map->bullet[b];
    while(cur){
        if(cur == node){
            if(pre == NULL) map->bullet[b] = cur->next;
            else pre->next = cur->next;
            break;
        }
        pre = cur;
        cur = cur->next;
    }
}

hash_node _hashmap_lookup(hash_node node, hash_map map, int (*hash)(hash_node node), bool (*hashcmp)(hash_node node1, hash_node node2)){
    int b = hash(node);
    hash_node cur = map->bullet[b];
    while(cur){
        if(hashcmp(cur, node)) return cur;
        cur = cur->next;
    }
    return NULL;
}