#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "relative-pointer-client-protocol.h"
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct wl_compositor* compositor;
static struct wl_shm* shm;
static struct wl_seat* seat;
static struct xdg_wm_base* shell;
static struct zwp_relative_pointer_manager_v1* relative_manager;
static struct wl_surface* surface;
static int width = 640, height = 480, ready = 0;

static void seat_capabilities(void* d, struct wl_seat* s, uint32_t caps)
{
	(void)d;
	(void)s;
	(void)caps;
}

static void seat_name(void* d, struct wl_seat* s, const char* name)
{
	(void)d;
	(void)s;
	(void)name;
}
static const struct wl_seat_listener seat_listener = {seat_capabilities, seat_name};

static void pointer_enter(
    void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s, wl_fixed_t x, wl_fixed_t y)
{
	(void)d;
	(void)p;
	(void)serial;
	(void)s;
	(void)x;
	(void)y;
}

static void pointer_leave(void* d, struct wl_pointer* p, uint32_t serial, struct wl_surface* s)
{
	(void)d;
	(void)p;
	(void)serial;
	(void)s;
}

static void pointer_motion(void* d, struct wl_pointer* p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d;
	(void)p;
	(void)t;
	(void)x;
	(void)y;
}

static void pointer_button(void* d, struct wl_pointer* p, uint32_t s, uint32_t t, uint32_t b, uint32_t state)
{
	(void)d;
	(void)p;
	(void)s;
	(void)t;
	(void)b;
	(void)state;
}

static void pointer_axis(void* d, struct wl_pointer* p, uint32_t t, uint32_t axis, wl_fixed_t value)
{
	(void)d;
	(void)p;
	(void)t;
	(void)axis;
	(void)value;
}

static void pointer_frame(void* d, struct wl_pointer* p)
{
	(void)d;
	(void)p;
}

static void pointer_source(void* d, struct wl_pointer* p, uint32_t source)
{
	(void)d;
	(void)p;
	(void)source;
}

static void pointer_stop(void* d, struct wl_pointer* p, uint32_t t, uint32_t axis)
{
	(void)d;
	(void)p;
	(void)t;
	(void)axis;
}

static void pointer_discrete(void* d, struct wl_pointer* p, uint32_t axis, int32_t discrete)
{
	(void)d;
	(void)p;
	(void)axis;
	(void)discrete;
}
static const struct wl_pointer_listener pointer_listener = {.enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_source,
    .axis_stop = pointer_stop,
    .axis_discrete = pointer_discrete};

static void ping(void* data, struct xdg_wm_base* base, uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener shell_listener = {.ping = ping};

static void global(void* data, struct wl_registry* registry, uint32_t id, const char* name, uint32_t version)
{
	(void)data;
	if (!strcmp(name, "wl_compositor"))
		compositor = wl_registry_bind(registry, id, &wl_compositor_interface, version < 4 ? version : 4);
	else if (!strcmp(name, "wl_shm"))
		shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
	else if (!strcmp(name, "wl_seat"))
	{
		seat = wl_registry_bind(registry, id, &wl_seat_interface, version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	}
	else if (!strcmp(name, "xdg_wm_base"))
	{
		shell = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(shell, &shell_listener, NULL);
	}
	else if (!strcmp(name, "zwp_relative_pointer_manager_v1"))
		relative_manager = wl_registry_bind(registry, id, &zwp_relative_pointer_manager_v1_interface, 1);
}

static void removed(void* data, struct wl_registry* registry, uint32_t id)
{
	(void)data;
	(void)registry;
	(void)id;
}
static const struct wl_registry_listener registry_listener = {global, removed};

static void configure(void* data, struct xdg_surface* xdg, uint32_t serial)
{
	(void)data;
	xdg_surface_ack_configure(xdg, serial);
	int fd = memfd_create("pointeraccel-raw-probe", MFD_CLOEXEC);
	size_t size = (size_t)width * height * 4;
	if (fd < 0 || ftruncate(fd, (off_t)size))
		exit(1);
	uint32_t* pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED)
		exit(1);
	for (size_t i = 0; i < size / 4; ++i)
		pixels[i] = 0xff202830;
	struct wl_shm_pool* pool = wl_shm_create_pool(shm, fd, (int)size);
	struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	munmap(pixels, size);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, width, height);
	wl_surface_commit(surface);
	if (!ready)
	{
		puts("READY");
		fflush(stdout);
		ready = 1;
	}
}
static const struct xdg_surface_listener surface_listener = {.configure = configure};

static void top_configure(void* data, struct xdg_toplevel* top, int32_t w, int32_t h, struct wl_array* states)
{
	(void)data;
	(void)top;
	(void)states;
	if (w > 0)
		width = w;
	if (h > 0)
		height = h;
}

static void top_close(void* data, struct xdg_toplevel* top)
{
	(void)data;
	(void)top;
	exit(0);
}
static const struct xdg_toplevel_listener top_listener = {.configure = top_configure, .close = top_close};

static void relative(void* data,
    struct zwp_relative_pointer_v1* pointer,
    uint32_t hi,
    uint32_t lo,
    wl_fixed_t dx,
    wl_fixed_t dy,
    wl_fixed_t rx,
    wl_fixed_t ry)
{
	(void)data;
	(void)pointer;
	printf("{\"raw_x\":%.8f,\"raw_y\":%.8f,\"dx\":%.8f,\"dy\":%.8f,\"time\":%llu}\n",
	    wl_fixed_to_double(rx),
	    wl_fixed_to_double(ry),
	    wl_fixed_to_double(dx),
	    wl_fixed_to_double(dy),
	    (unsigned long long)(((uint64_t)hi << 32) | lo));
	fflush(stdout);
}
static const struct zwp_relative_pointer_v1_listener relative_listener = {.relative_motion = relative};

int main(void)
{
	struct wl_display* display = wl_display_connect(NULL);
	if (!display)
	{
		fputs("无法连接 Wayland。\n", stderr);
		return 1;
	}
	struct wl_registry* registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !seat || !shell || !relative_manager)
	{
		fputs("缺少 Wayland 协议。\n", stderr);
		return 1;
	}
	struct wl_pointer* pointer = wl_seat_get_pointer(seat);
	wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	struct zwp_relative_pointer_v1* raw =
	    zwp_relative_pointer_manager_v1_get_relative_pointer(relative_manager, pointer);
	zwp_relative_pointer_v1_add_listener(raw, &relative_listener, NULL);
	surface = wl_compositor_create_surface(compositor);
	struct xdg_surface* xdg = xdg_wm_base_get_xdg_surface(shell, surface);
	xdg_surface_add_listener(xdg, &surface_listener, NULL);
	struct xdg_toplevel* top = xdg_surface_get_toplevel(xdg);
	xdg_toplevel_add_listener(top, &top_listener, NULL);
	xdg_toplevel_set_title(top, "PointerAccel raw probe");
	xdg_toplevel_set_app_id(top, "pointeraccel.raw-probe");
	xdg_toplevel_set_fullscreen(top, NULL);
	wl_surface_commit(surface);
	while (wl_display_dispatch(display) >= 0)
	{
	}
	return 0;
}
