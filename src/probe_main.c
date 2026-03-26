#include <stdbool.h>

#include "board_probe.h"
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();

    while (true) {
        sleep_ms(2000);
        meowkey_board_probe_emit_report();
    }
}
