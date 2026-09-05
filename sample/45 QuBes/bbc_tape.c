#include "bbc_tape.h"
#include <stddef.h>

void bbc_tape_init(bbc_tape_t *tape) {
    (void)tape;
}

void bbc_tape_free(bbc_tape_t *tape) {
    (void)tape;
}

void bbc_tape_set_irq_cb(bbc_tape_t *tape, void (*cb)(void *, bool), void *ctx) {
    (void)tape; (void)cb; (void)ctx;
}

void bbc_tape_set_motor(bbc_tape_t *tape, bool on) {
    (void)tape; (void)on;
}

uint8_t bbc_tape_read(bbc_tape_t *tape, uint8_t reg) {
    (void)tape; (void)reg;
    return 0x00; // Geen data / tape idle
}

void bbc_tape_write(bbc_tape_t *tape, uint8_t reg, uint8_t val) {
    (void)tape; (void)reg; (void)val;
}

void bbc_tape_tick(bbc_tape_t *tape, int32_t cycles) {
    (void)tape; (void)cycles;
}

int bbc_tape_load_uef(bbc_tape_t *tape, const char *path) {
    (void)tape; (void)path;
    return -1; // UEF niet ondersteund in bare-metal stub
}