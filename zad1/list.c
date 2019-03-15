//
// Created by kamil on 13.03.19.
//

#include "list.h"

#include <stdio.h>
#include <stdlib.h>

void enqueue(node_t **head, token_t* val) {
    node_t *new_node = malloc(sizeof(node_t));
    if (!new_node) return;

    new_node->val = val;
    new_node->next = *head;

    *head = new_node;
}

token_t* dequeue(node_t **head) {
    node_t *current, *prev = NULL;
    token_t* retval = NULL;

    if (*head == NULL) return NULL;

    current = *head;
    while (current->next != NULL) {
        prev = current;
        current = current->next;
    }

    retval = current->val;
    free(current);

    if (prev)
        prev->next = NULL;
    else
        *head = NULL;

    return retval;
}

int isEmpty(node_t *head){
    if(head == NULL)
        return 1;
    return 0;
}