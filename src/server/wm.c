#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <poll.h>
#include <linux/input.h>

#include "wm.h"
#include "fonts.h"

window_t windows[MAX_WINDOWS];

#define BTN_SIZE 16
#define BTN_SPACING 4

#define TITLEBAR_HEIGHT 20
#define BORDER 3

static inline void put_pixel(unsigned char *buf, int x, int y, const unsigned char *color, int pitch, int sw, int sh) {
    uint32_t mask = (unsigned)(x | (sw - 1 - x) | y | (sh - 1 - y)) >> 31;
    if (!mask) {
        *(uint32_t *)(buf + y * pitch + x * 4) = *(const uint32_t *)color;
    }
}

void draw_char(unsigned char *buf, int x, int y, char ch, const unsigned char *color, int pitch, int sw, int sh) {
    if (x >= sw || y >= sh) return;
    if (x + 8 <= 0 || y + 16 <= 0) return;

    const unsigned char *glyph = font8x16_basic[(unsigned char)ch];
    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0) continue;
        if (py >= sh) break;

        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0) continue;
            if (px >= sw) break;

            if (bits & (1 << (7 - col))) {
                put_pixel(buf, px, py, color, pitch, sw, sh);
            }
        }
    }
}

void draw_text(unsigned char *buf, int x, int y, const char *text, const unsigned char *color, int pitch, int sw, int sh) {
    while (*text) {
        draw_char(buf, x, y, *text, color, pitch, sw, sh);
        x += 8;
        text++;
    }
}

//=======================================================================

static inline void blend_pixel(unsigned char *dst, const unsigned char *src) {
    unsigned a = src[3];
    if (a == 255) {
        *(uint32_t *)dst = *(const uint32_t *)src;
    } else if (a) {
        for (int i = 0; i < 3; i++)
            dst[i] = (src[i] * a + dst[i] * (255 - a)) / 255;
        dst[3] = 255;
    }
}

void draw_rect(unsigned char *buf, int x, int y, int w, int h,
               const unsigned char *color, bool alpha, int pitch, int sw, int sh) {
    int start_y = y < 0 ? 0 : y;
    int end_y = y + h > sh ? sh : y + h;

    for (int py = start_y; py < end_y; py++) {
        int start_x = x < 0 ? 0 : x;
        int end_x = x + w > sw ? sw : x + w;

        for (int px = start_x; px < end_x; px++) {
            if (alpha) blend_pixel(buf + py * pitch + px * 4, color);
            else put_pixel(buf, px, py, color, pitch, sw, sh);
        }
    }
}

void draw_button(unsigned char *buf, int x, int y, int w, int h, const unsigned char *color, const char *label, int pitch, int sw, int sh) {
    draw_rect(buf, x, y, w, h, color, 1, pitch, sw, sh);
    unsigned char text_color[4] = {255, 255, 255, 255};
    int tx = x + (w - 8 * (int)strlen(label)) / 2;
    int ty = y + (h - 16) / 2;
    draw_text(buf, tx, ty, label, text_color, pitch, sw, sh);
}

static void draw_window_buttons(unsigned char *buf, int btn_x, int btn_y, int pitch, int sw, int sh, int fullscreen) {
    static const unsigned char close_color[4] = {200, 50, 50, 255};
    static const unsigned char min_color[4] = {50, 200, 50, 255};
    static const unsigned char fs_color[4] = {50, 50, 200, 255};

    draw_button(buf, btn_x, btn_y, BTN_SIZE, BTN_SIZE, close_color, "X", pitch, sw, sh);
    btn_x -= (BTN_SIZE + BTN_SPACING);
    draw_button(buf, btn_x, btn_y, BTN_SIZE, BTN_SIZE, min_color, "-", pitch, sw, sh);
    btn_x -= (BTN_SIZE + BTN_SPACING);
    draw_button(buf, btn_x, btn_y, BTN_SIZE, BTN_SIZE, fs_color, fullscreen ? "<-" : "[]", pitch, sw, sh);
}

void draw_window(window_t *w, unsigned char *buf, int pitch, int sw, int sh) {
    if (!w->used) return;

    static const unsigned char text_color[4] = {255,255,255,255};
    static const unsigned char border[4] = {40,40,40,255};
    unsigned char title[4] = {0, w->focused ? 200 : 128, 255, 200};
    unsigned char bg[4] = {w->color[0], w->color[1], w->color[2], 255};

    if (w->x + w->w <= 0 || w->y + w->h <= 0 || w->x >= sw || w->y >= sh) return;

    int btn_y = w->y + BORDER + (TITLEBAR_HEIGHT - BTN_SIZE)/2;
    int btn_x_start = w->x + w->w - BORDER - BTN_SPACING - BTN_SIZE;

    if (w->minimized) {
        int x0 = w->x < 0 ? 0 : w->x;
        int y0 = w->y < 0 ? 0 : w->y;
        int x1 = (w->x + w->w > sw) ? sw : (w->x + w->w);
        int y1 = (w->y + TITLEBAR_HEIGHT + BORDER > sh) ? sh : (w->y + TITLEBAR_HEIGHT + BORDER);
        if (x1 <= x0 || y1 <= y0) return;

        draw_rect(buf, w->x, w->y, w->w, BORDER, border, 0, pitch, sw, sh);
        draw_rect(buf, w->x, w->y, BORDER, TITLEBAR_HEIGHT + BORDER, border, 0, pitch, sw, sh);
        draw_rect(buf, w->x + w->w - BORDER, w->y, BORDER, TITLEBAR_HEIGHT + BORDER, border, 0, pitch, sw, sh);
        draw_rect(buf, w->x, w->y + BORDER, w->w, TITLEBAR_HEIGHT, title, 1, pitch, sw, sh);

        draw_text(buf, w->x + BORDER + 4, w->y + BORDER + 2, w->title, text_color, pitch, sw, sh);
        draw_window_buttons(buf, btn_x_start, btn_y, pitch, sw, sh, w->maximized);
        return;
    }

    int cx = w->x + BORDER, cy = w->y + BORDER + TITLEBAR_HEIGHT;
    int cw = w->w - 2 * BORDER, ch = w->h - TITLEBAR_HEIGHT - 2 * BORDER;
    
    draw_rect(buf, w->x, w->y, w->w, BORDER, border, 0, pitch, sw, sh);
    draw_rect(buf, w->x, w->y, BORDER, w->h, border, 0, pitch, sw, sh);
    draw_rect(buf, w->x + w->w - BORDER, w->y, BORDER, w->h, border, 0, pitch, sw, sh);
    draw_rect(buf, w->x, w->y + w->h - BORDER, w->w, BORDER, border, 0, pitch, sw, sh);

    draw_rect(buf, cx, w->y + BORDER, cw, TITLEBAR_HEIGHT, title, 1, pitch, sw, sh);
    draw_text(buf, w->x + BORDER + 4, w->y + BORDER + 2, w->title, text_color, pitch, sw, sh);
    draw_window_buttons(buf, btn_x_start, btn_y, pitch, sw, sh, w->maximized);

    draw_rect(buf, cx, cy, cw, ch, bg, 0, pitch, sw, sh);
    if (!w->canvas) return;

    int dst_x = cx < 0 ? 0 : cx;
    int src_x = cx < 0 ? -cx : 0;
    int vis_width = cw - src_x;
    if (dst_x + vis_width > sw) vis_width = sw - dst_x;
    if (vis_width <= 0) return;

    int vis_height = ch;
    if (cy < 0) {
        vis_height += cy; cy = 0;
    }
    if (cy + vis_height > sh) vis_height = sh - cy;
    if (vis_height <= 0) return;
    
#if 1
    for (int y = 0; y < vis_height; y++) {
        for (int x = 0; x < vis_width; x++) {
            put_pixel(buf, dst_x + x, cy + y,
                    w->canvas + (y * cw + src_x + x) * 4,
                    pitch, sw, sh);
        }
    }
#endif
}

void redraw_all(unsigned char *buf, int pitch, int sw, int sh) {
    unsigned char *back_buf = malloc(pitch * sh);
    if (!back_buf) return;
    memset(back_buf, 0, pitch * sh);
    for (int i = 0; i < MAX_WINDOWS; i++) {
        draw_window(&windows[i], back_buf, pitch, sw, sh);
    }
    memcpy(buf, back_buf, pitch * sh);
    free(back_buf);
}

void draw_cursor(unsigned char *buf, int pitch, int sw, int sh, int cx, int cy) {
    unsigned char c[4] = {100, 100, 100, 255};
    for (int y = -3; y <= 3; y++) {
        int py = cy + y;
        if (py < 0 || py >= sh) continue;
        for (int x = -3; x <= 3; x++) {
            int px = cx + x;
            if (px < 0 || px >= sw) continue;
            put_pixel(buf, px, py, c, pitch, sw, sh);
        }
    }
}

void free_windows() {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].canvas) free(windows[i].canvas);
        windows[i].canvas = NULL;
        windows[i].used = false;
    }
}

bool point_in_rect(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px < x + w && py >= y && py < y + h);
}

void process_window_buttons(window_t *w, int mx, int my) {
    if (!w->used) return;

    enum { CLOSE, MINIMIZE, MAXIMIZE };
    
    int btn_x = w->x + w->w - BORDER - BTN_SPACING - BTN_SIZE;
    int btn_y = w->y + BORDER + (TITLEBAR_HEIGHT - BTN_SIZE) / 2;

    for (int i = 0; i < 3; i++) {
        if (point_in_rect(mx, my, btn_x, btn_y, BTN_SIZE, BTN_SIZE)) {
            switch (i) {
                case CLOSE:
                    w->used = false;
                    return;

                case MINIMIZE:
                    w->minimized = !w->minimized;
                    return;

                case MAXIMIZE:
                    if (!w->maximized) {
                        // save old size and pos
                        w->prev_x = w->x;
                        w->prev_y = w->y;
                        w->prev_w = w->w;
                        w->prev_h = w->h;
                        w->prev_canvas_w = w->canvas_w;
                        w->prev_canvas_h = w->canvas_h;

                        int new_w = mode.hdisplay;
                        int new_h = mode.vdisplay;
                        int new_canvas_w = new_w - 2 * BORDER;
                        int new_canvas_h = new_h - TITLEBAR_HEIGHT - 2 * BORDER;

                        unsigned char *new_canvas = malloc(new_canvas_w * new_canvas_h * 4);
                        if (new_canvas) {
                            memset(new_canvas, 0, new_canvas_w * new_canvas_h * 4);
                            int copy_h = (w->canvas_h < new_canvas_h) ? w->canvas_h : new_canvas_h;
                            int copy_w = (w->canvas_w < new_canvas_w) ? w->canvas_w : new_canvas_w;
                            for (int row = 0; row < copy_h; row++) {
                                memcpy(new_canvas + row * new_canvas_w * 4,
                                       w->canvas + row * w->canvas_w * 4,
                                       copy_w * 4);
                            }
                            free(w->canvas);
                            w->canvas = new_canvas;
                            w->x = 0;
                            w->y = 0;
                            w->w = new_w;
                            w->h = new_h;
                            w->canvas_w = new_canvas_w;
                            w->canvas_h = new_canvas_h;
                            w->maximized = true;
                            w->minimized = false;
                        } else {
                            fprintf(stderr, "malloc failed for maximize\n");
                        }
                    } else {
                        int new_canvas_w = w->prev_canvas_w;
                        int new_canvas_h = w->prev_canvas_h;

                        unsigned char *new_canvas = malloc(new_canvas_w * new_canvas_h * 4);
                        if (new_canvas) {
                            memset(new_canvas, 0, new_canvas_w * new_canvas_h * 4);
                            int copy_h = (w->canvas_h < new_canvas_h) ? w->canvas_h : new_canvas_h;
                            int copy_w = (w->canvas_w < new_canvas_w) ? w->canvas_w : new_canvas_w;
                            for (int row = 0; row < copy_h; row++) {
                                memcpy(new_canvas + row * new_canvas_w * 4,
                                       w->canvas + row * w->canvas_w * 4,
                                       copy_w * 4);
                            }
                            free(w->canvas);
                            w->canvas = new_canvas;
                            w->x = w->prev_x;
                            w->y = w->prev_y;
                            w->w = w->prev_w;
                            w->h = w->prev_h;
                            w->canvas_w = new_canvas_w;
                            w->canvas_h = new_canvas_h;
                            w->maximized = false;
                        } else {
                            fprintf(stderr, "malloc failed for unmaximize\n");
                        }
                    }
                    return;
            }
        }
        btn_x -= (BTN_SIZE + BTN_SPACING);
    }
}

void handle_move(window_t *w, int dx, int dy) {
    if (!w->used) return;
    w->x = dx;
    w->y = dy;
}

void handle_create(int idx, const char *title, int x, int y, int content_w, int content_h, const unsigned char *color) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    window_t *win = &windows[idx];
    if (win->used && win->canvas) free(win->canvas);

    win->x = x; win->y = y;
    win->canvas_w = content_w > 0 ? content_w : 1;
    win->canvas_h = content_h > 0 ? content_h : 1;

    win->w = win->canvas_w + 2 * BORDER;
    win->h = win->canvas_h + TITLEBAR_HEIGHT + 2 * BORDER;
    *(uint32_t *)win->color = *(const uint32_t *)color;
    snprintf(win->title, sizeof(win->title), "%s", title);
    win->focused = false;

    int cw = content_w;
    int ch = content_h;
    if (cw <= 0) cw = 1;
    if (ch <= 0) ch = 1;

    win->canvas = malloc(cw * ch * 4);
    if (win->canvas) memset(win->canvas, 255, cw * ch * 4);
    else {
        fprintf(stderr, "failed to allocate window canvas\n");
    }
    win->used = true;
}

void handle_destroy(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    if (windows[idx].used) {
        free(windows[idx].canvas);
        windows[idx].canvas = NULL;
        windows[idx].used = false;
    }
}
