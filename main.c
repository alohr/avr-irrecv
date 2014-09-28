#ifndef F_CPU
#error F_CPU not defined
#endif

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

#include "irrecv.h"
#include "rc5codes.h"
#include "timer0.h"

enum {
    CONTROL_TOGGLE_BRIGHTNESS  = 0x01,
    CONTROL_TOGGLE_SPEED       = 0x02,
    CONTROL_BRIGHTNESS_UP      = 0x03,
    CONTROL_BRIGHTNESS_DOWN    = 0x04,
    CONTROL_SPEED_UP           = 0x05,
    CONTROL_SPEED_DOWN         = 0x06,
    CONTROL_OFF                = 0x07,

    SETTLE_ON_HIGH_TIMEOUT_MS  = 100,
    MIN_ACTIVE_BEFORE_SLEEP_MS = 1000,
};

typedef struct {
    unsigned long twakeup;
    int toggle;
    int code;
} State;

decode_results irresults;
State state;

static void setup_int0(void)
{
    // set INT0 to trigger on low level
    MCUCR &= ~_BV(ISC01);
    MCUCR &= ~_BV(ISC00);
}

static void enable_int0()
{
    GIMSK |= _BV(INT0);
}

static void disable_int0()
{
    GIMSK &= ~_BV(INT0);
}

ISR(INT0_vect)
{
    disable_int0();
}

static void sleep(int mode)
{
    set_sleep_mode(mode);
    sleep_enable();
    sleep_mode();
    sleep_disable();
}

static void powerdown(void)
{
    sleep(SLEEP_MODE_PWR_DOWN);
}

void settle_on_high(volatile uint8_t *port, uint8_t mask)
{
    long t = 0, t0 = 0;

    loop_until_bit_is_set(port, mask);
    t0 = millis();

    // wait for pin to stay high for x milliseconds
    while ((t = millis()) - t0 < SETTLE_ON_HIGH_TIMEOUT_MS) {
	if (bit_is_clear(port, mask))
	    t0 = millis();
    }
}

static void setup(void)
{
    DDRB = 0x0f;
    PORTB = 0xe0; // enable pull-up on used pins PB7, 6, 5
}

static void standby(State *state)
{
    PORTB = 0;
    for (int i = 0; i < 3; i++) {
	for (int j = 0; j < 4; j++) {
	    PORTB |= _BV(j);
	    _delay_ms(100);
	    PORTB &= ~_BV(j);
	}
    }

    // wait until there is no more activity from the remote control
    settle_on_high(&PIND, PD2);
    PORTB = 0;

    enable_int0();
    sei();
    powerdown();
    disable_int0();

    state->twakeup = millis();
    state->code = 0;
}

static void irinterpret(State *state, const decode_results *r)
{
    state->code = 0;

    if (r->decode_type == RC5) {
	state->toggle = (r->value & 0x800) != 0;

	switch (r->value & 0xff) {
	case ON_OFF:
	    if (millis() - state->twakeup > MIN_ACTIVE_BEFORE_SLEEP_MS)
		state->code = CONTROL_OFF;
	    break;

	case MUTE:
	    // toggle min/max brightness
	    state->code = CONTROL_TOGGLE_BRIGHTNESS;
	    break;

	case TV_AV:
	    // toggle min/max fade speed
	    state->code = CONTROL_TOGGLE_SPEED;
	    break;

	case VOLUME_UP:
	    // brighter
	    state->code = CONTROL_BRIGHTNESS_UP;
	    break;

	case VOLUME_DOWN:
	    // dimmer
	    state->code = CONTROL_BRIGHTNESS_DOWN;
	    break;

	case CHANNEL_UP:
	    // faster
	    state->code = CONTROL_SPEED_UP;
	    break;

	case CHANNEL_DOWN:
	    // slower
	    state->code = CONTROL_SPEED_DOWN;
	    break;
	}

	state->code |= (state->toggle << 3);
    }
}

void portout(State *state)
{
    if (state->code != 0) {
	loop_until_bit_is_set(PINB, PB4);

	PORTB |= (state->code & 0x0f);

	loop_until_bit_is_clear(PINB, PB4);

	PORTB &= 0xe0;

	state->code = 0;
	irrecv_resume();
    }
}

int main(void)
{
    setup_irrecv();
    setup_int0();
    setup();

    for (;;) {
	if (irrecv_decode(&irresults)) {
	    irinterpret(&state, &irresults);
	    irrecv_resume();
	}
	portout(&state);
    }

    return 0;
}
