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


    return 1;
}

static inline
int disable_terminal_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &original_termios) == -1) {
        perror("Failed to restore terminal attributes");
        return 0;
    }
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


#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>

static HANDLE hStdin = NULL;
static DWORD original_in_mode = 0;
static HANDLE hStdout = NULL;
static DWORD original_out_mode = 0;

static inline
int enable_terminal_raw_mode(void) {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);

    if (hStdin == INVALID_HANDLE_VALUE) {
        perror("Failed to get stdin handle");
        return 0;
    }

    if (!GetConsoleMode(hStdin, &original_in_mode)) {
        perror("Failed to get console input mode");
        return 0;
    }

    DWORD mode = original_in_mode;

    mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);

    if (!SetConsoleMode(hStdin, mode)) {
        perror("Failed to set console input mode");
        return 0;
    }

    return 1;
}

static inline
int disable_terminal_raw_mode(void) {
    if (!SetConsoleMode(hStdin, original_in_mode)) {
        perror("Failed to restore original console input mode");
        return 0;
    }
    return 1;
}

static inline
int enable_stdout_ansi_code_processing(void) {

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (hStdout == INVALID_HANDLE_VALUE) {
        perror("Failed to get stdout hanlde");
        return 0;
    }

    if (!GetConsoleMode(hStdout, &original_out_mode)) {
        perror("Failed to get out console mode");
        return 0;
    }

    DWORD mode = original_out_mode;

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;

    if (!SetConsoleMode(hStdout, mode)) {
        perror("Failed to get out console mode");
        return 0;
    }

    return 1;
}

static inline
int disable_stdout_ansi_code_processing(void) {
    if (!SetConsoleMode(hStdout, original_out_mode)) {
        perror("Failed to restore out console mode");
        return 0;
    }
    return 1;
}

#endif // end of OS specific terminal mode setting

static inline
int platform_setup(void) {

    if (!enable_terminal_raw_mode())
        return 0;

#ifdef __unix__
    if (!enable_stdin_non_blocking_mode())
        return 0;
#else
    if (!enable_stdout_ansi_code_processing())
        return 0;
#endif

    EXECUTE_ANSI_CODE("[?25l"); // make cursor invisible
    return 1;
}

static inline
void platform_sleep(uint32_t milliseconds) {
#ifdef __unix__
    usleep(milliseconds * 1000);
#else
    Sleep(milliseconds);
#endif
}

static inline
void platform_cursor_up(int n) {
    EXECUTE_ANSI_CODE("[%dA", n);
}

static inline
int platform_revert(void) {

    if (!disable_terminal_raw_mode())
        return 0;

#ifdef __unix__
    if (!disable_stdin_non_blocking_mode())
        return 0;
#else
    if (!disable_stdout_ansi_code_processing())
        return 0;
#endif

    EXECUTE_ANSI_CODE("[0J");   // clear till end of screen
    EXECUTE_ANSI_CODE("[?25h"); // make cursor visible
    return 1;
}

static inline
char platform_get_keycode(void) {

#ifdef __unix__

    char buf[1];
    int num_read = read(STDIN_FILENO, buf, 1);
    if (num_read > 0) {
        return buf[0];
    }
    return 0;

#else

    if (!_kbhit())
        return 0;

    return _getch();

#endif
}

static inline
int platform_beep(void) {
    printf("\a");
    return 1;
}

#endif // PLATFORM_H
