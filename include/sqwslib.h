#ifndef SQWSLIB_H
#define SQWSLIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "sqws/sock"

typedef struct window window_t;

struct window {
    bool used;
    int x, y, w, h, canvas_w, canvas_h;
    unsigned char color[4];
    char title[64];
    bool focused;

    unsigned char *canvas;

    bool minimized;
    bool maximized;
    int prev_x, prev_y, prev_w, prev_h, prev_canvas_w, prev_canvas_h;
};

typedef struct SqwsClient SqwsClient;
typedef struct SqwsWindow SqwsWindow;

struct SqwsClient {
    int fd;
};

struct SqwsWindow {
    SqwsClient *client;
    int idx;
    window_t info;
    unsigned char *canvas;
    size_t canvas_size;
};

static inline SqwsClient *sqws_connect(void) {
    SqwsClient *client = malloc(sizeof(SqwsClient));
    if (!client) return NULL;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); free(client); return NULL; }
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, SOCKET_PATH, sizeof(addr.sun_path) - 2);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        free(client);
        return NULL;
    }
    client->fd = fd;
    return client;
}

static inline void sqws_disconnect(SqwsClient *client) {
    if (client) {
        close(client->fd);
        free(client);
    }
}

static inline SqwsWindow *sqws_create_window(SqwsClient *client, int idx, const char *title, int x, int y, int w, int h, const uint8_t *color) {
    if (!client) return NULL;

    SqwsWindow *win = malloc(sizeof(SqwsWindow));
    if (!win) return NULL;

    win->client = client;
    win->idx = idx;

    uint8_t cmd = 0x01;
    uint8_t buf[85] = {0};
    buf[0] = idx;
    strncpy((char*)(buf+1), title, 64);
    int *p = (int*)(buf+65);
    p[0] = x; p[1] = y; p[2] = w; p[3] = h;
    memcpy(buf+81, color, 4);
    write(client->fd, &cmd, 1);
    write(client->fd, buf, sizeof(buf));

    uint8_t cmd2[2] = {0x10, (uint8_t)idx};
    if (write(client->fd, cmd2, 2) != 2 ||
        read(client->fd, &win->info, sizeof(window_t)) != sizeof(window_t)) {
        free(win);
        return NULL;
    }

    win->canvas_size = (size_t)win->info.canvas_w * win->info.canvas_h * 4;
    win->canvas = malloc(win->canvas_size);
    if (!win->canvas) {
        free(win);
        return NULL;
    }
    memset(win->canvas, 0, win->canvas_size);

    return win;
}

static inline void sqws_destroy_window(SqwsWindow *win) {
    if (!win) return;
    uint8_t buf[2] = {0x02, (uint8_t)win->idx};
    write(win->client->fd, buf, 2);
    free(win->canvas);
    free(win);
}

static inline void sqws_move_window(SqwsWindow *win, int x, int y) {
    if (!win) return;
    unsigned char buf[8];
    buf[0] = 0x03;
    buf[1] = win->idx;
    *(int*)&buf[2] = x;
    *(int*)&buf[6] = y;
    write(win->client->fd, buf, sizeof(buf));
}

static inline void sqws_draw_window(SqwsWindow *win) {
    if (!win) return;
    uint8_t cmd = 0x04;
    write(win->client->fd, &cmd, 1);
    write(win->client->fd, &win->idx, 1);
    write(win->client->fd, win->canvas, win->canvas_size);
}

static inline int sqws_request_window_info(SqwsWindow *win) {
    if (!win) return -1;
    uint8_t cmd[2] = {0x10, (uint8_t)win->idx};
    if (write(win->client->fd, cmd, 2) != 2 ||
        read(win->client->fd, &win->info, sizeof(window_t)) != sizeof(window_t)) {
        return -1;
    }
    return 0;
}

static inline unsigned char sqws_get_key(SqwsWindow *win) {
    if (!win) return 0;
    uint8_t cmd[2] = {0x11, (uint8_t)win->idx};
    if (write(win->client->fd, cmd, 2) != 2) {
        return 0;
    }
    unsigned char key = 0;
    ssize_t r = read(win->client->fd, &key, sizeof(key));
    if (r != sizeof(key)) return 0;
    return key;
}

static inline int sqws_get_mouse_pos(SqwsWindow *win, int *out_x, int *out_y) {
    if (!win) return -1;
    uint8_t cmd[2] = {0x12, (uint8_t)win->idx};
    if (write(win->client->fd, cmd, 2) != 2) {
        return -1;
    }
    int pos[2] = {0,0};
    ssize_t r = read(win->client->fd, pos, sizeof(pos));
    if (r != sizeof(pos)) return -1;
    *out_x = pos[0];
    *out_y = pos[1];
    return 0;
}

#endif // SQWSLIB_H