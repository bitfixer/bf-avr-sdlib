/*
    AVRSDLib.c
    Simple SD Card library for AVR Microcontrollers 
    Copyright (C) 2012 Michael Hill

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
    
    Contact the author at bitfixer@bitfixer.com
    http://bitfixer.com
*/

#define F_CPU 8000000UL		//freq 8 MHz
#define BAUD 19200
#define MYUBRR F_CPU/16/BAUD-1
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "SPI_routines.h"
#include "SD_routines.h"
#include "UART_routines.h"
#include "FAT32.h"

#define SPI_PORT PORTB
#define SPI_CTL  DDRB
#define MISO     0x10

#define FNAMELEN    39

void port_init(void)
{
    SPI_CTL = ~MISO;
    SPI_PORT = 0xff;
}

//call this routine to initialize all peripherals
void init_devices(void)
{
 cli();  //all interrupts disabled
 port_init();
 spi_init();
 uart0_init(MYUBRR);

 MCUCR = 0x00;
}

int main(void)
{
    unsigned char fileName[11];
    unsigned char progname[FNAMELEN];
    unsigned char tmp[50];
    unsigned char rdchar,rdbus,tmp1,tmp2,ctl;
    unsigned char option, error, data, FAT32_active;
    unsigned char getting_filename;
    unsigned char filename_position;
    unsigned char islong;
    unsigned char address;
    int bytes_to_send;
    unsigned char i;
    
    
    init_devices();
    
    int cardType = 0;

    struct dir_Structure *dir;
    unsigned long cluster, byteCounter = 0, fileSize, firstSector;
    unsigned int k;
    unsigned char j,sending;
    unsigned char response;
    unsigned char startline;
    unsigned int retry;
    unsigned int dir_start;
    unsigned int dirlength;
    unsigned char gotname;
    unsigned char savefile;
    unsigned char filenotfound;
    unsigned char initcard;
    unsigned char buscmd;
    
    unsigned long cl;
    getting_filename = 0;
    filename_position = 0;
    
    transmitString("initialize card");

    // initialize SD card
    for (i=0; i<10; i++)
    {
      error = SD_init();
      if(!error) break;
    }
    
    if (!error)
    {
        transmitString("card initialized.");
        error = getBootSectorData (); //read boot sector and keep necessary data in global variables
    
        // look for firmware file
        progname[0] = 'l';
        progname[1] = 'o';
        progname[2] = 'n';
        progname[3] = 'g';
        progname[4] = '*';
        progname[5] = 0;
        //dir = findFilesL(GET_FILE, progname, 0);
        dir = findFiles2(GET_FILE, progname, 1, _rootCluster);
        transmitString("I am back");
        
        if (dir != 0)
        {
            // found firmware file
            /*
            transmitString("found directory..\r\n");
            
            transmitString("firstClusterHI: ");
            transmitHex(INT, dir->firstClusterHI);
            transmitString("\r\n");
            
            transmitString("firstClusterLO: ");
            transmitHex(INT, dir->firstClusterLO);
            transmitString("\r\n");
            
            
            transmitString("fileSize: ");
            transmitHex(LONG, dir->fileSize);
            transmitString("\r\n");
            */
            
            //unsigned long cluster = (unsigned long)dir->firstClusterLO;
            //dir = findFiles2(GET_FILE, progname, 1, cluster);
            
            
            
            
        } 
        else 
        {
            //transmitString("no firmware.");
        }
        
        progname[0] = 'M';
        progname[1] = 'Y';
        progname[2] = 'F';
        progname[3] = 'I';
        progname[4] = 'L';
        progname[5] = 'E';
        progname[6] = ' ';
        progname[7] = ' ';
        progname[8] = ' ';
        progname[9] = 'B';
        progname[10] = 'I';
        progname[11] = 'N';
        
        openFileForWriting(progname);
        for (k = 0; k < 512; k++)
        {
            _buffer[k] = k % 256;
        }
        transmitString("writing..\r\n");
        writeBufferToFile();
        closeFile();
    }
    else
    {
        transmitString("no card found.");
    }
    
    while(1)
    {
        asm("nop;");
    }
}