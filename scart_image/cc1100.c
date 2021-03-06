/*
    cc1100.c
    Copyright (C) 2007 

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

#include <P89LPC932.h>
#include "cc1100.h"
#include "smartrf_CC1100.h"


/* Configuration bytes for cc1100
	Will be loaded into cc100 registers at initialization
	
	We redefine some of the definitions of SMARTRF. This is intended.
	
	0x02:IOCFG0 = 0x06: GD0 active high	
				Asserts when sync word has been sent / received, and de-asserts at the end of the packet. 
				In RX, the pin will de-assert when the optional address check fails or the RX FIFO overflows.
 				In TX the pin will de-assert if the TX FIFO underflows.
	0x02:IOCFG0 = 0x07: GD0 active high
				Asserts when a packet has been received with CRC OK. 
				De-asserts when the first byte is read from the RX FIFO.

	0x06:PKTLEN = 0xFF: maximum packet length is 255 (allows room for 2 STATUS APPEND bytes, see Errata Sheet)	
	0x07:PKTCTRL1 = 0x06: Address check and 0 broadcast, status append, no autoflush of RX FIFO
	0x08:PKTCTRL0 = 0x45: variable packet length (first byte after sync), CRC enabled, whitening on

	0x09:ADDR = 0x01: Device address is 1
	0x0a:CHANNR = 0x01: Channel 1
	0x17:MCSM1 = Goto IDLE after TX, goto IDLE after RX 
*/
#define SMARTRF_SETTING_PKTCTRL1	0x06
#define SMARTRF_SETTING_PKTCTRL0	0x45
#define SMARTRF_SETTING_IOCFG0D		0x07
#define SMARTRF_SETTING_IOCFG2		0x06
#define SMARTRF_SETTING_FIFOTHR		0x00

#define TXOFF_IDLE			0x00
#define RXOFF_IDLE			0x00
#define RXOFF_RX			0xC0
#define TXOFF_RX			0x03

const unsigned char conf[0x2F] = {
	SMARTRF_SETTING_IOCFG2, 	// CC1100_IOCFG2     0x00;
	0x2E,						// IOCFG1
	SMARTRF_SETTING_IOCFG0D,	// CC1100_IOCFG0D    0x02;
	SMARTRF_SETTING_FIFOTHR,	//Adr. 03 FIFOTHR   RXFIFO and TXFIFO thresholds.
	0xD3 , 0x91, 				// Adr. 4, Adr. 5 
	SMARTRF_SETTING_PKTLEN,		//Adr. 06 PKTLEN    Packet length.
	SMARTRF_SETTING_PKTCTRL1,	//Adr. 07 PKTCTRL1  Packet automation control.
	SMARTRF_SETTING_PKTCTRL0,	//Adr. 08 PKTCTRL0  Packet automation control.
	DEV_ADDR			,		//Adr. 09 ADDR      Device address.
	SMARTRF_SETTING_CHANNR,		//Adr. 0A CHANNR    Channel number.
	SMARTRF_SETTING_FSCTRL1,	//Adr. 0B FSCTRL1   Frequency synthesizer control.
	SMARTRF_SETTING_FSCTRL0,	//Adr. 0C FSCTRL0   Frequency synthesizer control.
	SMARTRF_SETTING_FREQ2,		//Adr. 0D FREQ2     Frequency control word, high byte.
	SMARTRF_SETTING_FREQ1,		//Adr. 0E FREQ1     Frequency control word, middle byte.
	SMARTRF_SETTING_FREQ0,		//Adr. 0F FREQ0     Frequency control word, low byte.
	SMARTRF_SETTING_MDMCFG4,	//Adr. 10 MDMCFG4   Modem configuration.
	SMARTRF_SETTING_MDMCFG3,	//Adr. 11 MDMCFG3   Modem configuration.
	SMARTRF_SETTING_MDMCFG2,	//Adr. 12 MDMCFG2   Modem configuration.
	SMARTRF_SETTING_MDMCFG1,	//Adr. 13 MDMCFG1   Modem configuration.
	SMARTRF_SETTING_MDMCFG0,	//Adr. 14 MDMCFG0   Modem configuration.
	SMARTRF_SETTING_DEVIATN,	//Adr. 15 DEVIATN   Modem deviation setting (when FSK modulation is enabled).
	0x07,						// Adr. 16 MCSM2
	0x00 | RXOFF_IDLE | TXOFF_IDLE,	// Adr. 17 MCSM1
	SMARTRF_SETTING_MCSM0,		//Adr. 18 MCSM0     Main Radio Control State Machine configuration.
	SMARTRF_SETTING_FOCCFG,		//Adr. 19 FOCCFG    Frequency Offset Compensation Configuration.
	SMARTRF_SETTING_BSCFG,		//Adr. 1A BSCFG     Bit synchronization Configuration.
	SMARTRF_SETTING_AGCCTRL2,	//Adr. 1B AGCCTRL2  AGC control.
	SMARTRF_SETTING_AGCCTRL1,	//Adr. 1C AGCCTRL1  AGC control.
	SMARTRF_SETTING_AGCCTRL0,	//Adr. 1D AGCCTRL0  AGC control.
	0x46 , 0x50 , 0x78,		// Adr. 1E, 1F, 20 
	SMARTRF_SETTING_FREND1,		//Adr. 21 FREND1    Front end RX configuration.
	SMARTRF_SETTING_FREND0,		//Adr. 22 FREND0    Front end TX configuration.
	SMARTRF_SETTING_FSCAL3,		//Adr. 23 FSCAL3    Frequency synthesizer calibration.
	SMARTRF_SETTING_FSCAL2,		//Adr. 24 FSCAL2    Frequency synthesizer calibration.
	SMARTRF_SETTING_FSCAL1,		//Adr. 25 FSCAL1    Frequency synthesizer calibration.
	SMARTRF_SETTING_FSCAL0,		//Adr. 26 FSCAL0    Frequency synthesizer calibration.
	0x41, 0x00,			// Adr. 27, 28
	SMARTRF_SETTING_FSTEST,		//Adr. 29 FSTEST    Frequency synthesizer calibration.
	0x7F , 0x3F,			// Adr. 2A, 2B
	SMARTRF_SETTING_TEST2,		//Adr. 2C TEST2     Various test settings.
	SMARTRF_SETTING_TEST1,		//Adr. 2D TEST1     Various test settings.
	SMARTRF_SETTING_TEST0,		//Adr. 2E TEST0     Various test settings.
};
	
// recommended by Smart RFStudio for 0 dBm 
#define PA_VALUE	0x60

/* write 1 byte to spi interface and simultaneously read 1 byte 
	This is of course not the real SPI but our bit banged pseudo SPI
*/
static unsigned char 
spi_rw(unsigned char write) {
	unsigned char i;

	for (i=8; i > 0; i--) {
		SCK = 0;
		if (write & 0x80)
			MOSI1 = 1;
		else
			MOSI1 = 0;
		SCK = 1;
		write <<= 1;
		if (MISO1)
			write |= 0x01;
	}
	SCK = 0;
 
	return(write);  
}

static unsigned char 
cc1100_write(unsigned char addr, unsigned char* dat, unsigned char length) {
 
	unsigned char i;
	unsigned char status;
 
	CS = 0;
	while (MISO1);
	status = spi_rw(addr | WRITE | BURST);
	for (i=0; i < length; i++) 
		spi_rw(dat[i]); 
	CS = 1;
 
	return(status);
} 

/* Write one data byte to addr of cc1100 */
unsigned char 
cc1100_write1(unsigned char addr, unsigned char dat) {
 
	unsigned char status;
 
	CS = 0;
	while (MISO1);
	status = spi_rw(addr | WRITE); 
	spi_rw(dat); 
	CS = 1;
 
	return(status);
} 

/* Initialize cc1100 */
void 
cc1100_init(void) {
 
	unsigned char i = 0xff;
  
	SCK = 1;
	MOSI1 = 0;
	CS = 0;
	while(i) {
		i--;
	}
	CS = 1;
	i=0xff; 
	while(i) {
		i--;
	}
	CS = 0;
	SCK = 0; 
	while (MISO1);  
	spi_rw(SRES);
	while (MISO1);

	cc1100_write(0x00, conf, 0x2f);
	cc1100_write1(PATABLE, PA_VALUE);	

}

// not used
#if 0
unsigned char 
cc1100_read(unsigned char addr, unsigned char* dat, unsigned char length) {
	unsigned char i;
	unsigned char status;
 
	CS = 0;
	while (MISO1);
	status = spi_rw(addr | READ);
	for (i=0; i < length; i++)
		dat[i]=spi_rw(0x00);
	CS = 1;
 
	return(status);
}
#endif

/* read 1 byte from addr of cc1100 and return it */
unsigned char
cc1100_read1(unsigned char addr) {
	unsigned char r;
 
	CS = 0;
	while (MISO1);
	spi_rw(addr | READ);
	r = spi_rw(0x00);
	CS = 1;
 
	return(r);
}

/* Send a command strobe to cc1100 */
unsigned char 
cc1100_strobe(unsigned char cmd) {
	unsigned char status;

	CS = 0;
	while (MISO1);
	status = spi_rw(cmd);
	CS = 1;
 
	return(status);
}


/* Read a status register (0x30 - 0x3D) of CC1100 on the fly.
 	Means we have to read until no change occurs because of a bug in the CC1100
*/
static unsigned char 
cc1100_read_status_reg_otf(unsigned char reg){
	 unsigned char res1, res2;
	
	 res1 = cc1100_read1(reg | BURST);
	 while ( (res2=cc1100_read1(reg | BURST)) != res1)
		 res1 = res2;
	 return res2;
 }

/* 
 	Read the status register of the cc1100 with RX_FIFO_AVAILABLE
	Works even when cc1100 is busy
*/
unsigned char 
cc1100_read_rxstatus(){
	 unsigned char res1, res2;
	
	 res1 = cc1100_strobe(SNOP | READ);
	 while ( (res2=cc1100_strobe(SNOP | READ)) != res1)
		 res1 = res2;
	 return res2;
 }


 /* Bring the CC1100 to idle mode and wait until it is reached */
 void 
switch_to_idle() {
	cc1100_strobe(SFRX);		// to get out of possible RX_OVERFLOW state
	cc1100_strobe(SIDLE);
	while ( (cc1100_read_rxstatus() & STATE_MASK) != CHIP_IDLE);
}


/* Returns TRUE iff the cc1100 is not in a TX mode,
	i.e. either RX or IDLE 
	CC1100 is not very reliable.
	There might be some bytes in TX buffer but sending stopped
	nevertheless for unknown reasons.
	So we check if state is idle or RX or some error state.
*/
unsigned char 
cc1100_tx_finished(){
	unsigned char s;
		
	s = cc1100_read_rxstatus() & STATE_MASK;
	return ( (s == CHIP_IDLE ) || (s == CHIP_RX) || (s == CHIP_RX_OVFL) || (s == CHIP_TX_UNFL));
}



