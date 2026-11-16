#ifndef CORE_LIST_H
#define CORE_LIST_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Intrusive circular doubly-linked list.
 *
 * "Intrusive" means the link lives inside the object being listed, so
 * putting an object on a list needs no memory allocation, which matters in
 * the allocator itself. A list is a `struct list_node` used as a head; an
 * empty list is a head that points at itself.
 *
 * Locking is the caller's responsibility.
 */
struct list_node {
    struct list_node *next;
    struct list_node *prev;
};

/* Recovers the object from a pointer to the list_node embedded in it. */
#define container_of(pointer, type, member) \
    ((type *)((char *)(pointer) - offsetof(type, member)))

static inline void list_init(struct list_node *head)
{
    head->next = head;
    head->prev = head;
}

static inline bool list_is_empty(const struct list_node *head)
{
    return head->next == head;
}

static inline void list_insert_between(struct list_node *node, struct list_node *before, struct list_node *after)
{
    node->prev = before;
    node->next = after;
    before->next = node;
    after->prev = node;
}

static inline void list_push_front(struct list_node *head, struct list_node *node)
{
    list_insert_between(node, head, head->next);
}

static inline void list_push_back(struct list_node *head, struct list_node *node)
{
    list_insert_between(node, head->prev, head);
}

static inline void list_remove(struct list_node *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

/* Iterates over the nodes of a list. The body must not remove `node`. */
#define list_for_each(node, head) \
    for (struct list_node *node = (head)->next; node != (head); node = node->next)

#endif
