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

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "list.h"
#include "event-loop.h"

struct event_loop {
	int epoll_fd;
	struct list_head check_list;
	struct list_head idle_list;
	struct list_head destroy_list;
};

struct event_source_interface {
	int (*dispatch)(struct event_source *source,
			struct epoll_event *ep);
};

struct event_source {
	struct event_source_interface *interface;
	struct event_loop *loop;
	struct list_head link;
	void *data;
	int fd;
};

struct event_source_fd {
	struct event_source base;
	event_loop_fd_func_t func;
	int fd;
};

static int
event_source_fd_dispatch(struct event_source *source,
			    struct epoll_event *ep)
{
	struct event_source_fd *fd_source = (struct event_source_fd *) source;
	uint32_t mask;

	mask = 0;
	if (ep->events & EPOLLIN)
		mask |= EVENT_READABLE;
	if (ep->events & EPOLLOUT)
		mask |= EVENT_WRITABLE;
	if (ep->events & EPOLLHUP)
		mask |= EVENT_HANGUP;
	if (ep->events & EPOLLERR)
		mask |= EVENT_ERROR;

	return fd_source->func(fd_source->fd, mask, source->data);
}

struct event_source_interface fd_source_interface = {
	event_source_fd_dispatch,
};

static struct event_source *
add_source(struct event_loop *loop,
	   struct event_source *source, uint32_t mask, void *data)
{
	struct epoll_event ep;

	if (source->fd < 0) {
		free(source);
		return NULL;
	}

	source->loop = loop;
	source->data = data;
	INIT_LIST_HEAD(&source->link);

	memset(&ep, 0, sizeof ep);
	if (mask & EVENT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & EVENT_WRITABLE)
		ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, &ep) < 0) {
		close(source->fd);
		free(source);
		return NULL;
	}

	return source;
}

static int set_cloexec_or_close(int fd)
{
	long flags;

	if (fd == -1)
		return -1;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		goto err;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
		goto err;

	return fd;

err:
	close(fd);
	return -1;
}

int os_dupfd_cloexec(int fd, long minfd)
{
	int newfd;

	newfd = fcntl(fd, F_DUPFD_CLOEXEC, minfd);
	if (newfd >= 0)
		return newfd;
	if (errno != EINVAL)
		return -1;

	newfd = fcntl(fd, F_DUPFD, minfd);
	return set_cloexec_or_close(newfd);
}

struct event_source *
event_loop_add_fd(struct event_loop *loop,
		     int fd, int nodupfd, uint32_t mask,
		     event_loop_fd_func_t func,
		     void *data)
{
	struct event_source_fd *source;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &fd_source_interface;
	if (!nodupfd)
		source->base.fd = os_dupfd_cloexec(fd, 0);
	else
		source->base.fd = fd;
	source->func = func;
	source->fd = fd;

	return add_source(loop, &source->base, mask, data);
}

int
event_source_fd_update(struct event_source *source, uint32_t mask)
{
	struct event_loop *loop = source->loop;
	struct epoll_event ep;

	memset(&ep, 0, sizeof ep);
	if (mask & EVENT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & EVENT_WRITABLE)
		ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	return epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, source->fd, &ep);
}

struct event_source_timer {
	struct event_source base;
	event_loop_timer_func_t func;
};

static int
event_source_timer_dispatch(struct event_source *source,
			       struct epoll_event *ep)
{
	struct event_source_timer *timer_source =
		(struct event_source_timer *) source;
	uint64_t expires;
	int len;

	len = read(source->fd, &expires, sizeof expires);
	if (!(len == -1 && errno == EAGAIN) && len != sizeof expires)
		/* Is there anything we can do here?  Will this ever happen? */
		printf("timerfd read error: %s\n", strerror(errno));

	return timer_source->func(timer_source->base.data);
}

struct event_source_interface timer_source_interface = {
	event_source_timer_dispatch,
};

struct event_source *
event_loop_add_timer(struct event_loop *loop,
			event_loop_timer_func_t func,
			void *data)
{
	struct event_source_timer *source;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &timer_source_interface;
	source->base.fd = timerfd_create(CLOCK_MONOTONIC,
					 TFD_CLOEXEC | TFD_NONBLOCK);
	source->func = func;

	return add_source(loop, &source->base, EVENT_READABLE, data);
}

int
event_source_timer_update(struct event_source *source, int ms_delay)
{
	struct itimerspec its;

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = ms_delay / 1000;
	its.it_value.tv_nsec = (ms_delay % 1000) * 1000 * 1000;
	if (timerfd_settime(source->fd, 0, &its, NULL) < 0)
		return -1;

	return 0;
}

struct event_source_signal {
	struct event_source base;
	int signal_number;
	event_loop_signal_func_t func;
};

static int
event_source_signal_dispatch(struct event_source *source,
				struct epoll_event *ep)
{
	struct event_source_signal *signal_source =
		(struct event_source_signal *) source;
	struct signalfd_siginfo signal_info;
	int len;

	len = read(source->fd, &signal_info, sizeof signal_info);
	if (!(len == -1 && errno == EAGAIN) && len != sizeof signal_info)
		/* Is there anything we can do here?  Will this ever happen? */
		printf("signalfd read error: %s\n", strerror(errno));

	return signal_source->func(signal_source->signal_number,
				   signal_source->base.data);
}

struct event_source_interface signal_source_interface = {
	event_source_signal_dispatch,
};

struct event_source *
event_loop_add_signal(struct event_loop *loop,
			 int signal_number,
			 event_loop_signal_func_t func,
			 void *data)
{
	struct event_source_signal *source;
	sigset_t mask;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &signal_source_interface;
	source->signal_number = signal_number;

	sigemptyset(&mask);
	sigaddset(&mask, signal_number);
	source->base.fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	source->func = func;

	return add_source(loop, &source->base, EVENT_READABLE, data);
}

struct event_source_idle {
	struct event_source base;
	event_loop_idle_func_t func;
};

struct event_source_interface idle_source_interface = {
	NULL,
};

struct event_source *
event_loop_add_idle(struct event_loop *loop,
		       event_loop_idle_func_t func,
		       void *data)
{
	struct event_source_idle *source;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &idle_source_interface;
	source->base.loop = loop;
	source->base.fd = -1;

	source->func = func;
	source->base.data = data;

	list_add_tail(&source->base.link, &loop->idle_list);

	return &source->base;
}

void
event_source_check(struct event_source *source)
{
	list_add_tail(&source->link, &source->loop->check_list);
}

int
event_source_remove(struct event_source *source)
{
	struct event_loop *loop = source->loop;

	/* We need to explicitly remove the fd, since closing the fd
	 * isn't enough in case we've dup'ed the fd. */
	if (source->fd >= 0) {
		epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, source->fd, NULL);
		if (source->interface == &fd_source_interface) {
			struct event_source_fd *fd_source;

			fd_source = (struct event_source_fd *)source;
			if (fd_source->fd != source->fd) {
				close(source->fd);
				source->fd = -1;
			}
		} else {
			close(source->fd);
			source->fd = -1;
		}
	}

	list_del(&source->link);
	list_add_tail(&source->link, &loop->destroy_list);

	return 0;
}

static void
event_loop_process_destroy_list(struct event_loop *loop)
{
	struct event_source *source, *next;

	list_for_each_entry_safe(source, next, &loop->destroy_list, link)
		free(source);

	INIT_LIST_HEAD(&loop->destroy_list);
}

int os_epoll_create_cloexec(void)
{
	int fd;

#ifdef EPOLL_CLOEXEC
	fd = epoll_create1(EPOLL_CLOEXEC);
	if (fd >= 0)
		return fd;
	if (errno != EINVAL)
		return -1;
#endif

	fd = epoll_create(1);
	return set_cloexec_or_close(fd);
}

struct event_loop *
event_loop_create(void)
{
	struct event_loop *loop;

	loop = malloc(sizeof *loop);
	if (loop == NULL)
		return NULL;

	loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC); // os_epoll_create_cloexec();
	if (loop->epoll_fd < 0) {
		free(loop);
		return NULL;
	}
	INIT_LIST_HEAD(&loop->check_list);
	INIT_LIST_HEAD(&loop->idle_list);
	INIT_LIST_HEAD(&loop->destroy_list);

	return loop;
}

void
event_loop_destroy(struct event_loop *loop)
{
	event_loop_process_destroy_list(loop);
	close(loop->epoll_fd);
	free(loop);
}

static bool
post_dispatch_check(struct event_loop *loop)
{
	struct epoll_event ep;
	struct event_source *source, *next;
	bool needs_recheck = false;

	ep.events = 0;
	list_for_each_entry_safe(source, next, &loop->check_list, link) {
		int dispatch_result;

		dispatch_result = source->interface->dispatch(source, &ep);
		if (dispatch_result < 0) {
			printf("Source dispatch function returned negative value!");
			printf("This would previously accidentally suppress a follow-up dispatch");
		}
		needs_recheck |= dispatch_result != 0;
	}

	return needs_recheck;
}

void
event_loop_dispatch_idle(struct event_loop *loop)
{
	struct event_source_idle *source;

	while (!list_empty(&loop->idle_list)) {
		source = container_of(loop->idle_list.next,
				      struct event_source_idle, base.link);
		source->func(source->base.data);
		event_source_remove(&source->base);
	}
}

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(arr)	(sizeof(arr) / sizeof(arr[0]))
#endif

int
event_loop_dispatch(struct event_loop *loop, int timeout)
{
	struct epoll_event ep[32];
	struct event_source *source;
	int i, count;

	event_loop_dispatch_idle(loop);

	count = epoll_wait(loop->epoll_fd, ep, ARRAY_LENGTH(ep), timeout);
	if (count < 0)
		return -1;

	for (i = 0; i < count; i++) {
		source = ep[i].data.ptr;
		// printf("recv fd %d, c %d\n", source->fd, count);
		if (source->fd != -1)
			source->interface->dispatch(source, &ep[i]);
	}

	event_loop_process_destroy_list(loop);

	event_loop_dispatch_idle(loop);

	while (post_dispatch_check(loop));

	return 0;
}

int
event_loop_get_fd(struct event_loop *loop)
{
	return loop->epoll_fd;
}
