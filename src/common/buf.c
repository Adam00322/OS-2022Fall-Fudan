#include<common/buf.h>

int bufqueue_push(Queue* q, buf* b){
    queue_lock(q);
    queue_push(q, &b->bnode);
    int sz = q->sz;
    queue_unlock(q);
    return sz;
}

void bufqueue_pop(Queue* q){
    queue_lock(q);
    queue_pop(q);
    queue_unlock(q);
}

buf* bufqueue_front(Queue* q){
    queue_lock(q);
    if(queue_empty(q)){
        queue_unlock(q);
        return NULL;
    }
    auto b = container_of(queue_front(q), buf, bnode);
    queue_unlock(q);
    return b;
}

bool bufqueue_empty(Queue* q){
    queue_lock(q);
    auto x = queue_empty(q);
    queue_unlock(q);
    return x;
}