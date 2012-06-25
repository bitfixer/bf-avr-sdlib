#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/signal.h>
#include <inttypes.h>
#include <avr/iom8.h>
#include <util/delay.h>
#include <string.h>

#define F_OSC 8000000		           /* oscillator-frequency in Hz */
#define UART_BAUD_RATE 19200
#define UART_BAUD_CALC(UART_BAUD_RATE,F_OSC) ((F_OSC)/((UART_BAUD_RATE)*16l)-1)

void usart_putc(unsigned char c) {
   // wait until UDR ready
	while(!(UCSRA & (1 << UDRE)));
	UDR = c;    // send character
}

void uart_puts (char *s) {
	//  loop until *s != NULL
	while (*s) {
		usart_putc(*s);
		s++;
	}
}

void init(void) {
	// set baud rate
	UBRRH = (uint8_t)(UART_BAUD_CALC(UART_BAUD_RATE,F_OSC)>>8);
	UBRRL = (uint8_t)UART_BAUD_CALC(UART_BAUD_RATE,F_OSC);

	// Enable receiver and transmitter; enable RX interrupt
	UCSRB = (1 << RXEN) | (1 << TXEN);

	//asynchronous 8N1
	UCSRC = (1 << URSEL) | (3 << UCSZ0);
}

int main(void) {
    DDRC = 0xff;
    
    init(); // init USART
    while(1)
    {
        PORTC = 0xff;
        //usart_putc('a');
        PORTC = 0x00;
        //usart_putc('b');
    }

}
