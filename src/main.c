#include "platform.h"

#define FATAL(...)                      \
    do {                                \
        fprintf(stderr, __VA_ARGS__);   \
        fprintf(stderr, "\n");          \
        exit(1);                        \
    } while (0)

#ifdef DEBUG_LOG
#define DEBUG(...)              printf(__VA_ARGS__)
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
#define X(instruction)          ((instruction & 0x0F00) >> 8)
#define Y(instruction)          ((instruction & 0x00F0) >> 4)
#define N(instruction)          (instruction & 0x000F)
#define NNN(instruction)        (instruction & 0x0FFF)

// cycle constants
#define TARGET_FRAME_RATE       60
#define FRAME_TIME              (1000/TARGET_FRAME_RATE)

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
    uint32_t   pc;
    uint16_t   i;
    uint16_t   stack[STACK_SIZE];
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
        DEBUG("Reached end of memory: %u\n", c->pc);
        exit(1);
    }

    uint16_t instruction = c->mem[c->pc] << BYTE_SIZE | c->mem[c->pc+1];
    c->pc += 2;
    DEBUG("instruction: %x\n", instruction);
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
void chip8_decode_execute(Chip8 *c, uint16_t instruction) {
    switch (instruction >> 12) {
        case 0x0: {
                      if (instruction == 0x0E00) {
                          DEBUG("Clear screen\n");
                          chip8_clear_screen(c);
                      }
                      break;
                  }
        case 0x1: {
                      const uint32_t jmp_pos = NNN(instruction);
                      DEBUG("Jump to %u\n", jmp_pos);
                      c->pc = jmp_pos;
                      break;
                  }
        case 0x6: {
                      const uint8_t reg = X(instruction);
                      const uint8_t val = N(instruction);
                      c->v[reg] = val;
                      DEBUG("v[%u] = %u\n", reg, val);
                      break;
                  }
        case 0x7: {
                      const uint8_t reg = X(instruction);
                      const uint8_t val = N(instruction);
                      c->v[reg] += val;
                      DEBUG("v[%u] += %u\n", reg, val);
                      break;
                  }
        case 0xA: {
                      const uint16_t val = NNN(instruction);
                      c->i = val;
                      DEBUG("i = %u\n", val);
                      break;
                  }
        case 0xD: {
                      const uint8_t x = c->v[X(instruction)];
                      const uint8_t y = c->v[Y(instruction)];
                      const uint8_t h = N(instruction);
                      DEBUG("draw %u, %u, %u\n", x, y, h);
                      chip8_load_pixels(c, x, y, h);
                      break;
                  }
        default:  {
                      DEBUG("Unrecognized instruction: %u\n", instruction);
                  }
    }
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
}

int main(int argc, const char **argv) {

    if (argc < 2)
        FATAL("No rom specified");

    Chip8 *c = &(Chip8){0};

    chip8_load_to_mem(c, FONT_DATA_OFFSET, FONT_DATA, sizeof(FONT_DATA));
    chip8_load_rom(c, argv[1]);

    if (!platform_setup())
        FATAL("Failed to setup platform");

    while (1) {

        uint16_t instruction = chip8_fetch(c);
        chip8_decode_execute(c, instruction);
        
        chip8_display(c);

        platform_cursor_up(DISPLAY_HEIGHT);
        platform_sleep(FRAME_TIME);

        switch (platform_get_keycode()) {
        case KEYCODE_ESC:
            goto quit;
        }
    }

quit:
    platform_revert();
}
