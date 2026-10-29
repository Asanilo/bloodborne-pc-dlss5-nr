#define _GNU_SOURCE
// SPDX-License-Identifier: GPL-2.0-or-later
// Lab tool: count frames the compositor actually puts on screen.
//
// Uses zwlr_screencopy_v1 to capture the output as fast as the compositor
// renders it (each capture waits for a new composed frame) and prints a
// sampled hash plus the frame's presentation timestamp. Distinct consecutive
// hashes are displayed content changes; with NVIDIA Smooth Motion the layer
// presents generated frames, so the change rate approaches 2x the game's real
// present rate instead of matching it.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-screencopy-client.h"

static struct wl_display *display;
static struct wl_output *output;
static struct zwlr_screencopy_manager_v1 *manager;
static struct wl_shm *shm_global;

struct shm_buffer {
    struct wl_buffer *buffer;
    void *data;
    size_t size;
    int width, height, stride;
};

static struct shm_buffer shm;
static uint8_t *previous_frame;
static size_t previous_size;
static int ready_flag;
static int failed_flag;
static uint64_t frame_hash;
static uint64_t frame_ts_ns;
static unsigned frame_diff_permille;

static void noop_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
                          int32_t ph, int32_t subpixel, const char *make, const char *model,
                          int32_t transform) {}

static void noop_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
                      int32_t refresh) {}

static void noop_done(void *d, struct wl_output *o) {}

static void noop_scale(void *d, struct wl_output *o, int32_t factor) {}

static void noop_name(void *d, struct wl_output *o, const char *name) {}

static void noop_description(void *d, struct wl_output *o, const char *description) {}

static const struct wl_output_listener output_listener = {
    .geometry = noop_geometry,
    .mode = noop_mode,
    .done = noop_done,
    .scale = noop_scale,
    .name = noop_name,
    .description = noop_description,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) == 0 && !output) {
        output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        wl_output_add_listener(output, &output_listener, NULL);
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        manager = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0 && !shm_global) {
        shm_global = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

static int ensure_buffer(int width, int height, int stride) {
    if (shm.buffer && shm.width == width && shm.height == height && shm.stride == stride) return 0;
    if (shm.buffer) {
        wl_buffer_destroy(shm.buffer);
        munmap(shm.data, shm.size);
        shm.buffer = NULL;
    }
    size_t size = (size_t)stride * height;
    int fd = memfd_create("sm-frame-count", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0) {
        perror("shm");
        return -1;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    if (!shm_global) {
        fprintf(stderr, "missing wl_shm\n");
        close(fd);
        return -1;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm_global, fd, (int32_t)size);
    shm.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    shm.data = data;
    shm.size = size;
    shm.width = width;
    shm.height = height;
    shm.stride = stride;
    return 0;
}

static void frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format,
                         uint32_t width, uint32_t height, uint32_t stride) {
    ensure_buffer((int)width, (int)height, (int)stride);
    zwlr_screencopy_frame_v1_copy(frame, shm.buffer);
}

static void frame_flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {}

static void frame_ready(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t sec_hi,
                        uint32_t sec_lo, uint32_t nsec) {
    // Sampled FNV-1a: cheap enough to keep up with the compositor.
    const uint8_t *p = shm.data;
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i + 8 <= shm.size; i += 64) {
        hash = (hash ^ p[i]) * 1099511628211ull;
    }
    frame_hash = hash;
    frame_ts_ns = ((uint64_t)sec_hi << 32 | sec_lo) * 1000000000ull + nsec;
    // Amplitude, not mere inequality: compositor redraws/dithering change a few
    // sampled bytes, a new game frame changes most of them.
    unsigned changed = 0, samples = 0;
    if (previous_frame && previous_size == shm.size) {
        for (size_t i = 0; i + 8 <= shm.size; i += 64) {
            ++samples;
            int delta = abs((int)p[i] - (int)previous_frame[i]) +
                        abs((int)p[i + 1] - (int)previous_frame[i + 1]) +
                        abs((int)p[i + 2] - (int)previous_frame[i + 2]);
            if (delta > 24) ++changed;
        }
    }
    frame_diff_permille = samples ? (unsigned)((uint64_t)changed * 1000 / samples) : 0;
    if (!previous_frame || previous_size != shm.size) {
        previous_frame = malloc(shm.size);
        previous_size = previous_frame ? shm.size : 0;
    }
    if (previous_frame) memcpy(previous_frame, shm.data, shm.size);
    ready_flag = 1;
    zwlr_screencopy_frame_v1_destroy(frame);
}

static void frame_failed(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    failed_flag = 1;
    zwlr_screencopy_frame_v1_destroy(frame);
}

static void frame_damage(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t x,
                         uint32_t y, uint32_t width, uint32_t height) {}

static void frame_linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format,
                               uint32_t width, uint32_t height) {}

static void frame_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *frame) {}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_buffer,
    .flags = frame_flags,
    .ready = frame_ready,
    .failed = frame_failed,
    .damage = frame_damage,
    .linux_dmabuf = frame_linux_dmabuf,
    .buffer_done = frame_buffer_done,
};

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 3.0;
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "cannot connect to the Wayland display\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
    if (!output || !manager || !shm_global) {
        fprintf(stderr, "missing wl_output or zwlr_screencopy_manager_v1\n");
        return 1;
    }

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    unsigned index = 0;
    uint64_t previous = 0;
    unsigned distinct = 0;
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= seconds) break;
        ready_flag = 0;
        failed_flag = 0;
        struct zwlr_screencopy_frame_v1 *frame =
            zwlr_screencopy_manager_v1_capture_output(manager, 0, output);
        zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, NULL);
        while (!ready_flag && !failed_flag) {
            if (wl_display_dispatch(display) < 0) {
                fprintf(stderr, "dispatch failed\n");
                return 1;
            }
        }
        if (failed_flag) continue;
        if (index == 0 || frame_hash != previous) ++distinct;
        previous = frame_hash;
        printf("FRAME %u %llu %016llx %u\n", index, (unsigned long long)frame_ts_ns,
               (unsigned long long)frame_hash, frame_diff_permille);
        ++index;
    }
    printf("TOTAL frames=%u distinct=%u seconds=%.3f\n", index, distinct, seconds);
    zwlr_screencopy_manager_v1_destroy(manager);
    wl_display_disconnect(display);
    return 0;
}
