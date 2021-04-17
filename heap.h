#ifndef _HEAP_H_

#include <stddef.h>

struct heap;

size_t heap_len(struct heap *h);
void *heap_top(struct heap *h);
int heap_empty(struct heap *h);
int heap_insert(struct heap *h, void *elem);
void heap_extract(struct heap *h, void *ret);
void *heap_index(struct heap *h, size_t index);
void heap_update(struct heap *h, int pos);

struct heap *
heap_create(size_t alloc, size_t size,
	    int (*compare)(const void *a, const void *b, void *arg),
	    void (*update)(void *elem, int pos, void *arg), void *arg);
void heap_destroy(struct heap *h);

#endif
