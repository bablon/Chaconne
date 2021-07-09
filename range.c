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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "range.h"

enum {
	start,
	range_start,
	range_start_space,
	range_end,
	range_end_space,
	range_desc,
	delimiter,
	done,
};

/*
 * syntax:
 *
 * ranges = range
 * 	| range ranges
 * 	;
 *
 * range = range_start ' ' range_end ' ' description
 * 	;
 *
 * range_start = [0-9]+
 * 	;
 *
 * range_end = [0-9]+
 * 	;
 *
 * description = literal
 * 	| literal description
 * 	;
 */

int range_parse(const char *desc, struct range *range, size_t size, char **endptr)
{
	int state = start;
	const char *p = desc;
	int i = 0;
	struct range entry;
	const char *s;

	for (;;) {
		switch (*p) {
		case ' ':
		case '\t':
		case '\n':
			if (*p == '\n' && state == range_desc) {
				entry.desc = s;
				for (s = p; s > entry.desc; s--)
					if (*(s-1) != ' ')
						break;
				entry.desc_len = s - entry.desc;
				if (i == size) {
					printf("%s %d %.*s.\n", __FUNCTION__, __LINE__, (int)(p-desc+1), desc);
					return -1;
				}
				range[i++] = entry;
				if (endptr)
					*endptr = (char *)p;

				state = done;
				break;
			}

			if (state == range_start) {
				entry.start = strtoul(s, NULL, 10);
				state = range_start_space;
			} else if (state == range_end) {
				entry.end = strtoul(s, NULL, 10);
				state = range_end_space;
			}
			p++;
			break;
		case ',':
			if (state != range_desc) {
				printf("%s %d %.*s.\n", __FUNCTION__, __LINE__, (int)(p-desc+1), desc);
				return -1;
			}
			state = delimiter;

			entry.desc = s;
			for (s = p; s > entry.desc; s--)
				if (*(s-1) != ' ')
					break;
			entry.desc_len = s - entry.desc;
			if (i == size) {
				printf("%s %d %.*s.\n", __FUNCTION__, __LINE__, (int)(p-desc+1), desc);
				return -1;
			}
			range[i++] = entry;

			p++;
			break;
		case '\0':
			if (state != range_desc && state != delimiter)
				return -1;

			entry.desc = s;
			for (s = p; s > entry.desc; s--)
				if (*(s-1) != ' ')
					break;
			entry.desc_len = s - entry.desc;
			if (i == size) {
				printf("%s %d %.*s.\n", __FUNCTION__, __LINE__, (int)(p-desc+1), desc);
				return -1;
			}
			range[i++] = entry;

			state = done;
			if (endptr)
				*endptr = (char *)p;
			break;
		default:
			if (*p <= '9' && *p >= '0') {
				if (state == start || state == delimiter) {
					s = p;
					state = range_start;
				} else if (state == range_start_space) {
					s = p;
					state = range_end;
				} else if (state == range_end_space) {
					s = p;
					state = range_desc;
				}

				p++;
				break;
			} else if (isgraph(*p)) {
				if (state == range_end_space) {
					s = p;
					state = range_desc;
				} else if (state != range_desc) {
					printf("%s %d %.*s.\n", __FUNCTION__, __LINE__, (int)(p-desc+1), desc);
					return -1;
				}

				p++;
				break;
			}
			printf("%s %d %.*s.\n", __FUNCTION__, __LINE__, (int)(p-desc+1), desc);
			return -1;
		}

		if (state == done)
			break;
	}

	return i;
}

#if 0
int main(void)
{
	struct range ranges[32];
	const char *line = "30 25 first desc, 20 15 second desc, 10 5 third desc";
	int i, r;

	r = range_parse(line, ranges, 32);
	assert(r > 0);
	assert(r == 3);

	for (i = 0; i < r; i++) {
		struct range *p = &ranges[i];
		printf("%d %d %.*s\n", p->start, p->end, (int)p->desc_len, p->desc);
	}

	return 0;
}
#endif
