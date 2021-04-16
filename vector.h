#ifndef _VECTOR_H_
#define _VECTOR_H_

#include <stddef.h>

struct vector *vector_create(size_t size, size_t alloc);
void vector_destroy(struct vector *v);
void *vector_push(struct vector *v);
void *vector_index(struct vector *v, int index);
size_t vector_len(struct vector *v);

#endif
