/*  Copyright (C) 2018  Florian Menne
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RCSWITCH_H_
#define RCSWITCH_H_

#include <driver/rmt.h>

typedef enum {
	RCSWITCH_HX2262, RCSWITCH_XX1527,
} rcswitch_type_t;

typedef enum {
	RCSWITCH_ERR_INV_ARGUMENT = -6,
	RCSWITCH_ERR_ALREADY_INITIALIZED,
	RCSWITCH_ERR_INV_TYPE,
	RCSWITCH_ERR_NO_MEM,
	RCSWITCH_ERR_INV_CODE_LEN,
	RCSWITCH_ERR_INV_CODE_SYM,
	RCSWITCH_OK
} rcswitch_err_t;

/**
 * Prototype for rx callback.
 * @param bits The received bits.
 * @param delayPerBit Period of one bit.
 */
typedef void (*rcswitch_rxCallback_t)(uint32_t bits, uint32_t delayPerBit);

/**
 * Initializes the rcswitch library according
 * to the configurations from menuconfig
 */
rcswitch_err_t rcswitch_init(void);

/**
 * Deinitialize the library
 */
void rcswitch_deinit(void);

/**
 * Sends signal to configured output
 * @param code Code to send -depends on type-
 * @param repeat Number of repetitions
 * @param type Which type to send out
 * @return Error code RCSWITCH_OK if everything is fine. Check header for all possible errors.
 */
rcswitch_err_t rcswitch_send(const char* code, uint8_t repeat,
		rcswitch_type_t type);

/**
 * Starts receiving
 */
rcswitch_err_t rcswitch_startRx(rcswitch_rxCallback_t rxCallback);

/**
 * Stops receiving
 */
void rcswitch_stopRx(void);

/**
 * Sends ELROAB440 specific signal
 * @param code Code to send. Two coding schemes are available. More info in README.
 * @param on On or Off
 * @param repeat Number of repetitions. At least two are necessary, but it is better to use more.
 * @return Error code RCSWITCH_OK if everything is fine. Check header for all possible errors.
 */
rcswitch_err_t rcswitch_elroab440_send(const char* code, bool on, uint8_t repeat);

/**
 * Bruteforces through all housecodes and channels in ELROAB440 style
 * @param on Turn it on or off
 * @param telegramRepeat Repetition of every code.
 */
void rcswitch_elroab440_bruteforce(bool on, uint8_t telegramRepeat);

/**
 * Converts up to 32 bits to a NULL terminated character string. String must have the length of size + 1.
 * MSB is right.
 * @param bits The bits to convert
 * @param string Pointer to the character array storing the string
 * @param size Number of bits to convert
 */
void rcswitch_bitsToCharacter(uint32_t bits, char* string, uint8_t size);

/**
 * Decodes the bits to HX2262 symbols (0/1/F) and writes these to a character array.
 * Last symbol on right side.
 * @param bits Bits to decode
 * @param code character array with the length of 13 bytes
 * @return RCSWITCH_OK if everything was fine. RCSWITCH_ERR_INV_CODE_SYM if bitstream contains invalid symbol.
 */
int rcswitch_decodeHX2262(uint32_t bits, char* code);

#endif /* RCSWITCH_H_ */
