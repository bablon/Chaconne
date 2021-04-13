/*
 * Stream Chain Buffer
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include "libregexp.h"

#define BUFSIZE		4096

struct stream_node {
	struct stream_node *next;
	unsigned char data[BUFSIZE];
	size_t tail, head;
};

struct stream {
	struct stream_node *first;
	struct stream_node *last;
	size_t count;
};

int stream_put(struct stream *s, const void *data, size_t c)
{
	size_t cap = 0, num;

	if (!c)
		return 0;

	if (s->last) {
		cap = BUFSIZE - s->last->head;
	}

	num = ((c - cap) + (BUFSIZE - 1)) / BUFSIZE;

	if (num) {
		int i;
		struct stream_node *ptr;
		struct stream_node *new = NULL;
		struct stream_node *last = NULL;

		for (i = 0; i < num; i++) {
			ptr = malloc(sizeof(*ptr));
			if (ptr == NULL)
				break;

			ptr->tail = ptr->head = 0;
			ptr->next = NULL;
			if (last == NULL) {
				new = last = ptr;
			} else {
				last->next = ptr;
				last = ptr;
			}
		}

		if (i < num) {
			for (; new; new = ptr) {
				ptr = new->next;
				free(new);
			}
			return -ENOMEM;	
		}

		if (s->last) {
			s->last->next = new;
		} else {
			s->first = s->last = new;
		}
	}

	s->count += c;

	while (c) {
		size_t block;

		block = BUFSIZE - s->last->head;
		if (block == 0) {
			s->last = s->last->next;
			block = BUFSIZE;
		}

		if (c < block)
			block = c;

		memcpy(s->last->data + s->last->head, data, block);

		data += block;
		c -= block;
		s->last->head += block;
	}

	return 0;
}

int stream_putc(struct stream *s, int c)
{
	char ch = c;

	return stream_put(s, &ch, 1);
}

int stream_putstrn(struct stream *s, const char *str, size_t n)
{
	return stream_put(s, str, n);
}

int stream_vputs(struct stream *s, const char *fmt, va_list ap)
{
	int l;
	char buf[256], *ptr = buf;
	va_list ap_copy;

	va_copy(ap_copy, ap);
	l = vsnprintf(buf, sizeof(buf), fmt, ap);

	if (l >= sizeof(buf)) {
		l++;
		ptr = malloc(l);
		if (ptr == NULL)
			return -ENOMEM;

		l = vsnprintf(ptr, l, fmt, ap_copy);
	}
	va_end(ap_copy);

	stream_putstrn(s, ptr, l);
	if (ptr != buf)
		free(ptr);

	return l;
}

int stream_puts(struct stream *s, const char *fmt, ...)
{
	int l;
	va_list ap;

	va_start(ap, fmt);
	l = stream_vputs(s, fmt, ap);
	va_end(ap);

	return l;
}

void stream_dump(struct stream *s)
{
	int block;
	struct stream_node *ptr;

	for (ptr = s->first; ptr; ptr = ptr->next) {
		block = ptr->head - ptr->tail;
		write(1, ptr->data + ptr->tail, block);
	}
}

struct stream *stream_new(void)
{
	struct stream *s;
       
	s = calloc(1, sizeof(struct stream));
	if (s == NULL)
		return NULL;

	return s;
}

void stream_free(struct stream *s)
{
	struct stream_node *ptr, *next;

	for (ptr = s->first; ptr; ptr = next) {
		next = ptr->next;
		free(ptr);
	}

	free(s);
}

void stream_consume(struct stream *s, size_t c)
{
	size_t block;
	struct stream_node *ptr, *next;

	if (c > s->count)
		c = s->count;

	s->count -= c;

	for (ptr = s->first; c && ptr; ptr = next) {
		next = ptr->next;

		block = ptr->head - ptr->tail;

		if (c < block)
			block = c;

		ptr->tail += block;
		c -= block;

		if (ptr->tail == BUFSIZE) {
			free(ptr);	
			s->first = next;
		}
	}

	if (s->first == NULL)
		s->last = NULL;
}

size_t stream_ndata(struct stream *s)
{
	return s->count;
}

int stream_get(struct stream *s, void *buf, size_t c)
{
	int ret;
	size_t block;
	struct stream_node *ptr;

	if (s->count == 0)
		return 0;

	if (c > s->count)
		c = s->count;

	ret = c;

	for (ptr = s->first; c && ptr; ptr = ptr->next) {
		if (ptr->tail == BUFSIZE)
			continue;

		block = ptr->head - ptr->tail;

		if (c < block)
			block = c;

		memcpy(buf, ptr->data + ptr->tail, block);

		c -= block;
		buf += block;
	}

	stream_consume(s, ret);

	return ret;
}

int stream_iovec(struct stream *s, struct iovec **vec)
{
	struct iovec *iovec;
	int idx = 0, count = 0;
	struct stream_node *ptr;

	for (ptr = s->first; ptr; ptr = ptr->next)
		count++;

	if (!count)
		return 0;

	iovec = malloc(sizeof(struct iovec) * count);
	if (iovec == NULL)
		return -ENOMEM;

	*vec = iovec;

	for (ptr = s->first; ptr; ptr = ptr->next) {
		iovec[idx].iov_base = ptr->data + ptr->tail;
		iovec[idx++].iov_len = ptr->head - ptr->tail;
	}

	return count;
}

int stream_flush(struct stream *s, int fd)
{
	int count;
	struct iovec *iovec;

	count = stream_iovec(s, &iovec);
	if (count <= 0)
		return count;

	count = writev(fd, iovec, count);
	if (count > 0)
		stream_consume(s, count);
	free(iovec);

	return count;
}

#define CAPTURE_COUNT_MAX 255

int lre_check_stack_overflow(void *opaque, size_t alloca_size)
{
    return 0;
}

void *lre_realloc(void *opaque, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

int stream_flush_regexp(struct stream *s, int fd, const char *regexp, size_t rlen)
{
	char line[8192];
	char *ptr = line;
	char c;
	int len;
	int ret;
	char error_msg[64];
    	uint8_t *capture[CAPTURE_COUNT_MAX * 2];
	uint8_t *bc;

	bc = lre_compile(&len, error_msg, sizeof(error_msg), regexp, rlen, 0, NULL);
	if (!bc) {
		char buf[128];

		snprintf(buf, sizeof(buf), "lre_compile error: %s\n", error_msg);
		write(fd, buf, strlen(buf));

		stream_consume(s, s->count);
		return -1;
	}

	while (stream_get(s, &c, 1) == 1) {
		if (c == '\r' || c == '\n') {
			if (ptr == line)
				continue;
			ret = lre_exec(capture, bc, (uint8_t *)line, 0, ptr - line, 0, NULL);
			if (ret == 1) {
				write(fd, line, ptr - line);
				write(fd, "\r\n", 2);
			} else if (ret == -1) {
				char buf[128];
				snprintf(buf, sizeof(buf), "lre_exec returns -1\n");
				write(fd, buf, strlen(buf));
			}

			ptr = line;
		} else {
			*ptr++ = c;
		}
	}

	if (ptr > line) {
		ret = lre_exec(capture, bc, (uint8_t *)line, 0, ptr - line, 0, NULL);
		if (ret == 1) {
			write(fd, line, ptr - line);
		} else if (ret == -1) {
			char buf[128];
			snprintf(buf, sizeof(buf), "lre_exec returns -1\n");
			write(fd, buf, strlen(buf));
		}
	}

	return 0;
}

#if 0
int main(int argc, char *argv[])
{
	struct stream *s;
	char buf[16];

	s = stream_init(NULL);

	stream_put(s, "hello, world\n", 13);
	stream_put(s, "n\n", 2);
	stream_put(s, "\n", 1);
	stream_put(s, "ok\n", 3);
	stream_dump(s);

	stream_get(s, buf, 15);
	buf[13] = 0;
	printf("** %s", buf);
	stream_dump(s);

	stream_free(s);

	return 0;
}
#endif
