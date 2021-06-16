/*
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

#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "heap.h"

#define left(i)		(((i) << 1) + 1)
#define right(i)	(((i) << 1) + 2)
#define parent(i)	(i > 0 ? (i - 1) >> 1 : 0)

struct heap *
heap_create(size_t alloc, size_t size,
	    int (*compare)(const void *a, const void *b, void *arg),
	    void (*update)(void *elem, int pos, void *arg), void *arg)
{
	struct heap *h;

	if (compare == NULL)
		return NULL;

	h = calloc(1, sizeof(*h) + sizeof(size));
	if (h == NULL)
		return NULL;

	h->array = malloc(alloc * size);
	if (h->array == NULL) {
		free(h);	
		return NULL;
	}

	h->alloc = alloc;
	h->size = size;
	h->count = 0;

	h->compare = compare;
	h->update = update;
	h->arg = arg;

	return h;
}

void heap_destroy(struct heap *h)
{
	if (h) {
		free(h->array);	
		free(h);
	}
}

static void heapify(struct heap *h, int pos)
{
	int i, m, l, r;
	void *lp, *rp, *ip, *mp;

	for (i = pos, m = i; i < h->count; i = m) {
		l = left(i);
		r = right(i);
		lp = h->array + l * h->size;
		rp = h->array + r * h->size;
		ip = h->array + i * h->size;

		if (l < h->count && h->compare(ip, lp, h->arg) > 0)
			m = l;

		mp = h->array + m * h->size;
		if (r < h->count && h->compare(mp, rp, h->arg) > 0)
			m = r;

		if (i != m) {
			mp = h->array + m * h->size;

			memcpy(h->buf, mp, h->size);
			memcpy(mp, ip, h->size);
			memcpy(ip, h->buf, h->size);
			if (h->update) {
				h->update(ip, m, h->arg);
				h->update(mp, i, h->arg);
			}
		} else
			break;
	}
}

int heap_extract(struct heap *h, void *ret)
{
	if (h->count == 0)
		return -ENOENT;

	if (ret)
		memcpy(ret, h->array, h->size);

	h->count--;
	memcpy(h->array, h->array + h->count * h->size, h->size);

	heapify(h, 0);

	return 0;
}

static void heap_update_key(struct heap *h, int pos)
{
	int i = pos, p;

	for (; i > 0; i = p) {
		p = parent(i);
		void *pp = h->array + p * h->size;
		void *ip = h->array + i * h->size;

		if (h->compare(pp, ip, h->arg) <= 0)
			break;
		else {
			memcpy(h->buf, pp, h->size);
			memcpy(pp, ip, h->size);
			memcpy(ip, h->buf, h->size);
			if (h->update) {
				h->update(ip, p, h->arg);	
				h->update(pp, i, h->arg);
			}
		}
	}
}

void heap_update(struct heap *h, int pos)
{
	heap_update_key(h, pos);
	heapify(h, pos);
}

int heap_insert(struct heap *h, void *elem)
{
	int i;

	if (h->count == h->alloc)
		return -ENOMEM;

	i = h->count;
	memcpy(h->array + i * h->size, elem, h->size);
	h->count++;

	heap_update_key(h, i);

	return 0;
}
