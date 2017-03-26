#ifndef _STACK_H_INCLUDED
#define _STACK_H_INCLUDED

#include "compat.h"

struct stack_node_t {
    struct stack_node_t *next;
};

typedef struct stack_node_t *stack_t;

static int inline
stack_push(stack_t *root, struct stack_node_t *data)
{
    struct stack_node_t *top;

    do {
        top = *root;
        data->next = top;
    }
    while (!atomic_cmpset(root, top, data));

    return 0;
}

static int inline
stack_pop(stack_t *root, struct stack_node_t **data)
{
    struct stack_node_t *top;

    while ((top = *root) && !atomic_cmpset(root, top, top->next));
    *data = top;

    return 0;
}

#endif //_STACK_H_INCLUDED
