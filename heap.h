#ifndef _HEAP_H_

#include <stddef.h>

struct heap {
	size_t alloc;
	size_t size;
	size_t count;
	void *array;

	int (*compare)(const void *, const void *, void *arg);
	void (*update)(void *elem, int pos, void *arg);
	void *arg;
	unsigned char buf[];
};

static inline int heap_empty(struct heap *h)
{
	return h->count == 0;
}

static inline void *heap_top(struct heap *h)
{
	return h->count ? h->array : NULL;
}

static inline size_t heap_len(struct heap *h)
{
	return h->count;
}

static inline void *heap_index(struct heap *h, size_t index)
{
	if (index >= h->count)
		return NULL;
	return h->array + index * h->size;
}

int heap_insert(struct heap *h, void *elem);
int heap_extract(struct heap *h, void *ret);
void heap_update(struct heap *h, int pos);

struct heap *
heap_create(size_t alloc, size_t size,
	    int (*compare)(const void *a, const void *b, void *arg),
	    void (*update)(void *elem, int pos, void *arg), void *arg);
void heap_destroy(struct heap *h);

#endif
