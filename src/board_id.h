#ifndef MEOWKEY_BOARD_ID_H
#define MEOWKEY_BOARD_ID_H

#include <stdbool.h>
#include <stdint.h>

void meowkey_board_id_init(void);
bool meowkey_board_id_is_detected(void);
uint32_t meowkey_board_id_get_code(void);
const char *meowkey_board_id_summary(void);
void meowkey_board_id_log_summary(void);

#endif
