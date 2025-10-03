#include "wm.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/kd.h>
#include <termios.h>
#include <linux/input.h>
#include <signal.h>
#include <stdatomic.h>

//#define QUICKTEST

#ifdef QUICKTEST
pid_t client_pid = -1;
#endif

struct pollfd *fds = NULL;

int ev_fd = -1;
struct termios orig_termios;
int old_kd_mode = -1;

volatile sig_atomic_t stop_flag = 0;

#define SOCKET_PATH "sqws/sock"

void handle_sigint(int signo) {
    stop_flag = 1;
}

void clients_init(client_array_t *clients) {
    clients->fds = NULL;
    clients->size = 0;
    clients->capacity = 0;
}

void clients_free(client_array_t *clients) {
    for (size_t i = 0; i < clients->size; i++) {
        if (clients->fds[i] >= 0) close(clients->fds[i]);
    }
    free(clients->fds);
    clients->fds = NULL;
    clients->size = 0;
    clients->capacity = 0;
}

bool clients_add(client_array_t *clients, int fd) {
    if (clients->size == clients->capacity) {
        size_t new_capacity = clients->capacity ? clients->capacity * 2 : 4;
        int *new_fds = realloc(clients->fds, new_capacity * sizeof(int));
        if (!new_fds) return false;
        clients->fds = new_fds;
        clients->capacity = new_capacity;
    }
    clients->fds[clients->size++] = fd;
    return true;
}

void clients_remove(client_array_t *clients, size_t index) {
    if (index >= clients->size) return;
    close(clients->fds[index]);
    memmove(&clients->fds[index], &clients->fds[index+1], (clients->size - index - 1) * sizeof(int));
    clients->size--;
}

int get_focused_window_idx(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].focused)
            return i;
    }
    return -1;
}

void event_loop(int server_fd) {
    client_array_t clients;
    clients_init(&clients);

    size_t fds_capacity = 0;

    int drag_window = -1, drag_dx = 0, drag_dy = 0;

    while (!stop_flag) {
        size_t needed = clients.size + 2;
        if (fds_capacity < needed) {
            size_t new_capacity = fds_capacity ? fds_capacity * 2 : 8;
            while (new_capacity < needed) new_capacity *= 2;
            struct pollfd *new_fds = realloc(fds, new_capacity * sizeof(struct pollfd));
            if (!new_fds) {
                perror("realloc");
                break;
            }
            fds = new_fds;
            fds_capacity = new_capacity;
        }

        fds[0].fd = server_fd;
        fds[0].events = POLLIN;

        for (size_t i = 0; i < clients.size; i++) {
            fds[i+1].fd = clients.fds[i];
            fds[i+1].events = POLLIN;
        }

        fds[clients.size + 1].fd = ev_fd;
        fds[clients.size + 1].events = POLLIN;

        int timeout = 10;
        int ret = poll(fds, needed, timeout);
        if (ret < 0) break;

        if (fds[0].revents & POLLIN) {
            int new_fd = accept(server_fd, NULL, NULL);
            if (new_fd >= 0) {
                printf("Client connected\n");
                free_windows();
                if (!clients_add(&clients, new_fd)) {
                    fprintf(stderr, "failed to add client, closing socket\n");
                    close(new_fd);
                }
            }
        }
        
        for (size_t i = 0; i < clients.size; ) {
            int cfd = clients.fds[i];
            if (fds[i+1].revents & POLLIN) {
                unsigned char cmd;
                ssize_t r = read(cfd, &cmd, 1);
                if (r <= 0) {
                    printf("client disconnected, closing all windows\n");
                    clients_remove(&clients, i);
                    free_windows();
                    continue;
                } else {
                    switch (cmd) {
                        case 0x01: {
                            unsigned char buf[1+64+4*4+4];
                            ssize_t total = 0;
                            while (total < (ssize_t)sizeof(buf)) {
                                ssize_t rr = read(cfd, buf + total, sizeof(buf) - total);
                                if (rr <= 0) {
                                    clients_remove(&clients, i);
                                    free_windows();
                                    goto next_client;
                                }
                                total += rr;
                            }
                            int idx = buf[0];
                            char title[65];
                            memcpy(title, &buf[1], 64);
                            title[64] = '\0';
                            int x = *(int *)(buf+65);
                            int y = *(int *)(buf+69);
                            int w = *(int *)(buf+73);
                            int h = *(int *)(buf+77);
                            unsigned char color[4];
                            memcpy(color, buf+81, 4);
                            handle_create(idx, title, x, y, w, h, color);
                            break;
                        }
                        case 0x02: {
                            unsigned char idx;
                            if (read(cfd, &idx, 1) == 1) {
                                handle_destroy(idx);
                            }
                            break;
                        }
                        case 0x03: {
                            unsigned char buf[1+4+4];
                            ssize_t total = 0;
                            while (total < (ssize_t)sizeof(buf)) {
                                ssize_t rr = read(cfd, buf + total, sizeof(buf) - total);
                                if (rr <= 0) {
                                    clients_remove(&clients, i);
                                    free_windows();
                                    goto next_client;
                                }
                                total += rr;
                            }
                            int idx = buf[0];
                            int x = *(int *)(buf+1);
                            int y = *(int *)(buf+5);
                            if (idx >= 0 && idx < MAX_WINDOWS && windows[idx].used) {
                                windows[idx].x = x;
                                windows[idx].y = y;
                            }
                            break;
                        }
                        case 0x04: {
                            unsigned char idx;
                            if (read(cfd, &idx, 1) == 1 && idx < MAX_WINDOWS && windows[idx].used) {
                                window_t *win = &windows[idx];
                                size_t canvas_size = win->canvas_w * win->canvas_h * 4;
                                ssize_t total = 0;
                                while (total < (ssize_t)canvas_size) {
                                    ssize_t r = read(cfd, win->canvas + total, canvas_size - total);
                                    if (r <= 0) {
                                        clients_remove(&clients, i);
                                        free_windows();
                                        goto next_client;
                                    }
                                    total += r;
                                }
                            }
                            break;
                        }
                        case 0x10: {
                            unsigned char idx;
                            if (read(cfd, &idx, 1) == 1 && idx < MAX_WINDOWS && windows[idx].used) {
                                write(cfd, &windows[idx], sizeof(window_t));
                            }
                            break;
                        }
                        case 0x11: {
                            unsigned char idx;
                            ssize_t rr = read(cfd, &idx, 1);
                            if (rr != 1) {
                                fprintf(stderr, "failed to read idx for get_key\n");
                                clients_remove(&clients, i);
                                free_windows();
                                break;
                            }
                            unsigned char key = 0;
                            int fidx = get_focused_window_idx();
                            if (fidx == idx) {
                                for (int k = 1; k < MAX_VK_CODE; k++) {
                                    if (keys_pressed[k]) {
                                        key = (unsigned char)k;
                                        break;
                                    }
                                }
                            }
                            ssize_t ww = write(cfd, &key, sizeof(key));
                            if (ww != sizeof(key)) {
                                fprintf(stderr, "failed to write key response\n");
                                clients_remove(&clients, i);
                                free_windows();
                            }
                            break;
                        }
                        case 0x12: {
                            unsigned char idx;
                            if (read(cfd, &idx, 1) == 1) {
                                int pos[2] = {0, 0};
                                int fidx = get_focused_window_idx();
                                if (fidx == idx) {
                                    pos[0] = mouse_x;
                                    pos[1] = mouse_y;
                                }
                                write(cfd, pos, sizeof(pos));
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
            i++;
            next_client:;
        }

        keyboard_process(&clients);
        mouse_process(&drag_window, &drag_dx, &drag_dy);

        redraw_all(screen_buffer, mode.hdisplay * 4, mode.hdisplay, mode.vdisplay);
        draw_cursor(screen_buffer, mode.hdisplay * 4, mode.hdisplay, mode.vdisplay, mouse_x, mouse_y);
        fb_flush();
    }

    clients_free(&clients);
    free(fds);
}

void cleanup(void) {
#ifdef QUICKTEST
    if (client_pid > 0) kill(client_pid, SIGTERM);
#endif

    keyboard_cleanup();

    fb_cleanup();
    mouse_cleanup();
}

int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    atexit(cleanup);

    memset(keys_pressed, 0, sizeof(keys_pressed));

    if (!fb_init()) return 1;
    if (!mouse_init()) {
        fprintf(stderr, "no mouse\n");
    }

    keyboard_init();

    memset(windows, 0, sizeof(windows));
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = false;

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, SOCKET_PATH, sizeof(addr.sun_path) - 2);
    unlink(SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 128) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

#ifdef QUICKTEST
    client_pid = fork();
    if (client_pid == 0) {
        execl("./client", "./client", NULL);
        perror("execl");
        exit(1);
    } else if (client_pid < 0) {
        perror("fork");
        close(server_fd);
        return 1;
    }
#endif

    event_loop(server_fd);
    close(server_fd);
    fb_cleanup();
    return 0;
}
