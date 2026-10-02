/* logger.c */
#include "logger.h"
#include <string.h>
#include <stdint.h>

extern void bbc_debug_log(const char *msg, unsigned val1, unsigned val2);

static volatile unsigned g_millis = 0;

/* Call this from a 1 ms timer ISR or from your main loop every ms */
void platform_millis_tick(void) { g_millis++; }

/* Return current ms */
unsigned platform_millis(void) { return g_millis; }

/* Config */
static unsigned g_max_per_sec = 50; /* steady rate */
static unsigned g_burst = 100;      /* burst capacity */

/* Token bucket state */
static float tokens = 0.0f;
static unsigned last_tick_ms = 0;

/* Simple ring buffer of compact records */
#define LOG_BUF_SIZE 256
typedef struct { const char *tag; unsigned v1; unsigned v2; } logrec_t;
static logrec_t buf[LOG_BUF_SIZE];
static unsigned buf_head = 0;
static unsigned buf_tail = 0;
static bool enabled = true;

/* Millisecond clock provider — implement per platform */
extern unsigned platform_millis(void);

/* init */
void logger_init(unsigned max_per_sec, unsigned burst) {
    g_max_per_sec = max_per_sec ? max_per_sec : 50;
    g_burst = burst ? burst : (g_max_per_sec * 2);
    tokens = (float)g_burst;
    last_tick_ms = platform_millis();
}

/* enqueue record (non-blocking) */
void logger_log(const char *tag, unsigned v1, unsigned v2) {
    if (!enabled) return;
    unsigned next = (buf_head + 1) & (LOG_BUF_SIZE - 1);
    if (next == buf_tail) {
        /* buffer full: drop oldest to make room */
        buf_tail = (buf_tail + 1) & (LOG_BUF_SIZE - 1);
    }
    buf[buf_head].tag = tag;
    buf[buf_head].v1 = v1;
    buf[buf_head].v2 = v2;
    buf_head = next;
}

/* internal: try to emit one record if tokens available */
static void emit_one_if_allowed(void) {
    unsigned now = platform_millis();
    unsigned dt = (now >= last_tick_ms) ? (now - last_tick_ms) : 0;
    if (dt) {
        /* refill tokens */
        tokens += ((float)g_max_per_sec) * ((float)dt / 1000.0f);
        if (tokens > (float)g_burst) tokens = (float)g_burst;
        last_tick_ms = now;
    }
    if (tokens >= 1.0f && buf_tail != buf_head) {
        /* pop and send */
        logrec_t r = buf[buf_tail];
        buf_tail = (buf_tail + 1) & (LOG_BUF_SIZE - 1);
        tokens -= 1.0f;
        bbc_debug_log(r.tag, r.v1, r.v2);
    }
}

/* flush loop: call periodically (e.g., from VSYNC or main loop) */
void logger_flush_now(void) {
    /* emit up to burst or until buffer empty */
    unsigned emitted = 0;
    unsigned limit = g_burst;
    while (buf_tail != buf_head && emitted < limit) {
        emit_one_if_allowed();
        emitted++;
    }
}

/* enable/disable */
void logger_enable(bool en) { enabled = en; }
bool logger_is_enabled(void) { return enabled; }
