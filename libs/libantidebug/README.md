# libantidebug

Anti-debugging, process disguise, and integrity checks.

## API

```c
void antidebug_init(void);            // compute .text checksum at startup
void antidebug_start_monitor(void);   // background thread: tracer + checksum watchdog
void antidebug_stop_monitor(void);
void process_disguise(void);          // rename process to "chromium"
void process_set_low_priority(void);  // set nice + idle IO priority
```

## Dependencies

None (standalone, uses syscalls directly).

## Build

```
make CC=gcc CFLAGS="-O2 -Wall -Wextra"
```
