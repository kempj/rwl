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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dlfcn.h>
#include <assert.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <ffi.h>

//JK
#include <netdb.h>

#include "wayland-server.h"
#include "wayland-server-protocol.h"
#include "connection.h"

struct wl_socket {
	int fd;
	int fd_lock;
	struct sockaddr_un addr;
	char lock_addr[113];
	struct wl_list link;
};

struct wl_client {
	struct wl_connection *connection;
	struct wl_event_source *source;
	struct wl_display *display;
	struct wl_list resource_list;
	uint32_t id_count;
	uint32_t mask;
	struct wl_list link;
};

struct wl_display {
	struct wl_object object;
	struct wl_event_loop *loop;
	struct wl_hash_table *objects;
	int run;

	struct wl_list frame_list;
	uint32_t client_id_range;
	uint32_t id;

	struct wl_list global_list;
	struct wl_list socket_list;
	struct wl_list client_list;
};

struct wl_frame_listener {
	struct wl_resource resource;
	struct wl_client *client;
	uint32_t key;
	struct wl_surface *surface;
	struct wl_list link;
};

struct wl_global {
	struct wl_object *object;
	wl_global_bind_func_t func;
	struct wl_list link;
};

static int wl_debug = 0;

WL_EXPORT void
wl_client_post_event(struct wl_client *client, struct wl_object *sender,
		     uint32_t opcode, ...)
{
	struct wl_closure *closure;
	va_list ap;

	va_start(ap, opcode);
	closure = wl_connection_vmarshal(client->connection,
					 sender, opcode, ap,
					 &sender->interface->events[opcode]);
	va_end(ap);

	wl_closure_send(closure, client->connection);

	if (wl_debug) {
		fprintf(stderr, " -> ");
		wl_closure_print(closure, sender);
	}

	wl_closure_destroy(closure);
}

WL_EXPORT void
wl_client_post_error(struct wl_client *client, struct wl_object *object,
		     uint32_t code, const char *msg, ...)
{
	char buffer[128];
	va_list ap;

	va_start(ap, msg);
	vsnprintf(buffer, sizeof buffer, msg, ap);
	va_end(ap);

	wl_client_post_event(client, &client->display->object,
			     WL_DISPLAY_ERROR, object, code, buffer);
}

static int
wl_client_connection_data(int fd, uint32_t mask, void *data)
{
	struct wl_client *client = data;
	struct wl_connection *connection = client->connection;
	//struct wl_object *object;
	//struct wl_closure *closure;
	//const struct wl_message *message;
	//uint32_t p[2], opcode, size;
	uint32_t cmask = 0;
	int len, temp_fd = fd;

	if (mask & WL_EVENT_READABLE)
	{
		printf("readable");
		cmask |= WL_CONNECTION_READABLE;
	}
	if (mask & WL_EVENT_WRITEABLE)
	{
		printf("writable");//cmask |= WL_CONNECTION_WRITABLE;
		//return 0;
	}
	printf("wccd\n");
	// read in
	printf("wccd 1\n");
	//TODO:do I need to use this len for error checking?
	len = wl_connection_data(connection, WL_CONNECTION_READABLE);
	
	//switch
	rwl_fd_switch(connection, fd);
	printf("wccd 2\n");
	//write out
	len = wl_connection_data(connection, WL_CONNECTION_WRITABLE);
	if (len < 0) {
		wl_client_destroy(client);
		return 1;
	}
	
	rwl_fd_switch(connection, temp_fd);
	printf("wccd 3\n");

/*
	while (len >= sizeof p) {
		wl_connection_copy(connection, p, sizeof p);
		opcode = p[1] & 0xffff;
		size = p[1] >> 16;
		if (len < size)
			break;

		object = wl_hash_table_lookup(client->display->objects, p[0]);
		if (object == NULL) {
			wl_client_post_error(client, &client->display->object,
					     WL_DISPLAY_ERROR_INVALID_OBJECT,
					     "invalid object %d", p[0]);
			wl_connection_consume(connection, size);
			len -= size;
			continue;
		}

		if (opcode >= object->interface->method_count) {
			wl_client_post_error(client, &client->display->object,
					     WL_DISPLAY_ERROR_INVALID_METHOD,
					     "invalid method %d, object %s@%d",
					     object->interface->name,
					     object->id, opcode);
			wl_connection_consume(connection, size);
			len -= size;
			continue;
		}

		message = &object->interface->methods[opcode];
		closure = wl_connection_demarshal(client->connection, size,
						  client->display->objects,
						  message);
		len -= size;

		if (closure == NULL && errno == EINVAL) {
			wl_client_post_error(client, &client->display->object,
					     WL_DISPLAY_ERROR_INVALID_METHOD,
					     "invalid arguments for %s@%d.%s",
					     object->interface->name,
					     object->id, message->name);
			continue;
		} else if (closure == NULL && errno == ENOMEM) {
			wl_client_post_no_memory(client);
			continue;
		}


		if (wl_debug)
			wl_closure_print(closure, object);

		wl_closure_invoke(closure, object,
				  object->implementation[opcode], client);

		wl_closure_destroy(closure);
	}*/

	return 1;
}

static int
wl_client_connection_update(struct wl_connection *connection,
			    uint32_t mask, void *data)
{
	struct wl_client *client = data;
	uint32_t emask = 0;

	client->mask = mask;
	if (mask & WL_CONNECTION_READABLE)
		emask |= WL_EVENT_READABLE;
	if (mask & WL_CONNECTION_WRITABLE)
		emask |= WL_EVENT_WRITEABLE;

	return wl_event_source_fd_update(client->source, emask);
}

WL_EXPORT void
wl_client_flush(struct wl_client *client)
{
	if (client->mask & WL_CONNECTION_WRITABLE)
		wl_connection_data(client->connection, WL_CONNECTION_WRITABLE);
}

WL_EXPORT struct wl_display *
wl_client_get_display(struct wl_client *client)
{
	return client->display;
}

static void
wl_display_post_range(struct wl_display *display, struct wl_client *client)
{
	wl_client_post_event(client, &client->display->object,
			     WL_DISPLAY_RANGE, display->client_id_range);
	display->client_id_range += 256;
	client->id_count += 256;
}

WL_EXPORT int
rwl_forward_create(struct wl_display *display, int client_fd)
	//how am I going to get the remote address in here?
{
	printf("entering rwl_forward_create\n");
	struct wl_client *client;
	struct addrinfo *remote, *result;
	int err, remote_fd;

	//open connection with remote host
	
	if((err = getaddrinfo("127.0.0.1", "35000", NULL, &remote)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(err));
		return EXIT_FAILURE;
	}

	for(result = remote;result != NULL;result = result->ai_next){
		if((remote_fd = socket(result->ai_family, result->ai_socktype,
				result->ai_protocol)) == -1){
			perror("client: socket");
			continue;
		}

		if (connect(remote_fd, result->ai_addr, result->ai_addrlen) == -1) {
		close(remote_fd);
		perror("client: connect");
		continue;
        	}

		break;
	}	
	//pack the client_fd into the source
	client = malloc(sizeof *client);
	memset(client, 0, sizeof *client);	
	client->display = display;
	client-> source = rwl_event_loop_add_fd(display->loop, remote_fd, client_fd,
					      WL_EVENT_READABLE,
					      wl_client_connection_data, client);
					      //rwl_forward_to_remote, client);
	client->connection = 
		wl_connection_create(remote_fd, wl_client_connection_update, client);//not sure

	return remote_fd;
}


WL_EXPORT struct wl_client *
wl_client_create(struct wl_display *display, int local_fd)
{

	printf("entering wl_cleint_create\n");
        struct wl_client *client;
        struct wl_global *global;
        int remote_fd;

        client = malloc(sizeof *client);
        if (client == NULL)
                return NULL;

        //I need the remote_fd because the fds for the remote and client are 
        //being swapped. Anything that comes in on the local fd, reads from the
        //local fd, then writes to the remote fd. And the inverse for writes 
        //to the remote fd.
        remote_fd = rwl_forward_create(display, local_fd);

        memset(client, 0, sizeof *client);
        client->display = display;
        client->source = rwl_event_loop_add_fd(display->loop, local_fd, remote_fd,
                                              WL_EVENT_READABLE,
                                              wl_client_connection_data, client);
        client->connection =
                wl_connection_create(local_fd, wl_client_connection_update, client);
        if (client->connection == NULL) {
                free(client);
                return NULL;
        }

        wl_list_insert(display->client_list.prev, &client->link);

        wl_list_init(&client->resource_list);

        wl_display_post_range(display, client);

        wl_list_for_each(global, &display->global_list, link)
                wl_client_post_global(client, global->object);

	wl_client_connection_data(local_fd, WL_EVENT_READABLE, client);

	printf("leaving wl_cleint_create\n");

        return client;

}

WL_EXPORT void
wl_client_add_resource(struct wl_client *client,
		       struct wl_resource *resource)
{
	struct wl_display *display = client->display;

	if (client->id_count-- < 64)
		wl_display_post_range(display, client);

	wl_list_init(&resource->destroy_listener_list);

	wl_hash_table_insert(client->display->objects,
			     resource->object.id, resource);
	wl_list_insert(client->resource_list.prev, &resource->link);
}

WL_EXPORT void
wl_client_post_no_memory(struct wl_client *client)
{
	wl_client_post_error(client, &client->display->object,
			     WL_DISPLAY_ERROR_NO_MEMORY, "no memory");
}

WL_EXPORT void
wl_client_post_global(struct wl_client *client, struct wl_object *object)
{
	wl_client_post_event(client,
			     &client->display->object,
			     WL_DISPLAY_GLOBAL,
			     object,
			     object->interface->name,
			     object->interface->version);
}

WL_EXPORT void
wl_resource_destroy(struct wl_resource *resource,
		    struct wl_client *client, uint32_t time)
{
	struct wl_display *display = client->display;
	struct wl_listener *l, *next;

	wl_list_for_each_safe(l, next,
			      &resource->destroy_listener_list, link)
		l->func(l, resource, time);

	wl_list_remove(&resource->link);
	if (resource->object.id > 0)
		wl_hash_table_remove(display->objects, resource->object.id);
	resource->destroy(resource, client);
}

WL_EXPORT void
wl_client_destroy(struct wl_client *client)
{
	struct wl_resource *resource, *tmp;

	printf("disconnect from client %p\n", client);

	wl_list_for_each_safe(resource, tmp, &client->resource_list, link)
		wl_resource_destroy(resource, client, 0);

	wl_event_source_remove(client->source);
	wl_connection_destroy(client->connection);
	wl_list_remove(&client->link);
	free(client);
}

static void
lose_pointer_focus(struct wl_listener *listener,
		   struct wl_resource *resource, uint32_t time)
{
	struct wl_input_device *device =
		container_of(listener, struct wl_input_device,
			     pointer_focus_listener);

	wl_input_device_set_pointer_focus(device, NULL, time, 0, 0, 0, 0);
}

static void
lose_keyboard_focus(struct wl_listener *listener,
		    struct wl_resource *resource, uint32_t time)
{
	struct wl_input_device *device =
		container_of(listener, struct wl_input_device,
			     keyboard_focus_listener);

	wl_input_device_set_keyboard_focus(device, NULL, time);
}

WL_EXPORT void
wl_input_device_init(struct wl_input_device *device,
		     struct wl_compositor *compositor)
{
	wl_list_init(&device->pointer_focus_listener.link);
	device->pointer_focus_listener.func = lose_pointer_focus;
	wl_list_init(&device->keyboard_focus_listener.link);
	device->keyboard_focus_listener.func = lose_keyboard_focus;

	device->x = 100;
	device->y = 100;
	device->compositor = compositor;
}

WL_EXPORT void
wl_input_device_set_pointer_focus(struct wl_input_device *device,
				  struct wl_surface *surface,
				  uint32_t time,
				  int32_t x, int32_t y,
				  int32_t sx, int32_t sy)
{
	if (device->pointer_focus == surface)
		return;

	if (device->pointer_focus &&
	    (!surface || device->pointer_focus->client != surface->client))
		wl_client_post_event(device->pointer_focus->client,
				     &device->object,
				     WL_INPUT_DEVICE_POINTER_FOCUS,
				     time, NULL, 0, 0, 0, 0);
	if (device->pointer_focus)
		wl_list_remove(&device->pointer_focus_listener.link);

	if (surface) {
		wl_client_post_event(surface->client,
				     &device->object,
				     WL_INPUT_DEVICE_POINTER_FOCUS,
				     time, surface, x, y, sx, sy);
		wl_list_insert(surface->resource.destroy_listener_list.prev,
			       &device->pointer_focus_listener.link);
	}

	device->pointer_focus = surface;
	device->pointer_focus_time = time;

}

WL_EXPORT void
wl_input_device_set_keyboard_focus(struct wl_input_device *device,
				   struct wl_surface *surface,
				   uint32_t time)
{
	if (device->keyboard_focus == surface)
		return;

	if (device->keyboard_focus &&
	    (!surface || device->keyboard_focus->client != surface->client))
		wl_client_post_event(device->keyboard_focus->client,
				     &device->object,
				     WL_INPUT_DEVICE_KEYBOARD_FOCUS,
				     time, NULL, &device->keys);
	if (device->keyboard_focus)
		wl_list_remove(&device->keyboard_focus_listener.link);

	if (surface) {
		wl_client_post_event(surface->client,
				     &device->object,
				     WL_INPUT_DEVICE_KEYBOARD_FOCUS,
				     time, surface, &device->keys);
		wl_list_insert(surface->resource.destroy_listener_list.prev,
			       &device->keyboard_focus_listener.link);
	}

	device->keyboard_focus = surface;
	device->keyboard_focus_time = time;
}

WL_EXPORT void
wl_input_device_end_grab(struct wl_input_device *device, uint32_t time)
{
	const struct wl_grab_interface *interface;

	interface = device->grab->interface;
	interface->end(device->grab, time);
	device->grab->input_device = NULL;
	device->grab = NULL;

	wl_list_remove(&device->grab_listener.link);
}

static void
lose_grab_surface(struct wl_listener *listener,
		  struct wl_resource *resource, uint32_t time)
{
	struct wl_input_device *device =
		container_of(listener,
			     struct wl_input_device, grab_listener);

	wl_input_device_end_grab(device, time);
}

WL_EXPORT void
wl_input_device_start_grab(struct wl_input_device *device,
			   struct wl_grab *grab,
			   uint32_t button, uint32_t time)
{
	struct wl_surface *focus = device->pointer_focus;

	device->grab = grab;
	device->grab_button = button;
	device->grab_time = time;
	device->grab_x = device->x;
	device->grab_y = device->y;

	device->grab_listener.func = lose_grab_surface;
	wl_list_insert(focus->resource.destroy_listener_list.prev,
		       &device->grab_listener.link);

	grab->input_device = device;
}

WL_EXPORT int
wl_input_device_update_grab(struct wl_input_device *device,
			    struct wl_grab *grab,
			    struct wl_surface *surface, uint32_t time)
{
	if (device->grab != &device->motion_grab ||
	    device->grab_time != time ||
	    device->pointer_focus != surface)
		return -1;

	device->grab = grab;
	grab->input_device = device;

	return 0;
}

static void
display_bind(struct wl_client *client,
	     struct wl_display *display, uint32_t id,
	     const char *interface, uint32_t version)
{
	struct wl_global *global;

	wl_list_for_each(global, &display->global_list, link)
		if (global->object->id == id)
			break;

	if (&global->link == &display->global_list)
		wl_client_post_error(client, &client->display->object,
				     WL_DISPLAY_ERROR_INVALID_OBJECT,
				     "invalid object %d", id);
	else if (global->func)
		global->func(client, global->object, version);
}

static void
display_sync(struct wl_client *client,
	       struct wl_display *display, uint32_t key)
{
	wl_client_post_event(client, &display->object, WL_DISPLAY_KEY, key, 0);
}

static void
destroy_frame_listener(struct wl_resource *resource, struct wl_client *client)
{
	struct wl_frame_listener *listener =
		container_of(resource, struct wl_frame_listener, resource);

	wl_list_remove(&listener->link);
	free(listener);
}

static void
display_frame(struct wl_client *client,
	      struct wl_display *display,
	      struct wl_surface *surface,
	      uint32_t key)
{
	struct wl_frame_listener *listener;

	listener = malloc(sizeof *listener);
	if (listener == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	/* The listener is a resource so we destroy it when the client
	 * goes away. */
	listener->resource.destroy = destroy_frame_listener;
	listener->resource.object.id = 0;
	listener->client = client;
	listener->key = key;
	listener->surface = surface;
	wl_list_init(&listener->resource.destroy_listener_list);
	wl_list_insert(client->resource_list.prev, &listener->resource.link);
	wl_list_insert(display->frame_list.prev, &listener->link);
}

struct wl_display_interface display_interface = {
	display_bind,
	display_sync,
	display_frame
};


WL_EXPORT struct wl_display *
wl_display_create(void)
{
	struct wl_display *display;
	const char *debug;

	debug = getenv("WAYLAND_DEBUG");
	if (debug)
		wl_debug = 1;

	display = malloc(sizeof *display);
	if (display == NULL)
		return NULL;

	display->loop = wl_event_loop_create();
	if (display->loop == NULL) {
		free(display);
		return NULL;
	}

	display->objects = wl_hash_table_create();
	if (display->objects == NULL) {
		wl_event_loop_destroy(display->loop);
		free(display);
		return NULL;
	}

	wl_list_init(&display->frame_list);
	wl_list_init(&display->global_list);
	wl_list_init(&display->socket_list);
	wl_list_init(&display->client_list);

	display->client_id_range = 256; /* Gah, arbitrary... */

	display->id = 1;
	display->object.interface = &wl_display_interface;
	display->object.implementation = (void (**)(void)) &display_interface;
	wl_display_add_object(display, &display->object);
	if (wl_display_add_global(display, &display->object, NULL)) {
		wl_hash_table_destroy(display->objects);
		wl_event_loop_destroy(display->loop);
		free(display);
		return NULL;
	}

	return display;
}

WL_EXPORT void
wl_display_destroy(struct wl_display *display)
{
	struct wl_socket *s, *next;
	struct wl_global *global, *gnext;

  	wl_event_loop_destroy(display->loop);
 	wl_hash_table_destroy(display->objects);
	wl_list_for_each_safe(s, next, &display->socket_list, link) {
		close(s->fd);
		unlink(s->addr.sun_path);
		close(s->fd_lock);
		unlink(s->lock_addr);
		free(s);
	}

	wl_list_for_each_safe(global, gnext, &display->global_list, link)
		free(global);

	free(display);
}

WL_EXPORT void
wl_display_add_object(struct wl_display *display, struct wl_object *object)
{
	object->id = display->id++;
	wl_hash_table_insert(display->objects, object->id, object);
}

WL_EXPORT int
wl_display_add_global(struct wl_display *display,
		      struct wl_object *object, wl_global_bind_func_t func)
{
	struct wl_global *global;

	global = malloc(sizeof *global);
	if (global == NULL)
		return -1;

	global->object = object;
	global->func = func;
	wl_list_insert(display->global_list.prev, &global->link);

	return 0;
}

WL_EXPORT int
wl_display_remove_global(struct wl_display *display,
                         struct wl_object *object)
{
	struct wl_global *global;
	struct wl_client *client;

	wl_list_for_each(global, &display->global_list, link)
		if (global->object == object)
			break;

	if (&global->link == &display->global_list)
		return -1;

	wl_list_for_each(client, &display->client_list, link)
		wl_client_post_event(client,
				     &client->display->object,
				     WL_DISPLAY_GLOBAL_REMOVE,
				     global->object->id);
	wl_list_remove(&global->link);
	free(global);

	return 0;
}

WL_EXPORT void
wl_display_post_frame(struct wl_display *display, struct wl_surface *surface,
		      uint32_t time)
{
	struct wl_frame_listener *listener, *next;

	wl_list_for_each_safe(listener, next, &display->frame_list, link) {
		if (listener->surface != surface)
			continue;
		wl_client_post_event(listener->client, &display->object,
				     WL_DISPLAY_KEY, listener->key, time);
		wl_resource_destroy(&listener->resource, listener->client, 0);
	}
}

WL_EXPORT struct wl_event_loop *
wl_display_get_event_loop(struct wl_display *display)
{
	return display->loop;
}

WL_EXPORT void
wl_display_terminate(struct wl_display *display)
{
	display->run = 0;
}

WL_EXPORT void
wl_display_run(struct wl_display *display)
{
	display->run = 1;

	while (display->run)
		wl_event_loop_dispatch(display->loop, -1);
}

static int
socket_data(int fd, uint32_t mask, void *data)
{
	struct wl_display *display = data;
	struct sockaddr_un name;
	socklen_t length;
	int client_fd;

	length = sizeof name;
	client_fd =
		accept4(fd, (struct sockaddr *) &name, &length, SOCK_CLOEXEC);
	if (client_fd < 0 && errno == ENOSYS) {
		client_fd = accept(fd, (struct sockaddr *) &name, &length);
		if (client_fd >= 0 && fcntl(client_fd, F_SETFD, FD_CLOEXEC) == -1)
			fprintf(stderr, "failed to set FD_CLOEXEC flag on client fd, errno: %d\n", errno);
	}

	if (client_fd < 0)
		fprintf(stderr, "failed to accept, errno: %d\n", errno);

	wl_client_create(display, client_fd);

	return 1;
}

static int
get_socket_lock(struct wl_socket *socket, socklen_t name_size)
{
	struct stat socket_stat;
	int lock_size = name_size + 5;

	snprintf(socket->lock_addr, lock_size,
		 "%s.lock", socket->addr.sun_path);

	socket->fd_lock = open(socket->lock_addr, O_CREAT | O_CLOEXEC,
			       (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));

	if (socket->fd_lock < 0) {
		fprintf(stderr,
			"unable to open lockfile %s check permissions\n",
			socket->lock_addr);
		return -1;
	}

	if (flock(socket->fd_lock, LOCK_EX | LOCK_NB) < 0) {
		fprintf(stderr,
			"unable to lock lockfile %s, maybe another compositor is running\n",
			socket->lock_addr);
		close(socket->fd_lock);
		return -1;
	}

	if (stat(socket->addr.sun_path, &socket_stat) < 0 ) {
		if (errno != ENOENT) {
			fprintf(stderr, "did not manage to stat file %s\n",
				socket->addr.sun_path);
			close(socket->fd_lock);
			return -1;
		}
	} else if (socket_stat.st_mode & S_IWUSR ||
		   socket_stat.st_mode & S_IWGRP) {
		unlink(socket->addr.sun_path);
	}

	return 0;
}

WL_EXPORT int
wl_display_add_socket(struct wl_display *display, const char *name)
{
	struct wl_socket *s;
	socklen_t size, name_size;
	const char *runtime_dir;

	s = malloc(sizeof *s);
	if (s == NULL)
		return -1;

	s->fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (s->fd < 0) {
		free(s);
		return -1;
	}

	runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (runtime_dir == NULL) {
		runtime_dir = ".";
		fprintf(stderr,
			"XDG_RUNTIME_DIR not set, falling back to %s\n",
			runtime_dir);
	}

	if (name == NULL)
		name = getenv("WAYLAND_DISPLAY");
	if (name == NULL)
		name = "wayland-0";

	memset(&s->addr, 0, sizeof s->addr);
	s->addr.sun_family = AF_LOCAL;
	name_size = snprintf(s->addr.sun_path, sizeof s->addr.sun_path,
			     "%s/%s", runtime_dir, name) + 1;
	fprintf(stderr, "using socket %s\n", s->addr.sun_path);

	if (get_socket_lock(s,name_size) < 0) {
		close(s->fd);
		free(s);
		return -1;
	}

	size = offsetof (struct sockaddr_un, sun_path) + name_size;
	if (bind(s->fd, (struct sockaddr *) &s->addr, size) < 0) {
		close(s->fd);
		free(s);
		return -1;
	}

	if (listen(s->fd, 1) < 0) {
		close(s->fd);
		unlink(s->addr.sun_path);
		free(s);
		return -1;
	}

	if (wl_event_loop_add_fd(display->loop, s->fd,
				 WL_EVENT_READABLE,
				 socket_data, display) == NULL) {
		close(s->fd);
		unlink(s->addr.sun_path);
		free(s);
		return -1;
	}
	wl_list_insert(display->socket_list.prev, &s->link);

	return 0;
}

static void
compositor_bind(struct wl_client *client,
		struct wl_object *global, uint32_t version)
{
	struct wl_compositor *compositor =
		container_of(global, struct wl_compositor, object);

	wl_client_post_event(client, global,
			     WL_COMPOSITOR_TOKEN_VISUAL,
			     &compositor->argb_visual.object,
			     WL_COMPOSITOR_VISUAL_ARGB32);

	wl_client_post_event(client, global,
			     WL_COMPOSITOR_TOKEN_VISUAL,
			     &compositor->premultiplied_argb_visual.object,
			     WL_COMPOSITOR_VISUAL_PREMULTIPLIED_ARGB32);

	wl_client_post_event(client, global,
			     WL_COMPOSITOR_TOKEN_VISUAL,
			     &compositor->rgb_visual.object,
			     WL_COMPOSITOR_VISUAL_XRGB32);
}

WL_EXPORT int
wl_compositor_init(struct wl_compositor *compositor,
		   const struct wl_compositor_interface *interface,
		   struct wl_display *display)
{
	compositor->object.interface = &wl_compositor_interface;
	compositor->object.implementation = (void (**)(void)) interface;
	wl_display_add_object(display, &compositor->object);
	if (wl_display_add_global(display,
				  &compositor->object, compositor_bind))
		return -1;

	compositor->argb_visual.object.interface = &wl_visual_interface;
	compositor->argb_visual.object.implementation = NULL;
	wl_display_add_object(display, &compositor->argb_visual.object);
	if (wl_display_add_global(display,
				  &compositor->argb_visual.object, NULL))
		return -1;

	compositor->premultiplied_argb_visual.object.interface =
		&wl_visual_interface;
	compositor->premultiplied_argb_visual.object.implementation = NULL;
	wl_display_add_object(display,
			      &compositor->premultiplied_argb_visual.object);
       if (wl_display_add_global(display,
                                 &compositor->premultiplied_argb_visual.object,
				 NULL))
	       return -1;

	compositor->rgb_visual.object.interface = &wl_visual_interface;
	compositor->rgb_visual.object.implementation = NULL;
	wl_display_add_object(display, &compositor->rgb_visual.object);
       if (wl_display_add_global(display,
				 &compositor->rgb_visual.object, NULL))
	       return -1;

	return 0;
}
