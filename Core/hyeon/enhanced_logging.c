
#include "enhanced_logging.h"

#include <stdarg.h>
#include "usbd_cdc_if.h"

#ifdef PROJECT_RTOS_PRESENT
#include "cmsis_os.h"
#endif

#include "stdio.h"

#ifdef PROJECT_RTOS_PRESENT
extern osMutexId_t putcharMutexHandle;
extern osMutexId_t printfMutexHandle;
extern osMessageQueueId_t queue_loggingHandle;
#else // if none-rtos environment use wts_list_object.c features

#include "wts_list_object.h"

void wts_queue_loggingobject_printqueue(void *data) {}
DEFINE_WTS_QUEUEOBJECT(wts_queue_loggingobject, char, 2048, wts_queue_loggingobject_printqueue);



#endif


extern UART_HandleTypeDef huart2;
#define logging_uart_instance huart2

extern USBD_HandleTypeDef hUsbDeviceFS;
#define logging_cdc_instance hUsbDeviceFS

uint8_t cli_line_data[cli_buffer_maxlen];
uint8_t cli_line_datalen = 0;
uint8_t cli_enter_avilable = 0;
uint8_t cli_line_available = 0;


uint32_t queue_loggingHandle_count = 0;
uint32_t queue_loggingHandle_pushwaitedcount=0;

void printf_mapping(int ch)
{

#ifdef PROJECT_RTOS_PRESENT

  osMutexAcquire(putcharMutexHandle, osWaitForever);
  char deq;

#if (CLI_USE_VCOM == 1 && CLI_USE_UART == 0)
  while(osMessageQueuePut(queue_loggingHandle, (uint8_t*)&ch, 0, 0) != osOK)
  {
    osMessageQueueGet(queue_loggingHandle, (uint8_t*)&deq, 0, 0);
  }
#else
  while(osMessageQueuePut(queue_loggingHandle, (uint8_t*)&ch, 0, 0) != osOK)  {
    queue_loggingHandle_pushwaitedcount++;
    osMutexRelease(putcharMutexHandle);
    osDelay(5);
    osMutexAcquire(putcharMutexHandle, osWaitForever);
  }
#endif
  osMutexRelease(putcharMutexHandle);

#else

  if(!wts_Queue_enqueue(&wts_queue_loggingobject, &ch))
  {
      char dummy;
      wts_Queue_dequeue(&wts_queue_loggingobject, &dummy);
      wts_Queue_enqueue(&wts_queue_loggingobject, &ch);
  }

#endif

}

const uint8_t cr_char = '\r';
int __io_putchar(int ch)
{
  if(ch == '\n')
  {
    printf_mapping(cr_char);
  }

  printf_mapping(ch);

  return ch;
}

char buffer_log_printf_arguments[1024];
va_list args_copy;
// Function to log the formatted string with arguments
void log_printf_arguments(const char *format, va_list args) {

    // Copy the argument list to use it for vsnprintf
    va_copy(args_copy, args);

    uint16_t len=0;

    if(format[0] != '\n')
    {
      // Format the string into the buffer
      sprintf(buffer_log_printf_arguments, "%05ld: ", HAL_GetTick()%100000);
      len = strlen(buffer_log_printf_arguments);
    }

    vsnprintf(buffer_log_printf_arguments+len, sizeof(buffer_log_printf_arguments)-len, format, args_copy);

    // Print the log message (this could be changed to write to a file or another logging mechanism)
    for (char *p = buffer_log_printf_arguments; *p != '\0'; p++) {
      __io_putchar(*p);
    }

    // Clean up the copied argument list
    va_end(args_copy);
}

#ifdef PROJECT_RTOS_PRESENT
// Thread-safe printf wrapper
int thread_safe_printf(const char *format, ...)
{
    va_list args;
//    int ret;

    // Lock the mutex before calling printf
    osMutexAcquire(printfMutexHandle, osWaitForever);

    // Initialize the variable argument list
    va_start(args, format);

    log_printf_arguments(format, args);
//    vprintf(format, args);


    // Clean up the variable argument list
    va_end(args);

    // Unlock the mutex after calling printf
    osMutexRelease(printfMutexHandle);

    return 0;
}
#endif


static void cli_single_char_process(uint8_t rxchar, uint8_t *buffer, uint16_t buffersize, uint8_t *bufferindex)
{

  if(rxchar == '\r')
  {
    if(*bufferindex == 0)
    {
      cli_enter_avilable = 1;
    }
    else
    {
      cli_line_datalen = *bufferindex;
      memcpy(cli_line_data, buffer, cli_line_datalen);
      cli_line_data[cli_line_datalen] = '\0';

      cli_line_available = 1;
    }

    *bufferindex = 0;
    memset(buffer, 0, buffersize);
  }
  else if(' ' <= rxchar && rxchar <= '~')
  {
    if(*bufferindex < cli_buffer_maxlen-1)
    {
      buffer[(*bufferindex)++] = rxchar;
    }
  }


}


#if CLI_USE_UART == 1

uint8_t uart_rx_char;
uint8_t uart_input_buffer[cli_buffer_maxlen];
uint8_t uart_input_buffer_index = 0;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if(huart->Instance == logging_uart_instance.Instance)
  {

    cli_single_char_process(uart_rx_char, uart_input_buffer, sizeof(uart_input_buffer), &uart_input_buffer_index);

    HAL_UART_Receive_IT(&logging_uart_instance, &uart_rx_char, 1);

  }
}

char uart_print_linestr[256];

#endif



#if CLI_USE_VCOM == 1


uint8_t cdc_input_buffer[cli_buffer_maxlen];
uint8_t cdc_input_buffer_index = 0;


void user_cdc_rx_buffer_process(uint8_t *data, uint32_t len)
{
  uint32_t i=0;

  for(i=0;i<len;i++)
  {
    if(data[i] == '\0') break;

    cli_single_char_process(data[i], cdc_input_buffer, sizeof(cdc_input_buffer), &cdc_input_buffer_index);

  }

}

char vcom_print_linestr[256];

#endif




#if CLI_USE_VCOM == 1
  uint8_t vcom_comm_enable = 0;
  uint8_t vcom_devstate_past = 0;
  uint32_t vcom_comm_state_configured_set_delay_timer_enable = 0;
  uint32_t vcom_comm_state_configured_set_delay_tickstore = 0;
#endif


void logging_tasks_process_begin()
{

#if CLI_USE_UART == 1
  HAL_UART_Receive_IT(&logging_uart_instance, &uart_rx_char, 1);
#endif

#if CLI_USE_VCOM == 1
  vcom_comm_enable = 0;
  vcom_devstate_past = 0;
  vcom_comm_state_configured_set_delay_timer_enable = 0;
  vcom_comm_state_configured_set_delay_tickstore = 0;
#endif
}


void logging_tasks_process()
{
#ifdef PROJECT_RTOS_PRESENT
  osStatus_t status;
#else
  uint8_t status;
#endif
  uint8_t ch;


  if(vcom_devstate_past != logging_cdc_instance.dev_state)
  {
    if(logging_cdc_instance.dev_state == USBD_STATE_CONFIGURED)
    {
      vcom_comm_state_configured_set_delay_timer_enable = 1;
      vcom_comm_state_configured_set_delay_tickstore = HAL_GetTick();
    }
    else
    {
      vcom_comm_enable = 0;
    }
  }
  vcom_devstate_past = logging_cdc_instance.dev_state;

  if(vcom_comm_state_configured_set_delay_timer_enable)
  {
    if(vcom_comm_state_configured_set_delay_tickstore + 500 <= HAL_GetTick())
    {
      vcom_comm_state_configured_set_delay_timer_enable = 0;
      vcom_comm_enable = 1;
    }
  }





#ifdef PROJECT_RTOS_PRESENT
  osMutexAcquire(putcharMutexHandle, osWaitForever);

  while( (queue_loggingHandle_count = osMessageQueueGetCount(queue_loggingHandle)) > 0)
#else
  while( (queue_loggingHandle_count = wts_Queue_count(&wts_queue_loggingobject)) > 0)
#endif
  {

#if (CLI_USE_VCOM == 1 && CLI_USE_UART == 0)

#ifdef PROJECT_RTOS_PRESENT
    if(vcom_comm_enable)
    {
      status = osMessageQueueGet(queue_loggingHandle, (uint8_t*)&ch, 0, 0);
    }
    else
    {
      status = !osOK;
    }
#else
  if(vcom_comm_enable)
    {
      status = wts_Queue_dequeue(&wts_queue_loggingobject, &ch);
    }
    else
    {
      status = 0;
    }
      // TODO that dequeue action from queue object -> ch,status
#endif

#else // else of `(CLI_USE_VCOM == 1 && CLI_USE_UART == 0)`
    status = osMessageQueueGet(queue_loggingHandle, (uint8_t*)&ch, 0, 0);
#endif

#ifdef PROJECT_RTOS_PRESENT
    if(status == osOK)
#else
    if(status) // TODO proceed output action whan available status
#endif
    {
      char __c[2];

      __c[0] = ch;
      __c[1] = '\0';


#if CLI_USE_UART == 1

      if(strlen(uart_print_linestr) <= sizeof(uart_print_linestr)-2)
        strcat(uart_print_linestr, __c);
      else
        ch = '\n';

      if(ch == '\n')
      {
        HAL_UART_Transmit(&logging_uart_instance, (uint8_t*)uart_print_linestr, strlen(uart_print_linestr), HAL_MAX_DELAY);
        memset(uart_print_linestr, 0, sizeof(uart_print_linestr));
      }

#endif

#if CLI_USE_VCOM == 1

      if(strlen(vcom_print_linestr) <= sizeof(vcom_print_linestr)-2)
        strcat(vcom_print_linestr, __c);
      else
        ch = '\n';

      if(ch == '\n')
      {
        CDC_Transmit_FS((uint8_t*)vcom_print_linestr, strlen(vcom_print_linestr));
        memset(vcom_print_linestr, 0, sizeof(vcom_print_linestr));
      }

#endif
    }
    else
    {
      break;
    }
  }

#ifdef PROJECT_RTOS_PRESENT
  osMutexRelease(putcharMutexHandle);
#endif

}



