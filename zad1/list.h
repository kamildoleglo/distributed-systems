//
// Created by kamil on 13.03.19.
//

#ifndef ZAD1_LIST_H
#define ZAD1_LIST_H
#include "client.h"

typedef struct node {
    token_t* val;
    struct node *next;
} node_t;

void enqueue(node_t **head, token_t* val);
token_t* dequeue(node_t **head);
int isEmpty(node_t *head);
#endif //ZAD1_LIST_H
