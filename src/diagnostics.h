#ifndef MEOWKEY_DIAGNOSTICS_H
#define MEOWKEY_DIAGNOSTICS_H

#include <stddef.h>

void meowkey_diag_init(void);
void meowkey_diag_clear(void);
void meowkey_diag_logf(const char *format, ...);
size_t meowkey_diag_snapshot(char *output, size_t output_capacity);

#endif
