# ESP32-RCSWITCH
This tiny library useses the remote module of an ESP32 to transmit and receive signals encoded by XX1527 and HX2262 based encoder chips.
These devices are used in remote controlled modules and power outlets.

## Features
* Transmitting
* Receiving
* Help functions to convert between XX1527 and HX2262
* No realtime issues due to the use of the RMT Module
* Configurable through 'make menuconfig'

## Signal basics

### XX1527
##### Frame format
A frame consists of a _PREAMBLE_, followed by an address _C0-C19_ (hardcoded in decoder) and the the data bits _D0_ _D1_ _D2_ and _D3_

| PREAMBLE | C0 - C19 | D0 | D1 | D2 | D3 |

The following figures show the timings for _LOW_, _HIGH_ and the _PREAMBLE_ (__each dash__ shows __one tick__, __superscript__ number used to __indicate length__).

PREAMBLE:

⁰– \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_ \_³¹

Data (HIGH):

⁰– – – \_⁴

Data (LOW):

⁰– \_ \_ \_⁴


### HX2262
The HX2262 protocol is similar to the XX1527, but instead of using each bit seperately the HX2262 useses two bits do define three symbols.
The available symbols are shown below.

##### Frame format
A0|A1|A2|A3|A4|A5|A6/D|A7/D|A8/D|A9/D|A10/D|A11/D|SYNC.BIT|

LOW   (0): 00

⁰– \_ \_ \_ – \_ \_ \_⁸

HIGH  (1): 11

⁰– – – \_ – – – \_⁸

FLOAT (F): 01

⁰– \_ \_ \_ – – – \_⁸

## How receiving is synchronized
The receiving snychronization is realized by setting the idle threshold of the RMT module to the half duration of the SYNC bit length.
As the Bitrate between the XX1527 and the HX2262 is nearly the same, it doesn't care if the HX2262 or XX1527 bitrate is chosen.
Currently the HX2262 bitrate is used as a base. The sanity check is done by verifying if the members _duration1_ and _level1_ of the last item is set to 0.

## Usage
Create a new subfolder called 'components' in your project root directory (not in 'main').
Clone or add this repo as an submodule to 'components/'. As the repo name 'ESP32-RCSWITCH' is used to clarify that this library is for the ESP32 only, you may change the target folder name to something handy, e.g RCSWITCH.

Git clone example (from project root):

git clone https://github.com/flo90/ESP32-RCSWITCH.git components/RCSWITCH

git submodule add https://github.com/flo90/ESP32-RCSWITCH.git components/RCSWITCH

A menuconfig entry called "RCSWITCH config" should appear in the 'Component config' submenu.

After including RCSWITCH.h (#include <RCSWITCH.h>) you can initialize the library by calling rcswitch_init().

## Configuration
The library can be configured through 'make menuconfig', it povides the following configuration options:
* RMD Divider - Divider used for RMT module clock
* Tx RMT channel - The RMT module channel used for transmitting
* Tx number of memory blocks - Number of RMT module memory blocks used for transmitting
* Tx GPIO - Output GPIO of the generated signal (connect to transmitter)
* Rx RMT channel - The RMT module channel used for receiving
* Rx RMT of memory blocks - Number of RMT module memory blocks used for receiving
* Rx GPIO - Input GPIO of received signal (connect to receiver)

Remember that using more than one memory block will occupie the memory block of the next channel.
E.g changing "Tx number of memory blocks" to 5 while "Tx RMT channel" is set to 0 would occupy all memory blocks used by channel 0 to 4.
Thus, they cannot be used for Rx if needed.
By default, this library is configured to use channel 0 and 4 with 4 memory blocks each for transmitting and receiving.

### Examples
Sending a XX1527 code four times:
rcswitch\_send("100101111010000100000001", 4, RCSWITCH_XX1527);

Sending HX2262 code five times:
rcswitch\_send("00f0ff000ff0", 5, RCSWITCH_HX2262);

Switching an ELROAB440 power outlet on. Housecode 11001 Channel A:
rcswitch\_elroab440\_send("11001A", true, 4)
Also switching channel A:
rcswitch\_elroab440\_send("1100110000", true, 4)

It is also possible to use two "channels" together (on power outlet e.g set dip switch A and B to "on")
To control such a configured power outlet with the original remote, you have to press A and B together on the ON or OFF side.
rcswitch\_elroab440\_send("1100111000", true, 4)

Start receiving with callback _rxCallback_:
rcswitch_startRx(rxCallback);
The prototype used for "rxCallback" is _void rxCallback(uint32_t bits, uint32_t bitTime)_ whereas _bits_ holds the decoded bits and _bitTime_ the time per bit in microseconds.
Use _int rcswitch_decodeHX2262(uint32_t bits, char* code)_ to convert the _bits_ to HX2262 format if necessary.
This function will exit with _RCSWITCH_ERR_INV_CODE_SYM_ if an invalid symbol is found in the bitstream.
