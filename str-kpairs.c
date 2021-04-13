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

#include <string.h>

#include "str-kpairs.h"

char *str_kpairs_get(struct str_kpairs *kps, const char *key)
{
	char *ptr;

	for (ptr = kps->buf; ptr < kps->last;) {
		if (*ptr == 0)
			return NULL;
		else if (strcmp(key, ptr) == 0) {
			ptr += strlen(ptr) + 1;
			return ptr;
		} else {
			ptr += strlen(ptr) + 1;
			ptr += strlen(ptr) + 1;
		}
	}

	return NULL;
}

int str_kpairs_set(struct str_kpairs *kps, const char *key, const char *value)
{
	char *val;

	val = str_kpairs_get(kps, key);
	if (val) {
		size_t len = strlen(value) + 1;
		size_t val_len = strlen(val) + 1;
		char *next = val + val_len;

		if (len == val_len) {
			strcpy(val, value);
		} else if (len > val_len) {
			size_t delta = len - val_len;

			if (kps->last + delta > kps->end)
				return -1;

			memmove(next + delta, next, kps->last - next);
			kps->last += delta;
			strcpy(val, value);
		} else {
			size_t delta = val_len - len;

			memmove(next - delta, next, kps->last - next);
			kps->last -= delta;
			strcpy(val, value);
		}
	} else {
		size_t key_len = strlen(key) + 1;
		size_t val_len = strlen(value) + 1;
		size_t total_len = key_len + val_len;

		if (total_len > (kps->end - kps->last))
			return -1;
		else {
			strcpy(kps->last, key);
			strcpy(kps->last + key_len, value);
			kps->last += total_len;
		}
	}

	return 0;
}

void str_kpairs_delete(struct str_kpairs *kps, const char *key)
{
	char *val;

	val = str_kpairs_get(kps, key);
	if (val == NULL)
		return;
	else {
		size_t key_len = strlen(key) + 1;
		size_t val_len = strlen(val) + 1;
		size_t total_len = key_len + val_len;

		memmove(val - key_len, val + val_len, kps->last - val - val_len);
		kps->last -= total_len;
		if (kps->last == kps->buf)
			kps->buf[0] = 0;
	}
}
