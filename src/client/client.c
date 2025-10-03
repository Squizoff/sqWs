// example of use sqws

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "sqwslib.h"

#define WIN_IDX 0
#define INIT_WIN_W 320
#define INIT_WIN_H 240
#define SQUARE_SIZE 20
#define SPEED 2
#define FRAME_DELAY 50000

int main() {
    SqwsClient *client = sqws_connect();
    if (!client) {
        fprintf(stderr, "failed to connect\n");
        return 1;
    }
    printf("connected!\n");

    uint8_t color[4] = {100, 100, 255, 255};
    SqwsWindow *win = sqws_create_window(client, WIN_IDX, "example", 100, 100, INIT_WIN_W, INIT_WIN_H, color);
    if (!win || win->info.canvas_w != INIT_WIN_W || win->info.canvas_h != INIT_WIN_H) {
        fprintf(stderr, "create failed or invalid size\n");
        if (win) sqws_destroy_window(win);
        sqws_disconnect(client);
        return 1;
    }
    printf("created: (%zu x %zu)\n", (size_t)win->info.canvas_w, (size_t)win->info.canvas_h);

    int pos = 0;
    int dir = 1;
    unsigned char key = 0;

    while (key == 0) {
        int canvas_w = win->info.canvas_w;
        int canvas_h = win->info.canvas_h;
        int y_start = (canvas_h - SQUARE_SIZE) / 2;

        memset(win->canvas, 0, win->canvas_size);

        for (int dy = 0; dy < SQUARE_SIZE; dy++) {
            for (int dx = 0; dx < SQUARE_SIZE; dx++) {
                int x = pos + dx;
                int y = y_start + dy;
                if (x >= 0 && x < canvas_w && y >= 0 && y < canvas_h) {
                    size_t idx = (y * canvas_w + x) * 4;
                    win->canvas[idx + 0] = 255; // R
                    win->canvas[idx + 1] = 0;   // G
                    win->canvas[idx + 2] = 0;   // B
                    win->canvas[idx + 3] = 255; // A
                }
            }
        }

        sqws_draw_window(win);

        key = sqws_get_key(win);
        usleep(FRAME_DELAY);
        
        pos += dir * SPEED;
        if (pos <= 0 || pos >= canvas_w - SQUARE_SIZE) {
            dir = -dir;
        }
    }
    printf("key: %c (0x%02x)\n", key ? key : ' ', key);

    sqws_destroy_window(win);
    printf("destroyed\n");

    sqws_disconnect(client);
    return 0;
}