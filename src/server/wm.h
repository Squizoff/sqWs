#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <linux/fb.h>
#include <stdbool.h>
#include <stddef.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>

#define MAX_WINDOWS 64

extern struct pollfd *fds;

typedef struct {
    int *fds;
    size_t size;
    size_t capacity;
} client_array_t;

typedef struct {
    bool used;
    int x, y, w, h, canvas_w, canvas_h;
    unsigned char color[4];
    char title[64];
    bool focused;

    unsigned char *canvas;

    bool minimized;
    bool maximized;
    int prev_x, prev_y, prev_w, prev_h, prev_canvas_w, prev_canvas_h;
} window_t;

bool fb_init();
void fb_cleanup();
void fb_flush();

#define MAX_VK_CODE 256
extern bool keys_pressed[MAX_VK_CODE];

extern int mouse_fd, mouse_x, mouse_y;
extern bool mouse_left;

bool mouse_init(void);
void mouse_cleanup(void);
void mouse_process(int *drag_window, int *dx, int *dy);

bool keyboard_init(void);
void keyboard_cleanup(void);
void keyboard_process(client_array_t *clients);

void process_window_buttons(window_t *w, int mx, int my);

void free_windows(void);

void handle_create(int idx, const char *title, int x, int y, int w, int h, const unsigned char *color);
void handle_destroy(int idx);

void redraw_all(unsigned char *buf, int pitch, int sw, int sh);
void draw_cursor(unsigned char *buf, int pitch, int sw, int sh, int cx, int cy);

extern int ev_fd;

extern window_t windows[MAX_WINDOWS];
extern unsigned char *screen_buffer;
extern int fb_fd;
extern drmModeModeInfo mode;
extern size_t screensize;
extern unsigned char *fbp;

#endif // WM_H
