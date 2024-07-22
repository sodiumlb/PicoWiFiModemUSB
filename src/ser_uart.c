

#include <stdbool.h>
#include <stdint.h>
#include "ser_hal.h"
#include "pico/stdlib.h"
#include "wifi_modem.h"

uart_inst_t* uarts[] = { uart0, uart1 };

void ser_set(unsigned int signal, bool val){
    gpio_put(signal, val);
}

bool ser_get(unsigned int signal){
    return gpio_get(signal);
}

unsigned int ser_set_baudrate(ser_inst_t ser, unsigned int baudrate){
    return uart_set_baudrate(uarts[ser], baudrate);
}

void ser_set_format(ser_inst_t ser, unsigned int dataBits, unsigned int stopBits, ser_parity_t parity){
    uart_set_format(uarts[ser], dataBits, stopBits, (uart_parity_t)parity);
}

void ser_set_translate_crlf(ser_inst_t ser, bool translate){
    uart_set_translate_crlf(uarts[ser],translate);
}

bool ser_is_readable(ser_inst_t ser){
    return uart_is_readable(uarts[ser]);
}

char ser_getc(ser_inst_t ser){
    return uart_getc(uarts[ser]);
}

void ser_putc(ser_inst_t ser, char c){
    uart_putc(uarts[ser], c);
}

void ser_putc_raw(ser_inst_t ser, char c){
    uart_putc_raw(uarts[ser], c);
}

void ser_tx_wait_blocking(ser_inst_t ser){
    uart_tx_wait_blocking(uarts[ser]);
}

void ser_puts(ser_inst_t ser, const char *s){
    uart_puts(uarts[ser], s);
}

void ser_set_break(ser_inst_t ser, bool en){
    uart_set_break(uarts[ser], en);
}

void ser_set_hw_flow(ser_inst_t ser, bool cts, bool rts){
   if( cts ) {
      gpio_set_function(CTS, GPIO_FUNC_UART);
   } else {
      gpio_init(CTS);
      gpio_set_dir(CTS, OUTPUT);
      gpio_put(CTS, ACTIVE);
   }
   if( rts ) {
      gpio_set_function(RTS, GPIO_FUNC_UART);
   } else {
      gpio_init(RTS);
      gpio_set_dir(CTS, INPUT);
   }
    uart_set_hw_flow(uarts[ser], cts, rts);
}