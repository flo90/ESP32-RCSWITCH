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

#include "rcswitch.h"
#include <driver/rmt.h>
#include <esp_log.h>
#include <freertos/task.h>
#include <string.h>

#ifndef CONFIG_RCSWITCH_RMT_FREQ
#define CONFIG_RCSWITCH_RMT_FREQ 80000000UL
#endif

#ifndef CONFIG_RCSWITCH_RMT_DIVIDER
#define CONFIG_RCSWITCH_RMT_DIVIDER 100UL
#endif

#ifndef CONFIG_RCSWITCH_RMT_TXCHANNEL
#define CONFIG_RCSWITCH_RMT_TXCHANNEL 0
#endif

#ifndef CONFIG_RCSWITCH_RMT_RXCHANNEL
#define CONFIG_RCSWITCH_RMT_RXCHANNEL 4
#endif

#ifndef CONFIG_RCSWITCH_RMT_TXMEM
#define CONFIG_RCSWITCH_RMT_TXMEM 1
#endif

#ifndef CONFIG_RCSWITCH_RMT_RXMEM
#define CONFIG_RCSWITCH_RMT_RXMEM 4
#endif

#ifndef CONFIG_RCSWITCH_RMT_TXGPIO
#define CONFIG_RCSWITCH_RMT_TXGPIO 18
#endif

#ifndef CONFIG_RCSWITCH_RMT_RXGPIO
#define CONFIG_RCSWITCH_RMT_RXGPIO 19
#endif

#ifndef CONFIG_RCSWITCH_EN_RX
#define CONFIG_RCSWITCH_EN_RX 0
#endif

#ifndef CONFIG_HX2262_FREQ
#define CONFIG_HX2262_FREQ 10000UL
#endif

#ifndef CONFIG_XX1527_DATACYCLE_US
#define CONFIG_XX1527_DATACYCLE_US 1480
#endif

#define TAG "RCSWITCH"

//Both have nearly the same Bitrate
#define HX2262_BITRATE (CONFIG_HX2262_FREQ/4)
#define XX1527_BITRATE (1000000/(CONFIG_XX1527_DATACYCLE_US/4))

#define BITRATE HX2262_BITRATE

#define TICK  (CONFIG_RCSWITCH_RMT_FREQ/CONFIG_RCSWITCH_RMT_DIVIDER/BITRATE)
#define TICK_3 (TICK*3)
#define TICK_31 (TICK*31)

#define US_PER_TICK (((double)CONFIG_RCSWITCH_RMT_DIVIDER/CONFIG_RCSWITCH_RMT_FREQ) * 1000000.0)

/*
 * Catching config errors here
 */

#if CONFIG_RCSWITCH_RMT_MEM > 8-CONFIG_RCSWITCH_RMT_CHANNEL
#error RMT channel memory allocation error, check RMT channel and RMT memory config!
#endif

#if (32768 <= TICK_31)
#error Duration overflowed, increase CONFIG_RCSWITCH_RMT_DIVIDER
#endif

typedef struct {
	rmt_item32_t SYNC;
	rmt_item32_t HIGH;
	rmt_item32_t LOW;

	rmt_channel_t txChannel;
	rmt_channel_t rxChannel;

	rcswitch_rxCallback_t rxCallback;

	bool rxRunning :1;
	bool stopRx :1;
} rcswitch_t;

static rcswitch_t* pRcswitch = NULL;

static inline rcswitch_err_t sendHX2262(const char* code, uint8_t repeat);
static inline rcswitch_err_t send1527(const char* code, uint8_t repeat);
static void rcswitch_rxTask(void* pvParameter);

static inline int checkElement(rmt_item32_t symbol);
static int decode(rmt_item32_t *items, size_t size, uint32_t* ticksPerBit);

static rcswitch_err_t _send(uint32_t bits, uint8_t repeat);

/*
 * Initialization
 */
rcswitch_err_t rcswitch_init() {
	rmt_config_t rmt_tx;

	const rmt_item32_t SYNC = { .level0 = 1, .duration0 = TICK, .level1 = 0,
			.duration1 = TICK_31 };
	const rmt_item32_t HIGH = { .level0 = 1, .duration0 = TICK_3, .level1 = 0,
			.duration1 = TICK };
	const rmt_item32_t LOW = { .level0 = 1, .duration0 = TICK, .level1 = 0,
			.duration1 = TICK_3 };

	if (pRcswitch != NULL)
		return RCSWITCH_ERR_ALREADY_INITIALIZED;

	pRcswitch = (rcswitch_t*) malloc(sizeof(rcswitch_t));

	if (pRcswitch == NULL)
		return RCSWITCH_ERR_NO_MEM;

	pRcswitch->SYNC = SYNC;
	pRcswitch->LOW = LOW;
	pRcswitch->HIGH = HIGH;

	pRcswitch->txChannel = CONFIG_RCSWITCH_RMT_TXCHANNEL;
	pRcswitch->rxChannel = CONFIG_RCSWITCH_RMT_RXCHANNEL;

	pRcswitch->rxRunning = false;
	pRcswitch->stopRx = false;
	pRcswitch->rxCallback = NULL;

	memset(&rmt_tx, 0, sizeof(rmt_config_t));

	rmt_tx.channel = pRcswitch->txChannel;
	rmt_tx.gpio_num = CONFIG_RCSWITCH_RMT_TXGPIO;
	rmt_tx.mem_block_num = CONFIG_RCSWITCH_RMT_TXMEM;
	rmt_tx.clk_div = CONFIG_RCSWITCH_RMT_DIVIDER;
	rmt_tx.tx_config.idle_output_en = 1;
	rmt_tx.tx_config.idle_level = 0;
	rmt_config(&rmt_tx);
	rmt_driver_install(pRcswitch->txChannel, 0, 0);

	return RCSWITCH_OK;

}

void rcswitch_deinit() {
	rcswitch_stopRx();
	//Busy wait for rx stop
	while (pRcswitch->rxRunning)
		;

	rmt_driver_uninstall(pRcswitch->txChannel);
	if (pRcswitch != NULL)
		free(pRcswitch);
	pRcswitch = NULL;
}

/*
 * Tx functions
 */
rcswitch_err_t rcswitch_send(const char* code, uint8_t repeat,
		rcswitch_type_t type) {
	switch (type) {
	case RCSWITCH_HX2262:
		return sendHX2262(code, repeat);
		break;

	case RCSWITCH_XX1527:
		return send1527(code, repeat);
		break;

	default:
		return RCSWITCH_ERR_INV_TYPE;
		break;
	}
	return RCSWITCH_OK;
}

static inline rcswitch_err_t sendHX2262(const char* code, uint8_t repeat) {
	uint32_t bits = 0;

	for (int i = 0; i < 12; i++) {
		switch (code[i]) {
		case '0':
			break;

		case '1':
			bits |= (0b11 << (2 * i));
			break;

		case 'f':
			bits |= (0b10 << (2 * i));
			break;

		case 'F':
			bits |= (0b10 << (2 * i));
			break;

		default:
			return RCSWITCH_ERR_INV_CODE_SYM;
			break;
		}
	}

	return _send(bits, repeat);
}

static inline rcswitch_err_t send1527(const char* code, uint8_t repeat) {
	uint32_t bits = 0;

	for (int i = 0; i < 24; i++) {
		switch (code[i]) {
		case '0':
			break;

		case '1':
			bits |= 1 << i;
			break;

		default:
			return RCSWITCH_ERR_INV_CODE_SYM;
			break;
		}
	}

	return _send(bits, repeat);
}

/*
 * Rx functions
 */
rcswitch_err_t rcswitch_startRx(rcswitch_rxCallback_t rxCallback) {
	if (rxCallback == NULL || pRcswitch->rxRunning)
		return RCSWITCH_ERR_INV_ARGUMENT;
	pRcswitch->rxCallback = rxCallback;

	xTaskCreate(rcswitch_rxTask, "rcswitch_rxTask", 2048, NULL, 10, NULL);
	return RCSWITCH_OK;
}

void rcswitch_stopRx() {
	if (pRcswitch->rxRunning)
		pRcswitch->stopRx = true;
}

int rcswitch_decodeHX2262(uint32_t bits, char* code) {
	int symbolIterator = 0;
	int symbolState = 0;

	//Decode
	for (int i = 0; i < 24; i++) {
		switch (symbolState) {
		case 0:
			if (bits & (1 << i))
				symbolState = 2;
			else
				symbolState = 1;
			break;

		case 1:
			if (bits & (1 << i))
				code[symbolIterator++] = 'F';
			else
				code[symbolIterator++] = '0';

			symbolState = 0;
			break;

		case 2:
			if (bits & (1 << i))
				code[symbolIterator++] = '1';
			else
				return RCSWITCH_ERR_INV_CODE_SYM;
			symbolState = 0;
			break;

		default:
			return RCSWITCH_ERR_INV_CODE_SYM;
		}
	}
	code[12] = 0;
	return RCSWITCH_OK;
}

static inline int checkElement(rmt_item32_t symbol) {
	if (symbol.duration0 <= (328 + 300) && symbol.duration0 > (328 - 300)
			&& symbol.duration1 <= (880 + 350)
			&& symbol.duration1 > (880 - 350))
		return 0;

	else if (symbol.duration0 <= (880 + 350) && symbol.duration0 > (880 - 350)
			&& symbol.duration1 <= (328 + 300)
			&& symbol.duration1 > (328 - 300))
		return 1;
	else
		return -1;
}

static int decode(rmt_item32_t *items, size_t size, uint32_t* ticksPerBit) {
	uint32_t bits = 0;
	uint32_t accumulatedTicks = 0;

	/*
	 * Sanity check
	 */
	if ((size == 25) && (items[size - 1].duration0 >= 150)
			&& (items[size - 1].duration0 < 400)
			&& (items[size - 1].level1 == 0)
			&& (items[size - 1].duration1 == 0)) {
		//Decode
		for (int j = 0; j < 24; j++) {
			accumulatedTicks += items[j].duration0 + items[j].duration1;
			int ret = checkElement(items[j]);
			if (ret > -1)
				bits |= (ret << j);
			else
				return -1;
		}

		*ticksPerBit = (uint32_t) ((double) accumulatedTicks / (4 * 24));
		return bits;
	}

	return -1;
}

static void rcswitch_rxTask(void* pvParameter) {
	int bits;
	rmt_config_t rmt_rx;

	pRcswitch->rxRunning = true;

	memset(&rmt_rx, 0, sizeof(rmt_config_t));
	rmt_rx.rmt_mode = RMT_MODE_RX;
	rmt_rx.channel = pRcswitch->rxChannel;
	rmt_rx.gpio_num = CONFIG_RCSWITCH_RMT_RXGPIO;
	rmt_rx.mem_block_num = CONFIG_RCSWITCH_RMT_RXMEM;
	rmt_rx.clk_div = CONFIG_RCSWITCH_RMT_DIVIDER;

	rmt_rx.rx_config.filter_ticks_thresh = 255;
	rmt_rx.rx_config.idle_threshold = TICK_31 / 2;
	rmt_rx.rx_config.filter_en = true;

	rmt_config(&rmt_rx);
	rmt_driver_install(rmt_rx.channel, 20000, 0);

	RingbufHandle_t rb = NULL;

	rmt_get_ringbuf_handle(pRcswitch->rxChannel, &rb);
	rmt_rx_start(pRcswitch->rxChannel, true);

	ESP_LOGI(TAG, "RX started");
	while (rb) {
		size_t rx_size = 0;
		uint32_t ticksPerBit;
		rmt_item32_t* item = (rmt_item32_t*) xRingbufferReceive(rb, &rx_size,
				1000);

		if (pRcswitch->stopRx)
			break;
		if (item) {
			bits = decode(item, rx_size / 4, &ticksPerBit);
			if (-1 < bits)
				pRcswitch->rxCallback(bits, ticksPerBit * US_PER_TICK);
			vRingbufferReturnItem(rb, (void*) item);
		}
	}

	rmt_rx_stop(pRcswitch->rxChannel);
	rmt_driver_uninstall(rmt_rx.channel);

	pRcswitch->rxRunning = false;
	pRcswitch->stopRx = false;
	vTaskDelete(NULL);
}

/*
 * Device specific signals
 */
rcswitch_err_t rcswitch_elroab440_send(const char* code, bool on, uint8_t repeat) {
	char tempCode[12];
	const char CHANNEL[] = { 'A', 'B', 'C', 'D', 'E' };
	const char channel[] = { 'a', 'b', 'c', 'd', 'e' };

	for (int i = 0; i < 10; i++) {
		if (code[i] == '0') {
			tempCode[i] = 'f';
		} else if (code[i] == '1') {
			tempCode[i] = '0';
		} else if (i == 5) {
			memset(&tempCode[i], 'f', i);
			for (int j = 0; j < 5; j++) {
				if (CHANNEL[j] == code[i] || channel[j] == code[i]) {
					tempCode[i + j] = '0';
					break;
				} else if (j == 4)
					return RCSWITCH_ERR_INV_CODE_SYM;
			}
			break;
		} else {
			return RCSWITCH_ERR_INV_CODE_SYM;
		}
	}

	tempCode[10] = '0';

	if (!on)
		tempCode[11] = '0';
	else
		tempCode[11] = 'f';
	ESP_LOGI(TAG, "Generated ELROAB440");
	return sendHX2262(tempCode, repeat);
}

void rcswitch_elroab440_bruteforce(bool on, uint8_t telegramRepeat) {
	char code[13];

	code[12] = 0;
	code[10] = '0';

	if (!on)
		code[11] = '0';
	else
		code[11] = 'f';

	for (uint8_t i = 0; i < 32; i++) {
		//generate code
		for (uint8_t bit = 0; bit < 5; bit++) {
			if (i & (1 << bit)) {
				code[bit] = '0';
			} else {
				code[bit] = 'f';
			}
		}

		//generate channel
		for (uint8_t channel = 0; channel < 4; channel++) {
			switch (channel) {
			case 0:
				code[5] = '0';
				code[6] = 'f';
				code[7] = 'f';
				code[8] = 'f';
				code[9] = 'f';
				break;

			case 1:
				code[5] = 'f';
				code[6] = '0';
				code[7] = 'f';
				code[8] = 'f';
				code[9] = 'f';
				break;
			case 2:
				code[5] = 'f';
				code[6] = 'f';
				code[7] = '0';
				code[8] = 'f';
				code[9] = 'f';
				break;
			case 3:
				code[5] = 'f';
				code[6] = 'f';
				code[7] = 'f';
				code[8] = '0';
				code[9] = 'f';
				break;
			default:
				//should never be reached
				break;
			}
			sendHX2262(code, telegramRepeat);
		}
	}
}

static rcswitch_err_t _send(uint32_t bits, uint8_t repeat) {
	int itemIterator = 0;
	rmt_item32_t* items = malloc(sizeof(rmt_item32_t) * 25 * repeat);
	if (items == NULL)
		return RCSWITCH_ERR_NO_MEM;

	for (uint8_t rep = 0; rep < repeat; rep++) {
		items[itemIterator++] = pRcswitch->SYNC;
		for (int i = 0; i < 24; i++) {
			if (bits & (1 << i))
				items[itemIterator++] = pRcswitch->HIGH;
			else
				items[itemIterator++] = pRcswitch->LOW;
		}
	}
	rmt_write_items(pRcswitch->txChannel, items, 25 * repeat, true);
	free(items);
	return RCSWITCH_OK;
}

void rcswitch_bitsToCharacter(uint32_t bits, char* string, uint8_t size) {
	for (uint16_t i = 0; i < size; i++) {
		if (bits & (1 << i))
			string[i] = '1';
		else
			string[i] = '0';
	}
	string[size] = 0;
}
