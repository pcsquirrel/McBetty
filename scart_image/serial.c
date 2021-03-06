/*
    serial.c
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

#include "serial.h"
#include <P89LPC932.h>

void initSerial(unsigned short baud) {
				// TODO disable reception here
				// TODO disable baud rate generator here
        SCON   = 0x52;		// 0101 0010 = Mode 8N1, enable reception, TxIntFlag=1 ? 
        SSTAT |= 0x80;		// 1000 0000 = Double buffering mode
        switch (baud) {
        
        case 96:
                BRGR0  = 0x88; 
                BRGR1  = 0x02;
                break;
        case 192:
                BRGR0  = 0xBC; 
                BRGR1  = 0x02;
                break;
        case 384:
                BRGR0  = 0x56; 
                BRGR1  = 0x01;
                break;
        case 576:
                BRGR0  = 0xDE; 
                BRGR1  = 0x00;
                break;
        case 1152:
                BRGR0  = 0x67; 
                BRGR1  = 0x00;
                break;
        }
        BRGCON = 0x03;		// Select the baud rate generator as timing source and enable it
        
        TI = 1;
        
}


/* Potentially infinite waiting time ! */
void send_byte(unsigned char h) {
        while (!TI);
        TI = 0; 
        SBUF = h;  
}

// unused
#if 0
void send_string(const unsigned char* string) {
 
        unsigned char i=0;
        
        while (string[i] != 0) {
                while (!TI);		// Wait
                TI = 0;
                SBUF = string[i++];		  
        } 
}

void send_bytes(unsigned char* h, unsigned char l) {
 
        unsigned char i=0;
        
        while (i<l) {
                while (!TI);
                TI = 0; 
                SBUF = h[i++];
        } 
}

void send_hex(unsigned char c) {
         
        unsigned char cn;
        
        cn = (c>>4) & 0x0f;
        cn += 0x30;
        if (cn > 0x39)
                cn += 0x07;
        while (!TI);
        TI = 0;
        SBUF = cn;
        
        cn = c & 0x0f;
        cn += 0x30;
        if (cn > 0x39)
                cn += 0x07;
        while (!TI);
        TI = 0;
        SBUF = cn;
        
} 
#endif



