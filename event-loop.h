/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __EVENT_LOOP_H__
#define __EVENT_LOOP_H__

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum {
	EVENT_READABLE = 0x01,
	EVENT_WRITABLE = 0x02,
	EVENT_HANGUP   = 0x04,
	EVENT_ERROR    = 0x08
};

typedef int (*event_loop_fd_func_t)(int fd, uint32_t mask, void *data);
typedef int (*event_loop_timer_func_t)(void *data);
typedef int (*event_loop_signal_func_t)(int signal_number, void *data);
typedef void (*event_loop_idle_func_t)(void *data);

struct event_loop *
event_loop_create(void);

void
event_loop_destroy(struct event_loop *loop);

struct event_source *
event_loop_add_fd(struct event_loop *loop,
		     int fd, int nodupfd, uint32_t mask,
		     event_loop_fd_func_t func,
		     void *data);

int
event_source_fd_update(struct event_source *source, uint32_t mask);

struct event_source *
event_loop_add_timer(struct event_loop *loop,
			event_loop_timer_func_t func,
			void *data);

struct event_source *
event_loop_add_signal(struct event_loop *loop,
			 int signal_number,
			 event_loop_signal_func_t func,
			 void *data);

int
event_source_timer_update(struct event_source *source,
			     int ms_delay);

int
event_source_remove(struct event_source *source);

void
event_source_check(struct event_source *source);

int
event_loop_dispatch(struct event_loop *loop, int timeout);

void
event_loop_dispatch_idle(struct event_loop *loop);

struct event_source *
event_loop_add_idle(struct event_loop *loop,
		       event_loop_idle_func_t func,
		       void *data);

int
event_loop_get_fd(struct event_loop *loop);

#ifdef  __cplusplus
}
#endif

#endif
