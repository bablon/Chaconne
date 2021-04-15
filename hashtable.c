/*
 * Generic Hash Table
 *
 * Copyright (c) 2021 Jiajia Liu <liujia6264@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hashtable.h"

struct elem {
	struct elem *next;
	size_t hash;
	unsigned char key[];
};

#define ALIGN(n, a)	((n + (a - 1)) & ~(a - 1))
#define KSIZE(h)	ALIGN(h->attr.key_size, sizeof(void *))
#define VSIZE(h)	ALIGN(h->attr.value_size, sizeof(void *))
#define KPTR(h, e)	(e->key)
#define VPTR(h, e)	(e->key + KSIZE(h))

#define ELEMSIZE(h)	(sizeof(struct elem) + KSIZE(h) + VSIZE(h))

struct node {
	size_t count;
	struct elem *next;
};

struct hashtable {
	struct kpattr attr;
	size_t bucket_size;
	struct node *node;
	size_t count;
	size_t factor;
};

static inline void *keyptr(struct hashtable *h, struct elem *e)
{
	if (!h->attr.key_is_ptr)
		return e->key;
	else
		return *(void **)e->key;
}

static inline void *valueptr(struct hashtable *h, struct elem *e)
{
	if (!h->attr.value_is_ptr)
		return VPTR(h, e);
	else
		return *(void **)VPTR(h, e);
}

static inline void copy_key(struct hashtable *h, struct elem *e, void *key)
{
	if (!h->attr.key_is_ptr)
		memcpy(KPTR(h, e), key, h->attr.key_size);
	else
		*((void **)KPTR(h, e)) = key;
}

static inline void copy_value(struct hashtable *h, struct elem *e, void *value)
{
	if (!h->attr.value_is_ptr)
		memcpy(VPTR(h, e), value, h->attr.value_size);
	else
		*((void **)VPTR(h, e)) = value;
}

static inline void free_key(struct hashtable *h, struct elem *e)
{
	if (h->attr.free_key) {
		void *src = KPTR(h, e);
		if (h->attr.key_is_ptr)
			src = *(void **)src;
		h->attr.free_key(src);
	}
}

static inline void free_value(struct hashtable *h, struct elem *e)
{
	if (h->attr.free_value) {
		void *src = VPTR(h, e);
		if (h->attr.value_is_ptr)
			src = *(void **)src;
		h->attr.free_value(src);
	}
}

static bool attr_ok(struct kpattr *attr)
{
	if (!attr->hash || !attr->compare)
		return false;
	if (!attr->key_is_ptr && !attr->key_size)
		return false;
	if (!attr->value_is_ptr && !attr->value_size)
		return false;

	return true;
}

struct hashtable *hashtable_create(size_t bucket_size, struct kpattr *attr)
{
	struct hashtable *h;

	h = calloc(1, sizeof(struct hashtable));
	if (!h)
		return NULL;

	h->factor = 80;
	if (bucket_size == 0)
		h->bucket_size = 16;

	h->node = calloc(bucket_size, sizeof(struct node));
	if (!h->node) {
		free(h);
		return NULL;
	}

	memcpy(&h->attr, attr, sizeof(*attr));

	if (!attr_ok(attr)) {
		free(h->node);
		free(h);
		return NULL;
	}

	h->bucket_size = bucket_size;
	if (h->attr.key_is_ptr)
		h->attr.key_size = sizeof(void *);
	if (h->attr.value_is_ptr)
		h->attr.value_size = sizeof(void *);

	return h;
}

void *hashtable_get(struct hashtable *h, const void *key)
{
	size_t hash;
	struct node *node;

	hash = h->attr.hash(key);
	node = h->node + (hash % h->bucket_size);

	if (node->count == 0)
		return NULL;
	else {
		struct elem *elem;

		for (elem = node->next; elem; elem = elem->next) {
			if (h->attr.compare(keyptr(h, elem), key) == 0)
				return valueptr(h, elem);
		}

		return NULL;
	}
}

void hashtable_grow(struct hashtable *h)
{
	size_t i;
	struct node *node;
	size_t bs = h->bucket_size * 2;

	node = calloc(1, sizeof(*node) * bs);
	assert(node);

	for (i = 0; i < h->bucket_size; i++) {
		struct elem *e, *n;

		for (e = h->node[i].next; e; e = n) {
			int index = e->hash % bs;

			n = e->next;
			e->next = node[index].next;
			node[index].next = e;
			node[index].count++;
		}
	}

	free(h->node);
	h->node = node;
	h->bucket_size = bs;
}

void hashtable_set(struct hashtable *h, void *key, void *data)
{
	size_t hash;
	struct node *node;
	struct elem **pp;
	struct elem *new;

	hash = h->attr.hash(key);
	node = h->node + (hash % h->bucket_size);

	for (pp = &node->next; *pp; pp = &(*pp)->next) {
		if (h->attr.compare(keyptr(h, *pp), key) == 0) {
			void *v = valueptr(h, *pp);

			if (!h->attr.value_is_ptr || v != data) {
				free_value(h, *pp);
			}

			copy_value(h, *pp, data);
			return;
		}
	}

	new = calloc(1, ELEMSIZE(h));
	assert(new);

	*pp = new;
	new->hash = hash;
	copy_key(h, new, key);
	copy_value(h, new, data);
	node->count++;
	h->count++;

	if (h->count > h->factor * h->bucket_size / 100) {
		printf("growing hashtable at count %zu\n", h->count);
		hashtable_grow(h);
	}
}

void hashtable_destroy(struct hashtable *h)
{
	size_t i;

	for (i = 0; i < h->bucket_size; i++) {
		struct elem *ptr, *next;

		for (ptr = h->node[i].next; ptr; ptr = next) {
			next = ptr->next;

			free_key(h, ptr);
			free_value(h, ptr);
			free(ptr);
		}
	}

	free(h->node);
	free(h);
}

void hashtable_delete(struct hashtable *h, void *key)
{
	size_t hash;
	struct node *node;
	struct elem **pp;

	hash = h->attr.hash(key);
	node = h->node + (hash % h->bucket_size);

	for (pp = &node->next; *pp; pp = &(*pp)->next) {
		if (h->attr.compare(keyptr(h, *pp), key) == 0) {
			struct elem *cur = *pp;

			*pp = cur->next;
			free_key(h, cur);
			free_value(h, cur);
			free(cur);
			node->count--;
			h->count--;
			return;
		}
	}
}

void hashtable_travel(struct hashtable *h, void (*func)(const void *k, const void *v))
{
	size_t i;

	for (i = 0; i < h->bucket_size; i++) {
		struct elem *e;

		for (e = h->node[i].next; e; e = e->next) {
			func(keyptr(h, e), valueptr(h, e));
		}
	}
}

void hashtable_stat(struct hashtable *h)
{
	size_t i;

	for (i = 0; i < h->bucket_size; i++) {
		printf("bucket %02zd: count %zd\n", i, h->node[i].count);
	}
}
