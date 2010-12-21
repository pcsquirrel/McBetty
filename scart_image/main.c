/*
    main.c
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

#include <P89LPC932.h>               /* special function register declarations   */
                                    /* for the Philips P89C931 device           */

#include "cc1100.h"
#include "smartrf_CC1100.h"
#include "serial.h"

//#define DEBUG


// Some ASCII codes below 0x20 needed for out of band communication
#define EOT	0x04
#define ETX	0x03
#define ACK	0x06

// Software Reset bit of AUXR1
#define SRST	3

// Bits of RTCCON
#define RTCF	7
#define RTCS1	6
#define RTCS0	5
#define RTCEN	0

// Bits of SSTAT
#define CIDIS	5

// Bits of IEN0
//#define ESR	4
//#define EA	7

#if 0
// A simple random number generator, was only used for debugging
#define znew  ((z=36969*(z&65535)+(z>>16))<<16)
#define wnew  ((w=18000*(w&65535)+(w>>16))&65535)
#define RAND  (znew+wnew)

static unsigned long z=362436069, w=521288629;
void setseed(unsigned long i1,unsigned long i2){z=i1; w=i2;}
#endif



/* Pin Belegung laut BettyHacks Forum  
P0.0 	 3 	 CMP2,KBI0 		= 	EPM3064A 			
P0.1 	26 	CIN2B,KBI1 		= 	CC1100 	7 	CSn 	Funkmodul Chip-Select
P0.2 	25 	CIN2A,KBI2 		= 				
P0.3 	24 	CIN1B,KBI3 		= 	EPM3064A 			
P0.4 	23 	CIN1A,KBI4 	I/O = 	ISP HEADER EPM3064A 	5_ 	PDA_ 	Serial data I/O for programming communication
P0.5 	22 	CMPREF,KBI5 I 	= 	ISP HEADER 	4 	PCL 	Serial clock input for programming communication.
P0.6 	20 	CMP1,KBI6 		= 	CC1100 	6 	GDO0 	Funkmodul Universal-I/O 1
P0.7 	19 	T1,KBI7 		= 				
P1.0 	18 	TXD 		O 	= 	ISP HEADER LED 	2_ 	TXD LED 	
P1.1 	17	RXD 		O 	= 	ISP_HEADER
P1.2 	12 	T0,SCL 		O 	= 	24C64AN 		SCL 	I2C Takt
P1.3 	11 	INT0,SDA 	I/O = 	24C64AN 		SDA 	I2C Daten
P1.4 	10 	INT1 			= 	EPM3064A 			
P1.5 	6 	RST 		I 	= 	CC1100 	2 	MISO 	Funkmodul Ausgang Daten
P1.6 	5 	P1.6 		O 	= 	CC1100 	20 	MOSI 	Funkmodul Eingang Daten
P1.7 	4 	P1.7 		O 	= 	CC1100 	1 	SCLK 	Funkmodul Eingang Takt
P2.0 	1 	P2.0 			= 	EPM3064A 			
P2.1 	2 	P2.1 			= 	EPM3064A 			
P2.2 	13 	SPI_MOSI 		= 	EPM3064A 			
P2.3 	14 	SPI_MISO 		= 	EPM3064A 			
P2.4 	15 	SPI_SS 			= 	EPM3064A 			
P2.5 	16 	SPI_CLK 		= 	EPM3064A 			
P2.6 	27 	P2.6 			= 	EPM3064A 			
P2.7 	28 	P2.7 			= 	EPM3064A 			
P3.0 	9 	XTAL2,CLKOUT 	= 				
P3.1 	8 	XTAL2,CLKIN I 	= 	EPM3064A 		CLK OUT 	Prozessor Takt
*/

/* Interpretation of the table above:
	We see that SPI is used exclusively for communication with EPM3064A
	So we have to emulate SPI communication with CC110 by hand (bit banging the corresponding pins).
	The reset pin P1.5 RST is connected to CC1100 MISO, so be careful to switch the reset function off.
*/

/* 
	See smartrf_settings.h for radio definitions.
	We set the radio baudrate to 38400 and the serial baudrate also to 38400.
	
*/


/* Global variable. Gives current direction of half duplex radio link. 
	Is RADIO_RX if we are ready to receive or already receiving via radio.
	Is RADIO_TX if we are sending a packet via radio.
	Is RADIO_IDLE otherwise.
*/
#define RADIO_IDLE 0
#define RADIO_RX 1
#define RADIO_TX 2
unsigned char radio_mode;

__sfr __at (0xAD) CMP2_;


/* --------------------------------------- radio output buffer ---------------------------------- */

/* Our radio transmit output data buffer has the following semantics:
	- We put data bytes in sequentially
	- The buffer is organizing them automatically into packets suitable for radio transmission
	- buf[bufstart] is 0, if no data payload bytes are in buffer, i.e. buffer empty
	- buf[bufstart] is n with n<0, if n payload bytes are in buffer, but packet is not complete yet
	- buf[bufstart] is n (with n>0), if complete packet with n payload bytes is in buffer
	- buf[bufstart] is only valid when we are not currently transferring bytes out of the buffer to TXFIFO

	The buffer is used to store data bytes before we send them to cc1100 because we do not know the packet size
	in advance.
	We can get quite long answers from MPD and there is a small delay each time we send a packet 
	(transfering to CC110, calibration etc. ), so we have to tell mpdtool when we are ready to receive more
	bytes. 

	We can concurrently put bytes in the buffer (via serial_isr) and read them out via buffer_out().
	The buffer is filled via interrupt, but the main program has to know when we have received an
	EOT. The flag got_eot is set by the interrupt service routine when EOT is seen.
*/

/* 
	This is the serial line input buffer == radio transmit outout buffer.
	The buffer is a few bytes longer than needed just to be sure
	It is implemented as a FIFO ring buffer
*/
/* Number of bytes that mpdtool sends before ETX/ACK pair. */
#define MPDTOOL_PKTSIZE	16

#define BUFSIZE (MAX_TX_PAYLOAD + MPDTOOL_PKTSIZE + 2)
__idata char buf[BUFSIZE];
unsigned char bufstart;
volatile unsigned char bufcnt;
volatile unsigned char bufnxt;
volatile __bit got_etx;
volatile __bit got_eot;


void
feed_wd(){
	EA = 0;
	WFEED1 = 0xA5;
	WFEED2 = 0x5A;
	EA = 1;
}

void buffer_init(){
	bufcnt = 0;			// Number of bytes in the buffer
	bufnxt = 0;			// Index of next free place in buffer
	bufstart = 0;		// Index of first data byte in buffer
}

/* "First in" part of buffer routines is handled by ISR: */
/* The serial interrupt reads a character from serial interface if there is one.
	The character is stored in the ring buffer.
	If the ring buffer is full, the last character will be overwritten.
	This ensures that EOT character should be stored.
	The flag got_etx is set to 1 if an ETX character is received.
	ETX will never be stored in buffer.
	For debugging purposes the number of overwritten(=dropped) bytes is counted.
	The flag got_etx can be used as a semaphore, sdcc will later generate atomical TestAndClear instructions for it.
	The flag got_eot is set as soon as we see an EOT character. All following bytes will be dropped until got_eot is cleared.
	bufcnt and buflim (and got_etx and got_eot and dropped) are changed in this routine.
*/

void serial_isr (void) __interrupt (4) {
	char x;
	
	x=SBUF;
	RI = 0;
	
	/* We remember if we have seen an ETX character from MPD 
		We don't expect any more characters over serial line until we have sent an ACK,
	*/
	if (x == ETX){
		got_etx = 1;
		return;
	};
	
	//TODO this need not be enforced here. mpdtool should take care of this,
	if (got_eot){
		return;
	};
	
	/* If the buffer is full, delete previous byte to make room */
	if (bufcnt >= BUFSIZE){
		bufcnt--;
		/* decrement bufnxt by 1, with wrap around */
		if (bufnxt == 0)
			bufnxt = BUFSIZE-1;
		else
			bufnxt--;
	};
	
	/* Store byte and increment bufnxt by 1, with wrap around */
	buf[bufnxt++] = x;
	if (bufnxt >= BUFSIZE)
		bufnxt = 0;
	bufcnt++;
	
	if (x == EOT)
		got_eot = 1;

	return;
}


/* "first out" part of buffer routine 
	Returns 0 if the buffer is empty
*/
char buffer_out(){
	char x;
	
	if (bufcnt == 0) return (0);
	x = buf[bufstart++];
	if (bufstart >= BUFSIZE)
		bufstart = 0;
	
	/* NOTE The following works only if decrement is an atomic operation in SDCC (it is)*/
	bufcnt--;
	
	return x;
}


/* Returns 1, iff there are some empty places in the buffer 
	We know that mpdtool sends 16 characters + ETX and then waits for an ACK.
	But between calling this function has_room() and the decision to send an ACK some
	short time may pass. For example we have exactly 16 bytes free in our buffer. 
	We check somewhere in the main loop if we have room. This seems to be the case.
	But directly after checking one more character might arrive and even one more might 
	be in the process of transferring. So better to say yes only when we have 18 bytes
	free,
	TODO well, no. Even better to check if we received an ETX. We then know that should be 
	the last character, because mpdtool waits after an ETX.
	So when got_etx is 1, has_room() is correct and stays correct until we allow more bytes
	via ACK!
*/
unsigned char has_room(){
	return ( (BUFSIZE - bufcnt) > (MPDTOOL_PKTSIZE + 2) );
}


/* This routine checks if reception is stuck in RX_OVERFLOW state.
	If so, it flushes the buffer and resets radio to RX */

#define RX_OVERFLOW 17

void
rx_overflow_reset(){
	if ( (cc1100_read_status_reg_otf(MARCSTATE) & 0x1f) == RX_OVERFLOW){
		cc1100_strobe(SFRX);
		cc1100_strobe(SRX);
	};
}


/* Start radio reception */
void start_rx() {
	// Reset state and set radio in RX mode
	rx_overflow_reset();
	switch_to_idle();
	
	cc1100_strobe(SFRX);
	cc1100_strobe(SCAL);
	cc1100_strobe(SRX);
	radio_mode = RADIO_RX;
}


/* Start radio transmission */
void start_tx() {	
	rx_overflow_reset();
	switch_to_idle();
	
	cc1100_strobe(SCAL);
	cc1100_strobe(STX);
	radio_mode = RADIO_TX;
}

/* Returns TRUE iff radio transmission is finished */
unsigned char tx_finished(){
	return ( (cc1100_read_status_reg_otf(TXBYTES) & 0x7f) == 0  &&
		( (cc1100_read1(PKTSTATUS | BURST) & (1<<2)) == 0 ) );		// GDO2 == 0
}

/* If we are in RADIO_TX mode and transmission is finished, enter RADIO_RX mode */
void re_enter_rx(){
	if ((radio_mode == RADIO_TX) && tx_finished())
		start_rx();
}		


/*
	Send buffer contents over radio.
	When buffer has enough bytes for one packet (MAX_PACKETLEN -1), it starts filling TX_FIFO.
	The packet length is then set to MAX_PACKETLEN.
	IF a complete packet is already in the buffer, the packet length is set to the buffer length +1.
	At each call of this function one byte is transferred from buffer to TX_FIFO if possible.
	When all necessary bytes have been transferred, the packet is sent
	and radio_mode is set to RADIO_TX.

	This is a state machine. Transmission can be in 3 states:
	(0) TX_IDLE			; not transmitting and nothing to transmit
	(1) TX_COPY			; copy bytes from buffer to tx_fifo
	(3) TX_SEND			; send the packet

*/

#define TX_IDLE		0
#define TX_COPY		1
#define TX_SEND		2

void handle_tx(){

	/* This byte is the status of the handle_tx routine */
	static unsigned char tx_state = TX_IDLE;
	static unsigned char tx_cnt;
	char x;

	switch (tx_state){
		
	case TX_IDLE:
		/* Is a new packet ready ? */
		if (got_eot) {
			tx_cnt = bufcnt;	/* = number of payload bytes to be transferred to TXFIFO (not counting address and length byte) */
		
			/* Transfer the length byte to TXFIFO (just add 1 for the address) */
			cc1100_write1(TX_fifo, tx_cnt + 1);
		
			/* Transfer the address to TXFIFO */
			cc1100_write1(TX_fifo, DEV_ADDR);	
			
			tx_state = TX_COPY;
			got_eot=0;
			
			break;
			
		} else if ( bufcnt >= MAX_TX_PAYLOAD ) {
			
			/* tx_cnt = number of payload bytes to be transferred to TXFIFO (not counting address and length byte) */
			tx_cnt = MAX_TX_PAYLOAD;	
			
			/* Transfer the length byte to TXFIFO (just add 1 for the address) */
			cc1100_write1(TX_fifo, MAX_TX_PAYLOAD + 1);
			
			/* Transfer the address to TXFIFO */
			cc1100_write1(TX_fifo, DEV_ADDR);	
			
			tx_state = TX_COPY;
		};
		break;
		
	case TX_COPY:
		/* Are there still bytes to copy to TXFIFO? */
		if (tx_cnt > 0){
			x = buffer_out();
			cc1100_write1(TX_fifo, x);	
			tx_cnt--;
		} else {
			/* Finished copying ! */ 
			tx_state = TX_SEND;
		};
		break;
		
	case TX_SEND:
		start_tx();
		tx_state = TX_IDLE;
		break;
	}
}

/* Checks if the sender is waiting for ACK. Sends ACK if there is room in buffer */
void check_etx(){
	if (got_etx) {	/* Is the sender waiting for an ACK ? */		
		if (has_room()){	/* Still enough space in buffer ? */
			got_etx = 0;		// Atomic Operation ! (see sdcc manual)
			send_byte(ACK);
		};
	};
}
	
	

/* checks if a complete packet has been received via radio 
	Transfers this packet to serial line
	Stops if EOT is received, EOT is sent over serial, radio_mode is set to IDLE
*/
void check_radio_packet (){
	char x;
	unsigned char i, length, address;
	
	if ( GDO0 ){
		// We have received a complete packet over radio

		cc1100_read(RX_fifo|BURST, &length, 1);		// Length byte = payload length + 1
		
		cc1100_read(RX_fifo|BURST, &address, 1); 	// Address byte (unused!)

		// We are reading the RX_fifo one byte at a time and sending this byte to serial output 
		for (i=0; i < (length - 1); i++){
			cc1100_read(RX_fifo|BURST, &x, 1);
			send_byte(x);
			if (x == EOT)
				break;
		};
		start_rx();	
	};
}

#define MARCSTATE_IDLE 0x01
/* We check if there are some bytes to read from the RX_FIFO
	and output one of them to the serial line.
	We make sure to empty the RX_FIFO only when a complete packet has been received. (see CC1100 errata)
	
	NOTE what can go wrong?
		a) We might read a wrong length byte.
		b) The CRC might not match.
		c) The address might be wrong.
		d) Reception somehow stops before the full packet has been received.
		
		a)  length read is shorter than it should be:
				we do not read the end of the packet, missing the EOT character.
			length read is longer than it should be:
				we read the complete packet and send it to serial line, but
				we also send some status bytes via serial and maybe the RX_FIFO underflows.
	None of these errors should lead to an infinite loop.
*/
void check_radio_input (){
	static unsigned char length = 0;
	unsigned char address;
	unsigned char n;
	char x;
	
	/* nothing received so far */
	if (length == 0){
		n = cc1100_read_status_reg_otf(RXBYTES) & 0x7f;
		if (n > 2) {
			cc1100_read(RX_fifo|BURST, &length, 1);		// Length byte = payload length + 1 address byte
			cc1100_read(RX_fifo|BURST, &address, 1); 	// Address byte (unused!)
			/* We do not decrement length here, so length is the remaining number of payload bytes + 1!
				So we are finished when length == 1 !
			*/
		} 
	} else {	// Already received length (and address), read payload 
		/* Finished packet ? */
		if ( (cc1100_read_status_reg_otf(MARCSTATE) & 0x1f) == MARCSTATE_IDLE) {
			while (length > 1) {
				cc1100_read(RX_fifo|BURST, &x, 1);
				send_byte(x);	
				length--;
			};
			length = 0;
			start_rx();	
			
		} else {
			n = cc1100_read_status_reg_otf(RXBYTES) & 0x7f;	
		
			/* are there enough bytes to read to avoid emptying the RX_FIFO */
			if (n > 1){
				cc1100_read(RX_fifo|BURST, &x, 1);
				send_byte(x);	
				length--;	
			}; 
		};	
	};
}


#if 0	
unsigned char timeout;

void start_timeout(unsigned char time_cnt){
	RTCCON &= ~((1<<RTCEN) | (1<<RTCF));	// Reset RTC	
	timeout = time_cnt;
	RTCCON |= 1<<RTCEN;						// Enable RTC	
}

unsigned char check_timeout(){
	if (RTCCON & (1<<RTCF)) {	// Timer overflow
		RTCCON &= ~(1<<RTCF);		// Clear RTC Flag
		if (timeout-- == 0){		// 
			return 1;
		}
	};
	return 0;
}
#endif

void main(void) {

	/* -------------------------- Initialize the ports ------------------------------ */
	// All pins are input only after reset, i.e. PxM1=11111111 and PxM2=00000000
	
	// -------------------------------- Port 0 ----------------------------------
	// Push-Pull: Pin 7, 4, 1
	// bidirectional: Pin 0
	// Input only: Pin 6, 5, 3, 2
						// 7654 3210
	P0M1 &= 0x6c;		// 0110 1100 -> P0M1
	P0M2 |= 0x92;		// 1001 0010 -> P0M2
	
	P0 = 0x83;			// 1000 0011 -> Port0
	
	// -------------------------------- Port 1 ----------------------------------
	// Push-Pull: Pin 7, 6, 0
	// bidirectional: 
	// Input only: Pin 5, 4, 1
						// 7654 3210
	P1M1 &= 0x3e;		// 0011 1110
	P1M2 |= 0xcd;		// 1100 1101
	
	P1 = 0xcd;			// 1100 1101 -> Port1
	
	// -------------------------------- Port 3 ----------------------------------
	
	P3M1 &= 0xfe;
	P3M2 |= 0x01;
	
	P3 = 0;
	/* ----------------------------------------------------------------- */
	
	
/*
	PT0AD = 0x24;		// enable P0.5 and P0.2 as analog pins
	
	TMOD = 0x22;		// Timer 0 Mode, Timer 1 mode
	TAMOD = 0x10;		// PWM Mode Timer 1, 8-bit auto-reload counter TMR 0
	TCON = 0x50;		// TMR0 and TMR1 on
	
	AUXR1 = 0x20;		// ENT1 = 1, toggle P0.7 when TIMER1 overflows
	TH0 = 0x22;
	TH1 = 0x80;
	CMP2_ = 0x24;
	KBPATN = 0x08;
	KBMASK = 0x08;
	KBCON = 0x02;
	*/
	
	/* Enable Break detection and enter ISP when Break occurs */
	AUXR1 |= (1<<6);
	
	RTCCON |= 1<<RTCS1;
	RTCCON |= 1<<RTCS0;
	RTCH = 0xff;
	RTCL = 0xff;
	
	initSerial(384);		// Serial baudrate 38400
	SSTAT |= (1<<CIDIS);	// Combined interrupt disabled, RX and TX generate different interrupts

	cc1100_init();

	ESR = 1;
	EA = 1;
	

/* ============================================== Main Loop ==================================================== */

	/* This is a state machine with the variable mode as the state variable 
		We have 2 main modes: 	
			cmd_mode means we are receiving a command via radio from Betty
				We then send this command to MPD via serial line
			response_mode means we are getting an answer from MPD via serial line
				We send this answer via radio to Betty
	
		Communication over serial line is no problem. It is full duplex, so sending and receiving
		can occur simultaneously and independently.
	
		But the radio link is half duplex. We can only send or receive but not both simultaneously.
		So we have to know when to switch between sending and receiving.
		The application is a server/client application.
		The client (Betty) sends a request, the server answers.
		The server (currently) never sends bytes without a previous request.
		In each case only the sender knows, when his transmission is finished.
		(The requests have different length, as well as the answers).
		But we can assume that requests and answers are finished within a short time frame,
		say 1.5 seconds after communication has started.
		So we implement a protocol, where the current sender finishes his message with a unique byte
		that does not normally occur in the message, in this case EOT.
		So, in this loop when an EOT is seen, the following happens:
			cmd_mode: drop EOT, Reset radio_RX buffer, reset radio_TX buffer, Switch to sending mode
			response_mode: Flush radio_TX buffer, send EOT over radio, reset radio_RX buffer, switch to receiving mode 
	
		The next complication is that the processor has a very limited RAM (256 Bytes) and the server may send very long 
		responses.
		We can only buffer a small amount and have to inform the server when the serial line can not receive
		more bytes. This is done via ETX and ACK bytes.
		The server (MPD) sends only small portions of data (16 bytes) at a time and then sends an ETX.
		If we still have room for more data, we send back an ACK character over serial line.
		Else we wait until part of our buffer has been sent out over radio link and is free again.
	
	
		We should implement a simple main loop here.
		Within the loop some tasks are called, which all return relatively fast (say 10 ms or shorter).
		That way we create the illusion of multitasking.
	
		Task 1: check_etx
			Checks if we got an ETX character. If we have room in our radio-tx-buffer, we send an ACK. 
			Takes less than 0.3 ms, even if serial TX is busy.
	
		Task 2: handle_radio_tx
			If there is a packet in our radio-tx-buffer ready to be sent (either because max packet length is 
				reached or because EOT was received)
				Move bytes from radio-tx-buffer to CC1100 TXFIFO.
				If all bytes have been moved to CC1100 FIFO and CC1100 is ready, strobe CC1100 to start sending.
	
		Task 3: receive bytes via radio
			when a complete packet has been received via radio, send the bytes to serial out. 
			When we have seen an EOT, mark the CC1100 as ready for TX.
		
	
	*/
	
	got_etx = 0;
	got_eot = 0;

	buffer_init();
		
	start_rx();								// Start receiving via radio
		
	/* Init Watchdog Timer */
	WDL = 0xFF;			// WDT counter
	EA = 0;
	WDCON = 0xE5;		// Start WDT
	WFEED1 = 0xA5;
	WFEED2 = 0x5A;
	EA = 1;
	
	while (1) {						/* Forever: */
		
		feed_wd();
		
		/* Check if mpdtool has sent ETX. Sends ACK if there is room in buffer */
		check_etx();
		
		/* Our radio link is only half duplex. We decide here if we receive or transmit 
			We are normally in state RADIO_RX to not miss a packet from Betty.
			Only if we have a packet to send, do we switch to mode RADIO_TX.
			Right after finishing sending the packet, mode RADIO_RX is reentered. 
		*/
		
		if (radio_mode == RADIO_RX)
			/* Test and handle input over wireless */
			check_radio_input();
			
		else
			/* reenter RADIO_RX mode if possible */
			re_enter_rx();
		
		/* Handle transmitting of buffer contents to CC1100 buffer and start sending 
			but only if we do not have a packet currently in transmission
		*/		
		if (radio_mode != RADIO_TX)
			handle_tx();
		
	};

}



