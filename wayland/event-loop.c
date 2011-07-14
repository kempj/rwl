/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <assert.h>
#include "wayland-server.h"

struct wl_event_loop {
	int epoll_fd;
	struct wl_list check_list;
	struct wl_list idle_list;
};

struct wl_event_source_interface {
	int (*dispatch)(struct wl_event_source *source,
			struct epoll_event *ep);
	int (*remove)(struct wl_event_source *source);
};

struct wl_event_source {
	struct wl_event_source_interface *interface;
	struct wl_event_loop *loop;
	struct wl_list link;
	void *data;
};

struct wl_event_source_fd {
	struct wl_event_source base;
	int fd;
	wl_event_loop_fd_func_t func;
};

static int
wl_event_source_fd_dispatch(struct wl_event_source *source,
			    struct epoll_event *ep)
{
	struct wl_event_source_fd *fd_source = (struct wl_event_source_fd *) source;
	uint32_t mask;

	mask = 0;
	if (ep->events & EPOLLIN)
		mask |= WL_EVENT_READABLE;
	if (ep->events & EPOLLOUT)
		printf("You shouldn't see me4\n");//mask |= WL_EVENT_WRITEABLE;

	return fd_source->func(fd_source->fd, mask, fd_source->base.data);
}

static int
wl_event_source_fd_remove(struct wl_event_source *source)
{
	struct wl_event_source_fd *fd_source =
		(struct wl_event_source_fd *) source;
	struct wl_event_loop *loop = source->loop;
	int fd;

	fd = fd_source->fd;
	free(source);

	return epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

struct wl_event_source_interface fd_source_interface = {
	wl_event_source_fd_dispatch,
	wl_event_source_fd_remove
};

WL_EXPORT struct wl_event_source *
rwl_event_loop_add_fd(struct wl_event_loop *loop,
		     int read_fd, int write_fd, uint32_t mask,
		     wl_event_loop_fd_func_t func,
		     void *data)
{
	struct wl_event_source_fd *source;
	struct epoll_event ep;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &fd_source_interface;
	source->base.loop = loop;
	wl_list_init(&source->base.link);
	source->fd = write_fd;
	source->func = func;
	source->base.data = data;

	memset(&ep, 0, sizeof ep);
	if (mask & WL_EVENT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & WL_EVENT_WRITEABLE)
		printf("You shouldn't see me1\n");//ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, read_fd, &ep) < 0) {
		free(source);
		return NULL;
	}

	return &source->base;
}

WL_EXPORT struct wl_event_source *
wl_event_loop_add_fd(struct wl_event_loop *loop,
		     int fd, uint32_t mask,
		     wl_event_loop_fd_func_t func,
		     void *data)
{
	struct wl_event_source_fd *source;
	struct epoll_event ep;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &fd_source_interface;
	source->base.loop = loop;
	wl_list_init(&source->base.link);
	source->fd = fd;
	source->func = func;
	source->base.data = data;

	memset(&ep, 0, sizeof ep);
	if (mask & WL_EVENT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & WL_EVENT_WRITEABLE)
		printf("You shouldn't see me2\n");//ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &ep) < 0) {
		free(source);
		return NULL;
	}

	return &source->base;
}

WL_EXPORT int
wl_event_source_fd_update(struct wl_event_source *source, uint32_t mask)
{
	struct wl_event_source_fd *fd_source =
		(struct wl_event_source_fd *) source;
	struct wl_event_loop *loop = source->loop;
	struct epoll_event ep;

	memset(&ep, 0, sizeof ep);
	if (mask & WL_EVENT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & WL_EVENT_WRITEABLE)
		printf("You shouldn't see me3\n");//ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	return epoll_ctl(loop->epoll_fd,
			 EPOLL_CTL_MOD, fd_source->fd, &ep);
}

struct wl_event_source_timer {
	struct wl_event_source base;
	int fd;
	wl_event_loop_timer_func_t func;
};

static int
wl_event_source_timer_dispatch(struct wl_event_source *source,
			       struct epoll_event *ep)
{
	struct wl_event_source_timer *timer_source =
		(struct wl_event_source_timer *) source;
	uint64_t expires;

	read(timer_source->fd, &expires, sizeof expires);

	return timer_source->func(timer_source->base.data);
}

static int
wl_event_source_timer_remove(struct wl_event_source *source)
{
	struct wl_event_source_timer *timer_source =
		(struct wl_event_source_timer *) source;

	close(timer_source->fd);
	free(source);
	return 0;
}

struct wl_event_source_interface timer_source_interface = {
	wl_event_source_timer_dispatch,
	wl_event_source_timer_remove
};

WL_EXPORT struct wl_event_source *
wl_event_loop_add_timer(struct wl_event_loop *loop,
			wl_event_loop_timer_func_t func,
			void *data)
{
	struct wl_event_source_timer *source;
	struct epoll_event ep;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &timer_source_interface;
	source->base.loop = loop;
	wl_list_init(&source->base.link);

	source->fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	if (source->fd < 0) {
		fprintf(stderr, "could not create timerfd\n: %m");
		free(source);
		return NULL;
	}

	source->func = func;
	source->base.data = data;

	memset(&ep, 0, sizeof ep);
	ep.events = EPOLLIN;
	ep.data.ptr = source;

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, &ep) < 0) {
		close(source->fd);
		free(source);
		return NULL;
	}

	return &source->base;
}

WL_EXPORT int
wl_event_source_timer_update(struct wl_event_source *source, int ms_delay)
{
	struct wl_event_source_timer *timer_source =
		(struct wl_event_source_timer *) source;
	struct itimerspec its;

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = ms_delay / 1000;
	its.it_value.tv_nsec = (ms_delay % 1000) * 1000 * 1000;
	if (timerfd_settime(timer_source->fd, 0, &its, NULL) < 0) {
		fprintf(stderr, "could not set timerfd\n: %m");
		return -1;
	}

	return 0;
}

struct wl_event_source_signal {
	struct wl_event_source base;
	int fd;
	int signal_number;
	wl_event_loop_signal_func_t func;
};

static int
wl_event_source_signal_dispatch(struct wl_event_source *source,
			       struct epoll_event *ep)
{
	struct wl_event_source_signal *signal_source =
		(struct wl_event_source_signal *) source;
	struct signalfd_siginfo signal_info;

	read(signal_source->fd, &signal_info, sizeof signal_info);

	return signal_source->func(signal_source->signal_number,
				   signal_source->base.data);
}

static int
wl_event_source_signal_remove(struct wl_event_source *source)
{
	struct wl_event_source_signal *signal_source =
		(struct wl_event_source_signal *) source;

	close(signal_source->fd);
	free(source);
	return 0;
}

struct wl_event_source_interface signal_source_interface = {
	wl_event_source_signal_dispatch,
	wl_event_source_signal_remove
};

WL_EXPORT struct wl_event_source *
wl_event_loop_add_signal(struct wl_event_loop *loop,
			int signal_number,
			wl_event_loop_signal_func_t func,
			void *data)
{
	struct wl_event_source_signal *source;
	struct epoll_event ep;
	sigset_t mask;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &signal_source_interface;
	source->base.loop = loop;
	wl_list_init(&source->base.link);
	source->signal_number = signal_number;

	sigemptyset(&mask);
	sigaddset(&mask, signal_number);
	source->fd = signalfd(-1, &mask, SFD_CLOEXEC);
	if (source->fd < 0) {
		fprintf(stderr, "could not create fd to watch signal\n: %m");
		free(source);
		return NULL;
	}
	sigprocmask(SIG_BLOCK, &mask, NULL);

	source->func = func;
	source->base.data = data;

	memset(&ep, 0, sizeof ep);
	ep.events = EPOLLIN;
	ep.data.ptr = source;

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, &ep) < 0) {
		close(source->fd);
		free(source);
		return NULL;
	}

	return &source->base;
}

struct wl_event_source_idle {
	struct wl_event_source base;
	wl_event_loop_idle_func_t func;
};

static int
wl_event_source_idle_remove(struct wl_event_source *source)
{
	free(source);

	return 0;
}

struct wl_event_source_interface idle_source_interface = {
	NULL,
	wl_event_source_idle_remove
};

WL_EXPORT struct wl_event_source *
wl_event_loop_add_idle(struct wl_event_loop *loop,
		       wl_event_loop_idle_func_t func,
		       void *data)
{
	struct wl_event_source_idle *source;

	source = malloc(sizeof *source);
	if (source == NULL)
		return NULL;

	source->base.interface = &idle_source_interface;
	source->base.loop = loop;

	source->func = func;
	source->base.data = data;

	wl_list_insert(loop->idle_list.prev, &source->base.link);

	return &source->base;
}

WL_EXPORT void
wl_event_source_check(struct wl_event_source *source)
{
	wl_list_insert(source->loop->check_list.prev, &source->link);
}

WL_EXPORT int
wl_event_source_remove(struct wl_event_source *source)
{
	if (!wl_list_empty(&source->link))
		wl_list_remove(&source->link);

	source->interface->remove(source);

	return 0;
}

WL_EXPORT struct wl_event_loop *
wl_event_loop_create(void)
{
	struct wl_event_loop *loop;

	loop = malloc(sizeof *loop);
	if (loop == NULL)
		return NULL;

	loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (loop->epoll_fd < 0) {
		free(loop);
		return NULL;
	}
	wl_list_init(&loop->check_list);
	wl_list_init(&loop->idle_list);

	return loop;
}

WL_EXPORT void
wl_event_loop_destroy(struct wl_event_loop *loop)
{
	close(loop->epoll_fd);
	free(loop);
}

static int
post_dispatch_check(struct wl_event_loop *loop)
{
	struct epoll_event ep;
	struct wl_event_source *source, *next;
	int n;

	ep.events = 0;
	n = 0;
	wl_list_for_each_safe(source, next, &loop->check_list, link)
		n += source->interface->dispatch(source, &ep);

	return n;
}

static void
dispatch_idle_sources(struct wl_event_loop *loop)
{
	struct wl_event_source_idle *source, *next;

	wl_list_for_each_safe(source, next, &loop->idle_list, base.link) {
		source->func(source->base.data);
		wl_event_source_remove(&source->base);
	}
}

WL_EXPORT int
wl_event_loop_dispatch(struct wl_event_loop *loop, int timeout)
{
	struct epoll_event ep[32];
	struct wl_event_source *source;
	int i, count, n;

	dispatch_idle_sources(loop);
	printf("event-loop dispatched ");//jkdebug
	count = epoll_wait(loop->epoll_fd, ep, ARRAY_LENGTH(ep), timeout);
	if (count < 0)
		return -1;
	n = 0;
	for (i = 0; i < count; i++) {
		source = ep[i].data.ptr;
		n += source->interface->dispatch(source, &ep[i]);
	}

	while (n > 0)
		n = post_dispatch_check(loop);
		
	return 0;
}

WL_EXPORT int
wl_event_loop_get_fd(struct wl_event_loop *loop)
{
	return loop->epoll_fd;
}
