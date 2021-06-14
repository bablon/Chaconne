#ifndef _VECTOR_H_
#define _VECTOR_H_

#include <stddef.h>

struct vector {
	size_t alloc;
	size_t size;
	size_t count;

	void *array;
};

struct vector *vector_create(size_t size, size_t alloc);
void vector_destroy(struct vector *v);
void *vector_push(struct vector *v);

static inline void *vector_index(struct vector *v, int index)
{
	if (index < 0 || index >= v->count)
		return NULL;

	return v->array + index * v->size;
}

static inline size_t vector_len(struct vector *v)
{
	return v->count;
}

#endif
