/* logger.h */
#ifndef LOGGER_H
#define LOGGER_H
#include <stdbool.h>
void logger_init(unsigned max_per_sec, unsigned burst);
void logger_log(const char *tag, unsigned v1, unsigned v2);
void logger_flush_now(void); /* call from VSYNC or periodic task */
bool logger_is_enabled(void);
void logger_enable(bool en);
#endif
