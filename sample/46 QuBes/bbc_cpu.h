#pragma once

#define FLAG_C       0x01
#define FLAG_Z       0x02
#define FLAG_I       0x04
#define FLAG_D       0x08
#define FLAG_B       0x10
#define FLAG_U       0x20
#define FLAG_V       0x40
#define FLAG_N       0x80

#define NMI_VECTOR   0xFFFA
#define IRQ_VECTOR   0xFFFE

struct bbc_machine;
typedef struct bbc_machine bbc_machine_t;

void bbc_cpu_init(bbc_machine_t *m);
void bbc_cpu_reset(bbc_machine_t *m);
void bbc_cpu_destroy(bbc_machine_t *m);
void bbc_cpu_nmi(bbc_machine_t *m);
void bbc_cpu_irq(bbc_machine_t *m);
void bbc_cpu_clear_irq(bbc_machine_t *m);
int bbc_cpu_step(bbc_machine_t *m);
