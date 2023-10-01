#include "platform.h"

// use
// #define DEBUG_LOG
// for printing logs to stderr.
// Pipe the output of stderr to a log like to not break the display
// Eg: chip8 <rom> 2> log

#ifdef DEBUG_LOG
#define DEBUG(...)                      \
    do {                                \
        fprintf(stderr, __VA_ARGS__);   \
        fprintf(stderr, "\n");          \
    } while (0)

#else
#define DEBUG(...)
#endif

#define BYTE_SIZE               8

// specification constants
#define REG_COUNT               16
#define MEM_SIZE                4096
#define STACK_SIZE              16
#define FONT_DATA_OFFSET        0x050
#define DISPLAY_WIDTH           8
#define DISPLAY_HEIGHT          32
#define DISPLAY_SIZE            (DISPLAY_HEIGHT * DISPLAY_WIDTH)
#define PROGRAM_START_OFFSET    512

// display constants
#define PIXEL_TEXT              "  "
#define ANSI_COLOR_FORMAT       ESC"[48;2;%u;%u;%um"
#define SET_WHITE_BG            ESC"[107m"
#define SET_DEFAULT_BG          ESC"[49m"
#define ANSI_COLOR_FORMAT_LEN   (sizeof(ANSI_COLOR_FORMAT) + 3 + sizeof(PIXEL_TEXT))
#define MAX_FRAME_BUFFER_SIZE   (DISPLAY_SIZE * (ANSI_COLOR_FORMAT_LEN * 2) * BYTE_SIZE)

// instruction decoding constants
#define OP(instruction)         (instruction >> 12)
#define X(instruction)          ((instruction & 0x0F00) >> 8)
#define Y(instruction)          ((instruction & 0x00F0) >> 4)
#define N(instruction)          (instruction & 0x000F)
#define NN(instruction)         (instruction & 0x00FF)
#define NNN(instruction)        (instruction & 0x0FFF)

// cycle constants
#define DEFAULT_FPS             60
#define DEFAULT_IPS             700

#define STRMATCH(flag_str)      (!strncmp(flag_str, arg, sizeof(flag_str)))

static uint8_t FONT_DATA[] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// set VY to VX before all the bit shifting operations
#define QUIRK_SHIFT_USE_VY      (1 << 0)

// BNNN uses the v0 register value as an offset to jump in the original COSMAC VIP
// BXNN version uses vX register value as the offset in modern implementations
#define QUIRK_BXNN              (1 << 1)

// Original COSMAC VIP incremented the index register on load/store operations
#define QUIRK_INC_INDEX         (1 << 2)

typedef struct {
    uint32_t     instructions_per_frame;
    uint32_t     frames_per_sec;
    uint32_t     quirks;
    const char   fg_text[ANSI_COLOR_FORMAT_LEN];
    const char   bg_text[ANSI_COLOR_FORMAT_LEN];
} Config;

typedef struct {
    uint8_t    mem[MEM_SIZE];
    uint16_t   pc;
    uint16_t   i;
    uint16_t   stack[STACK_SIZE];
    uint8_t    sp;
    uint8_t    delay_timer;
    uint8_t    sound_timer;
    uint8_t    v[REG_COUNT];
    uint8_t    display[DISPLAY_SIZE];
    KeyStates  keys;
    Config     config;
} Chip8;

static inline
void chip8_load_to_mem(Chip8 *c, uint32_t offset, void *data, size_t size) {
    memcpy(&c->mem[offset], data, size);
}

static inline
void chip8_load_rom(Chip8 *c, const char *file_path) {
    printf("Loading rom: %s\n", file_path);
    FILE *file = fopen(file_path, "rb");
    if (!file)
        FATAL("Failed to open file: %s", file_path);

    fseek(file, 0L, SEEK_END);
    const size_t file_size = ftell(file);
    rewind(file);

    c->pc = PROGRAM_START_OFFSET;
    fread(&c->mem[c->pc], sizeof(char), file_size, file);

    fclose(file);
}


static inline
uint16_t chip8_fetch(Chip8 *c) {
    if (c->pc + 2 > MEM_SIZE) {
        DEBUG("Reached end of memory: %u", c->pc);
        platform_revert();
        exit(1);
    }

    uint16_t instruction = c->mem[c->pc] << BYTE_SIZE | c->mem[c->pc+1];
    c->pc += 2;
    DEBUG("instruction: %04x", instruction);
    return instruction;
}

static inline
void chip8_clear_screen(Chip8 *c) {
    memset(c->display, 0, DISPLAY_SIZE);
}

static inline
void chip8_load_pixels(Chip8 *c, uint8_t x, uint8_t y, uint8_t h) {

    x %= DISPLAY_WIDTH * BYTE_SIZE;
    y %= DISPLAY_HEIGHT;

    const uint8_t *src = &c->mem[c->i];

    const uint8_t start_bit = x % BYTE_SIZE;
    const uint8_t rhs_bits = BYTE_SIZE - start_bit;

    uint8_t vf = 0;

    for (uint8_t i = 0; i < h; ++i) {
        const uint8_t sprite_row = src[i];

        const uint8_t first_byte_mask = sprite_row >> start_bit;

        const uint32_t idx = (y+i) * DISPLAY_WIDTH + x/DISPLAY_WIDTH;
        if (idx >= DISPLAY_SIZE)
            break;

        vf = vf || (c->display[idx] & first_byte_mask);

        c->display[idx] ^= first_byte_mask;

        const uint8_t second_byte_mask = (sprite_row & (0xFF >> rhs_bits)) << rhs_bits;

        const uint32_t not_offscreen = (idx+1) % DISPLAY_WIDTH;

        vf = not_offscreen ?
            vf || (c->display[idx+1] & second_byte_mask) :
            vf;

        c->display[idx+1] ^= not_offscreen ?
            second_byte_mask :
            0;
    }

    c->v[0xF] = vf;

}


static inline
void chip8_display(Chip8 *c) {
    char frame_buffer[MAX_FRAME_BUFFER_SIZE];
    size_t char_count = 0;
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            const uint8_t byte = c->display[y * DISPLAY_WIDTH + x];
            char_count += snprintf(&frame_buffer[char_count], MAX_FRAME_BUFFER_SIZE, "%s%s%s%s%s%s%s%s",
                    byte & (1 << 7) ? c->config.fg_text : c->config.bg_text,
                    byte & (1 << 6) ? c->config.fg_text : c->config.bg_text,
                    byte & (1 << 5) ? c->config.fg_text : c->config.bg_text,
                    byte & (1 << 4) ? c->config.fg_text : c->config.bg_text,
                    byte & (1 << 3) ? c->config.fg_text : c->config.bg_text,
                    byte & (1 << 2) ? c->config.fg_text : c->config.bg_text,
                    byte & (1 << 1) ? c->config.fg_text : c->config.bg_text,
                    byte & (1 << 0) ? c->config.fg_text : c->config.bg_text
                    );
        }
        char_count += snprintf(&frame_buffer[char_count], MAX_FRAME_BUFFER_SIZE, SET_DEFAULT_BG PLATFORM_EOL);
    }
    platform_write_to_console(frame_buffer, char_count, DISPLAY_HEIGHT);
}

static inline
void chip8_decode_execute(Chip8 *c, uint16_t instruction) {
    switch (OP(instruction)) {
        case 0x0: {
                      switch (NN(instruction)) {
                      case 0xE0:
                          DEBUG("Clear screen");
                          chip8_clear_screen(c);
                          break;
                      case 0xEE:
                          {
                              const uint16_t jmp_pos = c->stack[--c->sp];
                              DEBUG("Return to %u", jmp_pos);
                              c->pc = jmp_pos;
                              break;
                          }
                      }
                      break;
                  }
        case 0x1: {
                      const uint16_t jmp_pos = NNN(instruction);
                      DEBUG("Jump to %u", jmp_pos);
                      c->pc = jmp_pos;
                      break;
                  }
        case 0x2: {
                      const uint16_t jmp_pos = NNN(instruction);
                      DEBUG("Push to stack: %u -> call %u", c->pc, jmp_pos);
                      c->stack[c->sp++] = c->pc;
                      c->pc = jmp_pos;
                      break;
                  }
        case 0x3: {
                      const uint8_t reg = X(instruction);
                      const uint8_t val = NN(instruction);
                      DEBUG("Skip if v%u (%u) == %u", reg, c->v[reg], val);
                      c->pc += (c->v[reg] == val) * 2;
                      break;
                  }
        case 0x4: {
                      const uint8_t reg = X(instruction);
                      const uint8_t val = NN(instruction);
                      DEBUG("Skip if v%u (%u) != %u", reg, c->v[reg], val);
                      c->pc += (c->v[reg] != val) * 2;
                      break;
                  }
        case 0x5: {
                      const uint8_t x = X(instruction);
                      const uint8_t y = Y(instruction);
                      DEBUG("Skip if v%u (%u) == v%u (%u)", x, c->v[x], y, c->v[y]);
                      c->pc += (c->v[x] == c->v[y]) * 2;
                      break;
                  }
        case 0x9: {
                      const uint8_t x = X(instruction);
                      const uint8_t y = Y(instruction);
                      DEBUG("Skip if v%u (%u) != v%u (%u)", x, c->v[x], y, c->v[y]);
                      c->pc += (c->v[x] != c->v[y]) * 2;
                      break;
                  }
        case 0x6: {
                      const uint8_t reg = X(instruction);
                      const uint16_t val = NN(instruction);
                      c->v[reg] = val;
                      DEBUG("v[%u] = %u", reg, val);
                      break;
                  }
        case 0x7: {
                      const uint8_t reg = X(instruction);
                      const uint16_t val = NN(instruction);
                      c->v[reg] += val;
                      DEBUG("v[%u] += %u", reg, val);
                      break;
                  }
        case 0x8: {
                      const uint8_t x = X(instruction);
                      const uint8_t y = Y(instruction);
                      switch (N(instruction)) {
                      case 0x0:
                          c->v[x] = c->v[y];
                          DEBUG("v%u = v%u (%x)", x, y, c->v[y]);
                          break;
                      case 0x1:
                          c->v[x] |= c->v[y];
                          DEBUG("v%u |= v%u (%x) => %x", x, y, c->v[y], c->v[x]);
                          break;
                      case 0x2:
                          c->v[x] &= c->v[y];
                          DEBUG("v%u &= v%u (%x) => %x", x, y, c->v[y], c->v[x]);
                          break;
                      case 0x3:
                          c->v[x] ^= c->v[y];
                          DEBUG("v%u ^= v%u (%x) => %x", x, y, c->v[y], c->v[x]);
                          break;
                      case 0x4:
                          {
                              const uint16_t sum = (c->v[x] + c->v[y]);
                              c->v[x] = sum;
                              c->v[0xF] = sum > 0xFF;
                              DEBUG("v%u += v%u (%x) => %x", x, y, c->v[y], c->v[x]);
                              break;
                          }
                      case 0x5:
                          {
                              const uint8_t vf = c->v[x] >= c->v[y];
                              c->v[x] -= c->v[y];
                              c->v[0xF] = vf;
                              DEBUG("v%u -= v%u (%x) => %x", x, y, c->v[y], c->v[x]);
                              break;
                          }
                      case 0x7:
                          {
                              const uint8_t vf = c->v[x] <= c->v[y];
                              c->v[x] = c->v[y] - c->v[x];
                              c->v[0xF] = vf;
                              DEBUG("v%u = v%u - v%u (%x) => %x", x, y, x, c->v[y], c->v[x]);
                              break;
                          }
                      case 0x6:
                          {
                              if (c->config.quirks & QUIRK_SHIFT_USE_VY)
                                  c->v[x] = c->v[y];

                              const uint8_t vf = c->v[x] & (1 << 0) ? 1 : 0;
                              c->v[x] >>= 1;
                              c->v[0xF] = vf;
                              DEBUG("v%u >>= 1 => %u, vf: %u", x, c->v[x], c->v[0xF]);
                              break;
                          }
                      case 0xE:
                          {
                              if (c->config.quirks & QUIRK_SHIFT_USE_VY)
                                  c->v[x] = c->v[y];

                              const uint8_t vf = c->v[x] & (1 << 7) ? 1 : 0;
                              c->v[x] <<= 1;
                              c->v[0xF] = vf;
                              DEBUG("v%u <<= 1 => %u, vf: %u", x, c->v[x], c->v[0xF]);
                              break;
                          }
                      }
                      break;
                  }
        case 0xA: {
                      const uint16_t val = NNN(instruction);
                      c->i = val;
                      DEBUG("i = %u", c->i);
                      break;
                  }
        case 0xB: {
                      const uint8_t reg = c->config.quirks & QUIRK_BXNN ?
                                                X(instruction) :
                                                0;
                      const uint16_t val = NNN(instruction);
                      c->i = val + c->v[reg];
                      DEBUG("i = %u", c->i);
                      break;
                  }
        case 0xC: {
                      const uint8_t reg = X(instruction);
                      const uint16_t val = NN(instruction);
                      const uint8_t r = rand();
                      c->v[reg] = r & val;
                      DEBUG("Rand v%u = %u & %u", reg, r, val);
                      break;
                  }
        case 0xD: {
                      const uint8_t x = c->v[X(instruction)];
                      const uint8_t y = c->v[Y(instruction)];
                      const uint8_t h = N(instruction);
                      DEBUG("draw %u, %u, %u", x, y, h);
                      chip8_load_pixels(c, x, y, h);
                      break;
                  }
        case 0xE: {
                      const uint8_t reg = X(instruction);
                      switch (NN(instruction)) {
                      case 0x9E:
                          c->pc += KEY_DOWN(c->keys, c->v[reg]) * 2;
                          DEBUG("Skip if %x pressed", c->v[reg]);
                          break;
                      case 0xA1:
                          c->pc += !KEY_DOWN(c->keys, c->v[reg]) * 2;
                          DEBUG("Skip if %x not pressed", c->v[reg]);
                          break;
                      }
                      break;
                  }
        case 0xF: {
                      const uint8_t reg = X(instruction);
                      switch (NN(instruction)) {
                      case 0x0A:
                          {
                              DEBUG("Wait for key press and release");
                              static KeyStates store = 0;

                              if (store > c->keys) {
                                  uint16_t k = 0;

                                  const KeyStates diff = store ^ c->keys;
                                  while (k < CKEY_ESC && !KEY_DOWN(diff, k))
                                      k++;

                                  DEBUG("Key pressed and released: %s\n", get_chip8key_name(k));
                                  c->v[reg] = k;
                                  store = 0;
                              } else {
                                  store = c->keys;
                                  c->pc -= 2;
                              }

                              break;
                          }
                      case 0x07:
                          c->v[reg] = c->delay_timer;
                          DEBUG("v%u = delay_timer (%u)", reg, c->delay_timer);
                          break;
                      case 0x15:
                          c->delay_timer = c->v[reg];
                          DEBUG("delay_timer = v%u (%u)", reg, c->v[reg]);
                          break;
                      case 0x18:
                          c->delay_timer = c->v[reg];
                          DEBUG("sound_timer = v%u (%u)", reg, c->v[reg]);
                          break;
                      case 0x1E:
                          c->v[0xF] =
                              (c->i += c->v[reg]) > MEM_SIZE;
                          DEBUG("i += v%u: %u, vf: %u", reg, c->v[reg], c->v[0xF]);
                          break;
                      case 0x29:
                          c->i = N(c->v[reg]) * 5 + FONT_DATA_OFFSET;
                          DEBUG("i = %x", N(c->v[reg]));
                          break;
                      case 0x33:
                          {
                              uint8_t d = c->v[reg];
                              const uint8_t d3 = d % 10;
                              const uint8_t d2 = (d /= 10) % 10;
                              const uint8_t d1 = (d /= 10);
                              c->mem[c->i] = d1;
                              c->mem[c->i + 1] = d2;
                              c->mem[c->i + 2] = d3;
                              DEBUG("d: %u -> (%u, %u, %u)", c->v[reg], d1, d2, d3);
                              break;
                          }
                      case 0x55:
                          for (int i = 0; i <= reg; ++i) {
                              c->mem[c->i + i] = c->v[i];
                              DEBUG("Storing v%u (%u) at mem[%u]", i, c->v[i], i);
                          }
                          if (c->config.quirks & QUIRK_INC_INDEX)
                              c->i += reg + 1;
                          break;
                      case 0x65:
                          for (int i = 0; i <= reg; ++i) {
                              c->v[i] = c->mem[c->i + i];
                              DEBUG("Loading v%u from mem[%u] (%u)", i, i, c->mem[i]);
                          }
                          break;
                      }
                      break;
                  }
        default:  {
                      DEBUG("Unrecognized instruction: %04x", instruction);
                  }
    }
}

static inline
const char *usage(void) {
    return
        "Usage: chip8 <rom> [options]\n"
        "Options:\n"
        "    --help, -h             Display this information.\n"
        "    -ips <arg>             Instructions per second to use, must be greater or equal to FPS (Default: " STRINGIFY(DEFAULT_IPS) ").\n"
        "    -fps <arg>             Frames per second to use (Default: " STRINGIFY(DEFAULT_FPS) ").\n"
        "    -qshift-use-vy         Quirk: set VY to VX before bit shifting operations.\n"
        "    -qbxnn                 Quirk: use BXNN version of BNNN (Jump with offset) operation.\n"
        "    -qinc-index            Quirk: increment index register on memory load/store operations.\n"
        "    -fg <hexcode>          Set pixel 'on' color (foreground). Eg: FF0000 for red\n"
        "    -bg <hexcode>          Set pixel 'off' color (background). Eg: 00FF00 for green\n"
    ;
}

static inline
void generate_ansi_coded_text(int32_t color, const char *out_text, const char *default_text) {
    char color_format_buffer[ANSI_COLOR_FORMAT_LEN];
    size_t str_len = 0;

    if (color != -1)
        str_len = snprintf(color_format_buffer,
                ANSI_COLOR_FORMAT_LEN,
                ANSI_COLOR_FORMAT PIXEL_TEXT,
                color >> 16 & 0xFF,
                color >>  8 & 0xFF,
                color >>  0 & 0xFF);
    else
        str_len = snprintf(color_format_buffer,
                ANSI_COLOR_FORMAT_LEN,
                default_text);
    memcpy((char*)out_text, color_format_buffer, str_len);
}

static inline
void parse_cmdline_args(Chip8 *c, CmdLineArgs *args, const char **rom) {

    uint32_t
        ips = 0,
        fps = 0,
        quirks = 0;
    int32_t
        fgc = -1,
        bgc = -1;

    const char instructions_per_sec[]   = "-ips";
    const char frames_per_sec[]         = "-fps";
    const char quirk_shift_use_vy[]     = "-qshift-use-vy";
    const char quirk_bxnn[]             = "-qbxnn";
    const char quirk_inc_index[]        = "-qinc-index";
    const char fgcolor[]                = "-fg";
    const char bgcolor[]                = "-bg";
    const char help1[]                  = "--help";
    const char help2[]                  = "-h";

    const char *arg;
    while ((arg = next_arg(args))) {

        if (STRMATCH(instructions_per_sec))
            ips = parse_option_value_to_uint(args, 10);

        else if (STRMATCH(frames_per_sec))
            fps = parse_option_value_to_uint(args, 10);

        else if (STRMATCH(fgcolor))
            fgc = parse_option_value_to_uint(args, 16);

        else if (STRMATCH(bgcolor))
            bgc = parse_option_value_to_uint(args, 16);

        else if (STRMATCH(quirk_shift_use_vy))
            quirks |= QUIRK_SHIFT_USE_VY;

        else if (STRMATCH(quirk_inc_index))
            quirks |= QUIRK_INC_INDEX;

        else if (STRMATCH(quirk_bxnn))
            quirks |= QUIRK_BXNN;

        else if (STRMATCH(help1) || STRMATCH(help2)) {
            printf("%s", usage());
            exit(0);
        }

        else if (!strncmp("-", arg, 1))
            FATAL("Unrecognized command-line option: %s", arg);

        else
            *rom = arg;

    }

    generate_ansi_coded_text(fgc, c->config.fg_text, SET_WHITE_BG PIXEL_TEXT);
    generate_ansi_coded_text(bgc, c->config.bg_text, SET_DEFAULT_BG PIXEL_TEXT);

    c->config.instructions_per_frame = ips;
    c->config.frames_per_sec = fps;
    c->config.quirks = quirks;

    if (!*rom)
        FATAL("No rom specified");
}

int main(int argc, const char **argv) {

    if (argc < 2)
        FATAL("No rom specified");

    Chip8 *c = &(Chip8){0};

    {
        CmdLineArgs args = init_args_list(argc, argv);
        const char *rom = NULL;
        parse_cmdline_args(c, &args, &rom);
        chip8_load_to_mem(c, FONT_DATA_OFFSET, FONT_DATA, sizeof(FONT_DATA));
        chip8_load_rom(c, rom);
    }

    const uint32_t instructions_per_sec = c->config.instructions_per_frame ?
        c->config.instructions_per_frame : DEFAULT_IPS;
    const uint32_t frames_per_sec = c->config.frames_per_sec ?
        c->config.frames_per_sec : DEFAULT_FPS;

    printf("ips: %u/sec\n", instructions_per_sec);
    printf("fps: %u/sec\n", frames_per_sec);
    printf("fg:  %s"SET_DEFAULT_BG"\n"
           "bg:  %s"SET_DEFAULT_BG"\n\n",
            c->config.fg_text,
            c->config.bg_text);


    if (instructions_per_sec < frames_per_sec)
        FATAL("Instructions per second cannot be less than Frames per second. Use -h for more details");

    if (!platform_setup())
        FATAL("Failed to setup platform");

    while (1) {

        for (uint32_t i = 0; i < instructions_per_sec/frames_per_sec; ++i) {
            uint16_t instruction = chip8_fetch(c);
            chip8_decode_execute(c, instruction);
        }

        c->delay_timer -= c->delay_timer != 0;
        c->sound_timer -= c->sound_timer != 0 ? platform_beep() : 0;
        chip8_display(c);

        if (platform_set_keystates(&c->keys)) {
            if (KEY_DOWN(c->keys, CKEY_ESC))
                goto quit;
        }

        platform_sleep(1000/frames_per_sec);

    }

quit:
    platform_revert();
}
