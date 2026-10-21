#pragma once
/*
 * The firmware's own PWM buzzer driver, for boards with a buzzer pin
 * (PRG32_PIN_BUZZER): the setup menu's audio test and the splash use it when no
 * I2S audio is available. It is not part of the cartridge ABI: the buzzer calls
 * were retired from it in ABI 1.7; cartridges use the 8-voice audio API.
 */
#include <stdint.h>

void prg32_buzzer_init(void);
void prg32_buzzer_tone(uint32_t hz, uint32_t ms, uint16_t duty);
