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

typedef enum {
    CKEY_1 = 0x1,
    CKEY_2 = 0x2,
    CKEY_3 = 0x3,
    CKEY_4 = 0xC,
    CKEY_Q = 0x4,
    CKEY_W = 0x5,
    CKEY_E = 0x6,
    CKEY_R = 0xD,
    CKEY_A = 0x7,
    CKEY_S = 0x8,
    CKEY_D = 0x9,
    CKEY_F = 0xE,
    CKEY_Z = 0xA,
    CKEY_X = 0x0,
    CKEY_C = 0xB,
    CKEY_V = 0xF,

    // for emulator exit
    CKEY_ESC,
} Chip8Key;

typedef uint32_t KeyStates;
static inline const char *get_chip8key_name(Chip8Key key);
static uint8_t keys[256];

#ifdef __unix__
#include <termios.h>
#include <unistd.h>
#include <X11/XKBlib.h>

typedef struct termios termios;

static termios  original_termios = {0};
static Display *x11display = NULL;
static Window   terminal_emulator_window = 0;

static inline
int setup_x11_keyboard(void) {

    if (!(x11display = XOpenDisplay(NULL))) {
        fprintf(stderr, "Failed to open X11 display: %s\n", XDisplayName(NULL));
        return 0;
    }

    int revert_to;
    XGetInputFocus(x11display, &terminal_emulator_window, &revert_to);
    if (terminal_emulator_window == 0) {
        fprintf(stderr, "Failed to get terminal emulator X11 window\n");
        return 0;
    }

    XSelectInput(x11display, terminal_emulator_window, KeyPressMask | KeyReleaseMask );

    if (!XkbSetDetectableAutoRepeat(x11display, True, NULL)) {
        fprintf(stderr, "Failed to set detectable autorepeat\n");
        return 0;
    }

    struct {
        const char* key_name;
        Chip8Key key;
    } name_keys[] = {
        { .key_name = "ESC",  .key = CKEY_ESC},
        { .key_name = "AE01", .key = CKEY_1},
        { .key_name = "AE02", .key = CKEY_2},
        { .key_name = "AE03", .key = CKEY_3},
        { .key_name = "AE04", .key = CKEY_4},
        { .key_name = "AD01", .key = CKEY_Q},
        { .key_name = "AD02", .key = CKEY_W},
        { .key_name = "AD03", .key = CKEY_E},
        { .key_name = "AD04", .key = CKEY_R},
        { .key_name = "AC01", .key = CKEY_A},
        { .key_name = "AC02", .key = CKEY_S},
        { .key_name = "AC03", .key = CKEY_D},
        { .key_name = "AC04", .key = CKEY_F},
        { .key_name = "AB01", .key = CKEY_Z},
        { .key_name = "AB02", .key = CKEY_X},
        { .key_name = "AB03", .key = CKEY_C},
        { .key_name = "AB04", .key = CKEY_V},
    };

    XkbDescPtr xkbdesc = XkbGetMap(x11display, 0, XkbUseCoreKbd);
    XkbGetNames(x11display, XkbKeyNamesMask, xkbdesc);

    memset(keys, -1, sizeof(keys));

    for(KeyCode i = xkbdesc->min_key_code; i < xkbdesc->max_key_code; ++i) {
        for(size_t j = 0; j < sizeof(name_keys)/sizeof(name_keys[0]); ++j) {
            if(strncmp(name_keys[j].key_name, xkbdesc->names->keys[i].name, XkbKeyNameLength) == 0) {
                keys[i] = name_keys[j].key;
                break;
            }
        }
    }

    XkbFreeNames(xkbdesc, XkbNamesMask, True);
    XkbFreeKeyboard(xkbdesc, 0, True);

    return 1;
}

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

#elif defined _WIN32

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

#endif // end of OS specific stuff

static inline
int platform_setup(void) {

    if (!enable_terminal_raw_mode())
        return 0;

#ifdef __unix__
    if (!setup_x11_keyboard())
        return 0;
#elif defined _WIN32
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
#elif defined _WIN32
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
    XCloseDisplay(x11display);
    tcflush(0, TCIFLUSH);
#elif defined _WIN32
    if (!disable_stdout_ansi_code_processing())
        return 0;
#endif

    EXECUTE_ANSI_CODE("[0J");   // clear till end of screen
    EXECUTE_ANSI_CODE("[?25h"); // make cursor visible
    return 1;
}

#define KEY_FLAG(key)                ((KeyStates)1 << (key))
#define KEY_DOWN(keystate, key)      ((keystate & KEY_FLAG(key)) == KEY_FLAG(key))

static inline
int platform_set_keystates(KeyStates *keystates) {
    int state_changed = 0;

#ifdef __unix__

    while (XPending(x11display)) {
        XEvent event;
        XNextEvent(x11display, &event);

        const uint32_t xkeycode = event.xkey.keycode;

        const Chip8Key key = keys[xkeycode];

        if (key == (uint8_t)-1 ||
           (event.type == KeyPress && KEY_DOWN(*keystates, key)))
            continue;

        *keystates ^= KEY_FLAG(key);

        state_changed = 1;
    }

#elif defined _WIN32
#endif

    return state_changed;
}

static inline
int platform_beep(void) {
    printf("\a");
    return 1;
}

static inline
const char *get_chip8key_name(Chip8Key key) {
    switch (key) {
    case CKEY_1:    return "1";
    case CKEY_2:    return "2";
    case CKEY_3:    return "3";
    case CKEY_4:    return "C";
    case CKEY_Q:    return "4";
    case CKEY_W:    return "5";
    case CKEY_E:    return "6";
    case CKEY_R:    return "D";
    case CKEY_A:    return "7";
    case CKEY_S:    return "8";
    case CKEY_D:    return "9";
    case CKEY_F:    return "E";
    case CKEY_Z:    return "A";
    case CKEY_X:    return "0";
    case CKEY_C:    return "B";
    case CKEY_V:    return "F";
    case CKEY_ESC:  return "ESC";
    }
    return "<unknown>";
}

#endif // PLATFORM_H
