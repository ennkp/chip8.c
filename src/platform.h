#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ESC                         "\x1b"
#define EXECUTE_ANSI_CODE(...)      printf(ESC __VA_ARGS__)
#define SET_WHITE                   ESC"[107m"
#define SET_DEFAULT                 ESC"[49m"

#ifdef __unix__

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>

typedef struct termios termios;

static termios original_termios = {0};

static inline
int enable_terminal_raw_mode(void) {
    // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        perror("Failed to get terminal attributes");
        return 0;
    }

    termios t = original_termios;

    // Modify the terminal settings to enable raw mode
    t.c_lflag &= !(ICANON | ECHO);

    // Set terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == -1) {
        perror("Failed to set terminal attributes");
        return 0;
    }

    EXECUTE_ANSI_CODE("[?25l"); // make cursor invisible

    return 1;
}

static inline
int disable_terminal_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &original_termios) == -1) {
        perror("Failed to restore terminal attributes");
        return 0;
    }
    EXECUTE_ANSI_CODE("[?25h"); // make cursor visible
    return 1;
}

static inline
int enable_stdin_non_blocking_mode(void) {
    int file_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (file_flags == -1) {
        perror("Failed to stdin get flags");
        return 0;
    }

    file_flags |= O_NONBLOCK;

    if (fcntl(STDIN_FILENO, F_SETFL, file_flags) == -1) {
        perror("Failed to stidn set flags");
        return 0;
    }

    return 1;
}

static inline
int disable_stdin_non_blocking_mode(void) {
    int file_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (file_flags == -1) {
        perror("Failed to restore stdin flags");
        return 0;
    }

    file_flags &= ~O_NONBLOCK;

    if (fcntl(STDIN_FILENO, F_SETFL, file_flags) == -1) {
        perror("Failed to set flags");
        return 0;
    }

    return 1;
}

static inline
int platform_setup(void) {

    if (!enable_terminal_raw_mode())
        return 0;

    if (!enable_stdin_non_blocking_mode())
        return 0;

    return 1;
}

static inline
void platform_clear_till_end_of_screen(void) {
    EXECUTE_ANSI_CODE("[0J");
}

static inline
void platform_sleep(uint32_t milliseconds) {
    usleep(milliseconds * 1000);
}

static inline
void platform_cursor_up(int n) {
    EXECUTE_ANSI_CODE("[%dA", n);
}

static inline
int platform_revert(void) {
    if (!disable_terminal_raw_mode())
        return 0;

    if (!disable_stdin_non_blocking_mode())
        return 0;

    platform_clear_till_end_of_screen();
    return 1;
}

static inline
char platform_get_keycode(void) {
    char buf[1];
    int num_read = read(STDIN_FILENO, buf, 1);
    if (num_read > 0) {
        return buf[0];
    }
    return 0;
}

static inline
int platform_beep(void) {
    // TODO: more reliable beep on linux
    printf("\a");
    return 1;
}

#else
#perroror only unix is supported for now
#endif

#endif // PLATFORM_H
