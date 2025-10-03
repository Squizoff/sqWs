#include "wm.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

unsigned char *screen_buffer = NULL;

static int drm_fd = -1;
static uint32_t crtc_id = 0;
static uint32_t connector_id = 0;
drmModeModeInfo mode;

static struct {
    uint32_t handle;
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    size_t size;
    void *map;
} dumb_buf[2];

static int front_buf = 0;

static bool drm_setup(void) {
    drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { perror("open"); return false; }

    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) { perror("drmModeGetResources"); goto err_close; }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            connector_id = conn->connector_id;
            mode = conn->modes[0];
            break;
        }
        drmModeFreeConnector(conn);
        conn = NULL;
    }
    if (!conn) { fprintf(stderr, "no connected connector found\n"); goto err_res; }

    drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
    if (!enc) { fprintf(stderr, "no encoder found\n"); goto err_conn; }
    crtc_id = enc->crtc_id;
    drmModeFreeEncoder(enc);

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return true;

err_conn:
    drmModeFreeConnector(conn);
err_res:
    drmModeFreeResources(res);
err_close:
    close(drm_fd);
    return false;
}

static bool create_dumb_buffer(int idx, uint32_t w, uint32_t h) {
    struct drm_mode_create_dumb creq = {.width=w, .height=h, .bpp=32};
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) { perror("DRM_IOCTL_MODE_CREATE_DUMB"); return false; }

    dumb_buf[idx].handle = creq.handle;
    dumb_buf[idx].pitch = creq.pitch;
    dumb_buf[idx].size = creq.size;
    dumb_buf[idx].width = w;
    dumb_buf[idx].height = h;

    if (drmModeAddFB(drm_fd, w, h, 24, 32, creq.pitch, creq.handle, &dumb_buf[idx].fb_id)) {
        perror("drmModeAddFB"); return false;
    }

    struct drm_mode_map_dumb mreq = {.handle=creq.handle};
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) { perror("DRM_IOCTL_MODE_MAP_DUMB"); return false; }

    dumb_buf[idx].map = mmap(NULL, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);
    if (dumb_buf[idx].map == MAP_FAILED) { perror("mmap"); return false; }

    memset(dumb_buf[idx].map, 0, creq.size);
    return true;
}

bool fb_init() {
    if (!drm_setup()) return false;

    if (!create_dumb_buffer(0, mode.hdisplay, mode.vdisplay)) return false;
    if (!create_dumb_buffer(1, mode.hdisplay, mode.vdisplay)) return false;

    if (drmModeSetCrtc(drm_fd, crtc_id, dumb_buf[0].fb_id, 0, 0, &connector_id, 1, &mode)) {
        perror("drmModeSetCrtc");
        return false;
    }

    screen_buffer = malloc(dumb_buf[0].size);
    if (!screen_buffer) {
        perror("malloc screen_buffer");
        return false;
    }

    return true;
}

void fb_cleanup() {
    free_windows();

    if (screen_buffer) free(screen_buffer);

    for (int i = 0; i < 2; i++) {
        if (dumb_buf[i].map) {
            munmap(dumb_buf[i].map, dumb_buf[i].size);
            dumb_buf[i].map = NULL;
        }
        if (dumb_buf[i].fb_id) {
            drmModeRmFB(drm_fd, dumb_buf[i].fb_id);
            dumb_buf[i].fb_id = 0;
        }
        if (dumb_buf[i].handle) {
            struct drm_mode_destroy_dumb dreq = {0};
            dreq.handle = dumb_buf[i].handle;
            drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
            dumb_buf[i].handle = 0;
        }
    }

    if (drm_fd >= 0) {
        close(drm_fd);
        drm_fd = -1;
    }
}

void fb_flush() {
    int back_buf = 1 - front_buf;
    memcpy(dumb_buf[back_buf].map, screen_buffer, dumb_buf[back_buf].size);

    if (drmModeSetCrtc(drm_fd, crtc_id, dumb_buf[back_buf].fb_id, 0, 0, &connector_id, 1, &mode)) {
        perror("drmModeSetCrtc swap");
    } else {
        front_buf = back_buf;
    }
}