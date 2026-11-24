#ifndef LIBANTIDEBUG_H
#define LIBANTIDEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include <signal.h>

void antidebug_init(void);
void antidebug_start_monitor(void);
void antidebug_stop_monitor(void);
void process_disguise(void);
void process_set_low_priority(void);

#endif
