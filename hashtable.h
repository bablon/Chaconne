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

#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#include <stddef.h>

struct kpattr {
	int key_is_ptr;
	int value_is_ptr;

	size_t key_size;
	size_t value_size;

	size_t (*hash)(const void *data);
	int (*compare)(const void *a, const void *b);
	void (*free_key)(void *data);
	void (*free_value)(void *data);
};

struct hashtable *hashtable_create(size_t bucket_size, struct kpattr *attr);
void *hashtable_get(struct hashtable *h, const void *key);
void hashtable_set(struct hashtable *h, void *key, void *data);
void hashtable_destroy(struct hashtable *h);
void hashtable_delete(struct hashtable *h, void *key);
void hashtable_travel(struct hashtable *h, void (*func)(const void *k, const void *v));

#endif
