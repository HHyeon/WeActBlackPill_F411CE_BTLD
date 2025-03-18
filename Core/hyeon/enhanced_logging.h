
#ifndef ____ENHANCED_LOGGING___H____
#define ____ENHANCED_LOGGING___H____

#include "stm32f4xx_hal.h"


#define cli_buffer_maxlen 1024
extern uint8_t cli_line_data[cli_buffer_maxlen];
extern uint8_t cli_line_datalen;
extern uint8_t cli_enter_avilable;
extern uint8_t cli_line_available;


#define CLI_USE_UART 0
#define CLI_USE_VCOM 1

int thread_safe_printf(const char *format, ...);

void logging_tasks_process_begin();
void logging_tasks_process();

#endif
