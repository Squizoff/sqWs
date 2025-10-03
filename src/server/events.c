#include "wm.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/poll.h>
#include <unistd.h>
#include <linux/keyboard.h>
#include <linux/input.h>
#include <dirent.h>
#include <glob.h>

int mouse_fd = -1, mouse_x = 0, mouse_y = 0;
bool mouse_left = false;

bool keys_pressed[MAX_VK_CODE];

// REVIEW
static int linux_keycode_to_vk(int linux_code) {
    switch (linux_code) {
        case KEY_ESC: return 0x1B;
        case KEY_ENTER: return 0x0D;
        case KEY_BACKSPACE: return 0x08;
        case KEY_TAB: return 0x09;
        case KEY_SPACE: return 0x20;

        case KEY_LEFT: return 0x25;
        case KEY_UP: return 0x26;
        case KEY_RIGHT: return 0x27;
        case KEY_DOWN: return 0x28;

        case KEY_A: return 0x41;
        case KEY_B: return 0x42;
        case KEY_C: return 0x43;
        case KEY_D: return 0x44;
        case KEY_E: return 0x45;
        case KEY_F: return 0x46;
        case KEY_G: return 0x47;
        case KEY_H: return 0x48;
        case KEY_I: return 0x49;
        case KEY_J: return 0x4A;
        case KEY_K: return 0x4B;
        case KEY_L: return 0x4C;
        case KEY_M: return 0x4D;
        case KEY_N: return 0x4E;
        case KEY_O: return 0x4F;
        case KEY_P: return 0x50;
        case KEY_Q: return 0x51;
        case KEY_R: return 0x52;
        case KEY_S: return 0x53;
        case KEY_T: return 0x54;
        case KEY_U: return 0x55;
        case KEY_V: return 0x56;
        case KEY_W: return 0x57;
        case KEY_X: return 0x58;
        case KEY_Y: return 0x59;
        case KEY_Z: return 0x5A;

        case KEY_0: return 0x30;
        case KEY_1: return 0x31;
        case KEY_2: return 0x32;
        case KEY_3: return 0x33;
        case KEY_4: return 0x34;
        case KEY_5: return 0x35;
        case KEY_6: return 0x36;
        case KEY_7: return 0x37;
        case KEY_8: return 0x38;
        case KEY_9: return 0x39;

        case KEY_F1: return 0x70;
        case KEY_F2: return 0x71;
        case KEY_F3: return 0x72;
        case KEY_F4: return 0x73;
        case KEY_F5: return 0x74;
        case KEY_F6: return 0x75;
        case KEY_F7: return 0x76;
        case KEY_F8: return 0x77;
        case KEY_F9: return 0x78;
        case KEY_F10: return 0x79;
        case KEY_F11: return 0x7A;
        case KEY_F12: return 0x7B;

        case KEY_LEFTSHIFT: return 0xA0;
        case KEY_RIGHTSHIFT: return 0xA1;
        case KEY_LEFTCTRL: return 0xA2;
        case KEY_RIGHTCTRL: return 0xA3;
        case KEY_LEFTALT: return 0xA4;
        case KEY_RIGHTALT: return 0xA5;

        // add other

        default:
            return 0; // unknown key
    }
}

void restore_terminal(void) {
    if (ev_fd >= 0) {
        close(ev_fd);
        ev_fd = -1;
    }
}

bool keyboard_init(void) {
    glob_t glob_result;
    int ret = glob("/dev/input/by-id/*-event-kbd", 0, NULL, &glob_result);
    if (ret != 0 || glob_result.gl_pathc == 0) {
        fprintf(stderr, "no keyboard found!\n");
        globfree(&glob_result);
        return false;
    }

    ev_fd = open(glob_result.gl_pathv[0], O_RDONLY | O_NONBLOCK);
    globfree(&glob_result);

    if (ev_fd < 0) {
        perror("open keyboard");
        return false;
    }

    return true;
}

void keyboard_cleanup(void) {
    restore_terminal();
}

void keyboard_process(client_array_t *clients) {
    if (fds[clients->size + 1].revents & POLLIN) {
        struct input_event ev;
        ssize_t n = read(ev_fd, &ev, sizeof(ev));
        if (n == sizeof(ev)) {
            if (ev.type == EV_KEY) {
                int vk = linux_keycode_to_vk(ev.code);
                if (vk > 0 && vk < MAX_VK_CODE) {
                    if (ev.value == 1) {
                        keys_pressed[vk] = true;
                    } else if (ev.value == 0) {
                        keys_pressed[vk] = false;
                    }
                }
            }
        }
    }
}

bool mouse_init() {
    mouse_fd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);

    mouse_x = mode.hdisplay / 2;
    mouse_y = mode.vdisplay / 2;

    return mouse_fd >= 0 || (perror("mouse"), false);
}

void mouse_cleanup() {
    if (mouse_fd >= 0) close(mouse_fd);
}

void mouse_process(int *drag_window, int *dx, int *dy) {
    struct pollfd pfd = { .fd = mouse_fd, .events = POLLIN };
    int ret = poll(&pfd, 1, 0);
    if (ret <= 0) return;

    if (pfd.revents & POLLIN) {
        unsigned char d[3];
        if (read(mouse_fd, d, 3) != 3) return;

        bool left = d[0] & 1;
        int mx = (signed char)d[1];
        int my = -(signed char)d[2];

        int nx = mouse_x + mx;
        if (nx < 0) nx = 0;
        else if (nx >= (int)mode.hdisplay) nx = mode.hdisplay - 1;
        mouse_x = nx;

        int ny = mouse_y + my;
        if (ny < 0) ny = 0;
        else if (ny >= (int)mode.vdisplay) ny = mode.vdisplay - 1;
        mouse_y = ny;

        if (*drag_window != -1) {
            window_t *w = &windows[*drag_window];
            if (w->used) {
                w->x = mouse_x - *dx;
                w->y = mouse_y - *dy;

                if (w->x < 0) w->x = 0;
                if (w->y < 0) w->y = 0;
                if (w->x + w->w > (int)mode.hdisplay) w->x = mode.hdisplay - w->w;
                if (w->y + w->h > (int)mode.vdisplay) w->y = mode.vdisplay - w->h;
            }
        }

        if (left && !mouse_left) {
            mouse_left = true;
            *drag_window = -1;

            for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                window_t *w = &windows[i];
                if (!w->used) continue;

                if (mouse_x >= w->x && mouse_x < w->x + w->w &&
                    mouse_y >= w->y && mouse_y < w->y + w->h) {
                    
                    process_window_buttons(w, mouse_x, mouse_y);
                    if (!w->used) break;

                    *drag_window = i;
                    *dx = mouse_x - w->x;
                    *dy = mouse_y - w->y;
                    
                    for (int j = 0; j < MAX_WINDOWS; j++) {
                        windows[j].focused = false;
                    }
                    w->focused = true;
                    break;
                }
            }
        }
        else if (!left && mouse_left) {
            mouse_left = false;
            *drag_window = -1;
        }
    }
}
