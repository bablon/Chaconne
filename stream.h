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

#ifndef __STREAM_H__
#define __STREAM_H__

#include <stdarg.h>
#include <sys/types.h>
#include <sys/uio.h>

extern struct stream *stream_new(void);
extern void stream_free(struct stream *s);

extern int stream_put(struct stream *s, const void *data, size_t c);
extern int stream_putc(struct stream *s, int c);
extern int stream_putstrn(struct stream *s, const char *str, size_t n);
extern int stream_vputs(struct stream *out, const char *fmt, va_list ap); 
extern int stream_puts(struct stream *out, const char *fmt, ...);
extern void stream_dump(struct stream *s);
extern void stream_consume(struct stream *s, size_t c);
extern int stream_flush(struct stream *s, int fd);
extern int stream_get(struct stream *s, void *buf, size_t c);
extern int stream_iovec(struct stream *s, struct iovec **vec);
extern size_t stream_ndata(struct stream *s);
extern int stream_flush_regexp(struct stream *s, int fd, const char *regexp, size_t rlen);

#endif
