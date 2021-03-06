#include <stddef.h>
#include <stdlib.h>
#include <inttypes.h>

#include "vector.h"

struct vector *vector_create(size_t size, size_t alloc)
{
	struct vector *v;

	v = malloc(size * alloc);
	if (!v)
		return NULL;

	v->array = malloc(size * alloc);
	if (!v->array)
		return NULL;

	v->alloc = alloc;
	v->size = size;
	v->count = 0;

	return v;
}

void vector_destroy(struct vector *v)
{
	if (v) {
		free(v->array);	
		free(v);
	}
}

void *vector_push(struct vector *v)
{
	void *ret;
	if (v->count == v->alloc) {
		void *p;

		p = realloc(v->array, v->size * v->alloc * 2);
		if (p == NULL)
			return NULL;
		v->array = p;
	}

	ret = v->array + v->count * v->size;
	v->count++;
	return ret;
}
