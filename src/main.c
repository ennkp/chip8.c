#include "platform.h"

#define FATAL(...)                      \
    do {                                \
        fprintf(stderr, __VA_ARGS__);   \
        fprintf(stderr, "\n");          \
        exit(1);                        \
    } while (0)

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

// instruction decoding constants
#define OP(instruction)         (instruction >> 12)
#define X(instruction)          ((instruction & 0x0F00) >> 8)
#define Y(instruction)          ((instruction & 0x00F0) >> 4)
#define N(instruction)          (instruction & 0x000F)
#define NN(instruction)         (instruction & 0x00FF)
#define NNN(instruction)        (instruction & 0x0FFF)

// cycle constants
#define DEFAULT_FPS             60

#define KEYCODE_ESC             27

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
} Chip8;

static inline
void print_as_formatted_binary(uint8_t byte) {

    printf("%s%s%s%s%s%s%s%s",
            byte & (1 << 7) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT,
            byte & (1 << 6) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT,
            byte & (1 << 5) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT,
            byte & (1 << 4) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT,
            byte & (1 << 3) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT,
            byte & (1 << 2) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT,
            byte & (1 << 1) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT,
            byte & (1 << 0) ? SET_WHITE PIXEL_TEXT : SET_DEFAULT PIXEL_TEXT
          );
}

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
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            const uint8_t byte = c->display[y * DISPLAY_WIDTH + x];
            print_as_formatted_binary(byte);
        }
        printf(SET_DEFAULT"\n");
    }
    platform_cursor_up(DISPLAY_HEIGHT);
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
                              // TODO: optimize this?
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
                      // TODO: add flag to allow setting VX to the value of VY
                      case 0x6:
                          {
                              const uint8_t vf = c->v[x] & (1 << 0) ? 1 : 0;
                              c->v[x] >>= 1;
                              c->v[0xF] = vf;
                              DEBUG("v%u >>= 1 => %u, vf: %u", x, c->v[x], c->v[0xF]);
                              break;
                          }
                      case 0xE:
                          {
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
                      // TODO: add flag for using BXNN optionally
                      const uint16_t val = NNN(instruction);
                      c->i = val + c->v[0];
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
        case 0xF: {
                      const uint8_t reg = X(instruction);
                      switch (NN(instruction)) {
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
                      // TODO: add flag for incrementing c->i for supporting older games
                      case 0x55:
                          for (int i = 0; i <= reg; ++i) {
                              c->mem[c->i + i] = c->v[i];
                              DEBUG("Storing v%u (%u) at mem[%u]", i, c->v[i], i);
                          }
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

int main(int argc, const char **argv) {

    if (argc < 2)
        FATAL("No rom specified");

    Chip8 *c = &(Chip8){0};

    chip8_load_to_mem(c, FONT_DATA_OFFSET, FONT_DATA, sizeof(FONT_DATA));
    chip8_load_rom(c, argv[1]);

    // TODO: introduce flag for changing instructions_per_sec
    const uint32_t instructions_per_sec = 700;

    if (!platform_setup())
        FATAL("Failed to setup platform");

    while (1) {

        for (uint32_t i = 0; i < instructions_per_sec/DEFAULT_FPS; ++i) {
            uint16_t instruction = chip8_fetch(c);
            chip8_decode_execute(c, instruction);
        }

        switch (platform_get_keycode()) {
            case KEYCODE_ESC:
                goto quit;
        }

        c->delay_timer -= c->delay_timer != 0;
        c->sound_timer -= c->sound_timer != 0 ? platform_beep() : 0;
        chip8_display(c);

        platform_sleep(1000/DEFAULT_FPS);

    }

quit:
    platform_revert();
}
