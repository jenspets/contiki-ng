/*
 * Copyright (c) 2006, Swedish Institute of Computer Science
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/**
 * \file
 *         Device driver for the ST M25P80 40MHz 1Mbyte external memory.
 * \author
 *         Bj�rn Gr�nvall <bg@sics.se>
 *
 *         Data is written bit inverted (~-operator) to flash so that
 *         unwritten data will read as zeros (UNIX style).
 */


#include "contiki.h"
#include <stdio.h>
#include <string.h>

#include "dev/spi-legacy.h"
#include "dev/xmem.h"
#include "dev/watchdog.h"

#if 0
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...) do {} while (0)
#endif

#if 1
#define CANARY
#define CANARY1 {0xe7, 0x1d, 0xe5, 0xce}
#define CANARY2 {0xca, 0xfe, 0xba, 0xbe}
#define CANARY3 {0xde, 0xad, 0xbe, 0xef}
#endif

#define  SPI_FLASH_INS_WREN        0x06
#define  SPI_FLASH_INS_WRDI        0x04
#define  SPI_FLASH_INS_RDSR        0x05
#define  SPI_FLASH_INS_WRSR        0x01
#define  SPI_FLASH_INS_READ        0x03
#define  SPI_FLASH_INS_FAST_READ   0x0b
#define  SPI_FLASH_INS_PP          0x02
#define  SPI_FLASH_INS_SE          0xd8
#define  SPI_FLASH_INS_BE          0xc7
#define  SPI_FLASH_INS_DP          0xb9
#define  SPI_FLASH_INS_RES         0xab
/*---------------------------------------------------------------------------*/
#ifdef CANARY
#define SECTOR_SIZE 0x10000
#define PAGE_SIZE 0x100

static void
check_write_canary(const unsigned char *b, int size, unsigned long offset){
  unsigned long i;
  char c1[4] = CANARY1;
  char c2[4] = CANARY2;
  char c3[4] = CANARY3;

  for (i=0; size>=4 && i<size-4; i++){
    if (!memcmp(b+i, c1, 4)){
      printf("CANARY1 W P 0x%08lx O 0x%08lx\n", offset & ~(PAGE_SIZE-1), i);
    } else if (!memcmp(b+i, c2, 4)){
      printf("CANARY2 W P 0x%08lx O 0x%08lx\n", offset & ~(PAGE_SIZE-1), i);
    } else if (!memcmp(b+i, c3, 4)){
      printf("CANARY3 W P 0x%08lx O 0x%08lx\n", offset & ~(PAGE_SIZE-1), i);
    }
  }
}

static void
check_erase_canary(unsigned long offset){
  unsigned long  i, p;
  char c1[4] = CANARY1;
  char c2[4] = CANARY2;
  char c3[4] = CANARY3;
  char b[PAGE_SIZE];
  
  for (p=offset; p<offset+SECTOR_SIZE; p+=PAGE_SIZE){
    if (xmem_pread(b, PAGE_SIZE, p) != PAGE_SIZE){
      printf("ERROR READING PAGE: %08lx\n", p);
    }
    
    for (i=0; i<PAGE_SIZE-4; i++){
      if (!memcmp(b+i, c1, 4)){
	printf("CANARY1 E P 0x%08lx S 0x%08lx\n", p, offset);
      } else if (!memcmp(b+i, c2, 4)){
	printf("CANARY2 E P 0x%08lx S 0x%08lx\n", p, offset);
      } else if (!memcmp(b+i, c3, 4)){
	printf("CANARY3 E P 0x%08lx S 0x%08lx\n", p, offset);
      }
    }
  }
}
#endif
/*---------------------------------------------------------------------------*/
static void
write_enable(void)
{
  int s;

  s = splhigh();
  SPI_FLASH_ENABLE();
  
  SPI_WRITE(SPI_FLASH_INS_WREN);

  SPI_FLASH_DISABLE();
  splx(s);
}
/*---------------------------------------------------------------------------*/
static unsigned
read_status_register(void)
{
  unsigned char u;

  int s;

  s = splhigh();
  SPI_FLASH_ENABLE();
  
  SPI_WRITE(SPI_FLASH_INS_RDSR);

  SPI_FLUSH();
  SPI_READ(u);

  SPI_FLASH_DISABLE();
  splx(s);

  return u;
}
/*---------------------------------------------------------------------------*/
/*
 * Wait for a write/erase operation to finish.
 */
static unsigned
wait_ready(void)
{
  unsigned u;
  do {
    u = read_status_register();
    watchdog_periodic();
  } while(u & 0x01);		/* WIP=1, write in progress */
  return u;
}
/*---------------------------------------------------------------------------*/
/*
 * Erase 64k bytes of data. It takes about 1s before WIP goes low!
 */
static void
erase_sector(unsigned long offset)
{
  int s;
  
  wait_ready();
#ifdef CANARY
  /* Need to read the pages in the sector to search for canary */
  check_erase_canary(offset);
#endif
  write_enable();

  s = splhigh();
  SPI_FLASH_ENABLE();
  
  SPI_WRITE_FAST(SPI_FLASH_INS_SE);
  SPI_WRITE_FAST(offset >> 16);	/* MSB */
  SPI_WRITE_FAST(offset >> 8);
  SPI_WRITE_FAST(offset >> 0);	/* LSB */
  SPI_WAITFORTx_ENDED();

  SPI_FLASH_DISABLE();
  splx(s);
}
/*---------------------------------------------------------------------------*/
/*
 * Initialize external flash *and* SPI bus!
 */
void
xmem_init(void)
{
  int s;
  spi_init();

  P4DIR |= BV(FLASH_CS) | BV(FLASH_HOLD) | BV(FLASH_PWR);
  P4OUT |= BV(FLASH_PWR);       /* P4.3 Output, turn on power! */

  /* Release from Deep Power-down */
  s = splhigh();
  SPI_FLASH_ENABLE();
  SPI_WRITE_FAST(SPI_FLASH_INS_RES);
  SPI_WAITFORTx_ENDED();
  SPI_FLASH_DISABLE();		/* Unselect flash. */
  splx(s);

  SPI_FLASH_UNHOLD();
}
/*---------------------------------------------------------------------------*/
int
xmem_pread(void *_p, int size, unsigned long offset)
{
  unsigned char *p = _p;
  const unsigned char *end = p + size;
  int s;

  wait_ready();

  s = splhigh();
  SPI_FLASH_ENABLE();

  SPI_WRITE_FAST(SPI_FLASH_INS_READ);
  SPI_WRITE_FAST(offset >> 16);	/* MSB */
  SPI_WRITE_FAST(offset >> 8);
  SPI_WRITE_FAST(offset >> 0);	/* LSB */
  SPI_WAITFORTx_ENDED();
  
  SPI_FLUSH();
  for(; p < end; p++) {
    unsigned char u;
    SPI_READ(u);
    *p = ~u;
  }

  SPI_FLASH_DISABLE();
  splx(s);

  return size;
}
/*---------------------------------------------------------------------------*/
static const unsigned char *
program_page(unsigned long offset, const unsigned char *p, int nbytes)
{
  const unsigned char *end = p + nbytes;
  int s;

  wait_ready();
#ifdef CANARY
  check_write_canary(p, nbytes, offset);
#endif
  write_enable();

  s = splhigh();
  SPI_FLASH_ENABLE();
  
  SPI_WRITE_FAST(SPI_FLASH_INS_PP);
  SPI_WRITE_FAST(offset >> 16);	/* MSB */
  SPI_WRITE_FAST(offset >> 8);
  SPI_WRITE_FAST(offset >> 0);	/* LSB */

  for(; p < end; p++) {
    SPI_WRITE_FAST(~*p);
  }
  SPI_WAITFORTx_ENDED();

  SPI_FLASH_DISABLE();
  splx(s);

  return p;
}
/*---------------------------------------------------------------------------*/
int
xmem_pwrite(const void *_buf, int size, unsigned long addr)
{
  const unsigned char *p = _buf;
  const unsigned long end = addr + size;
  unsigned long i, next_page;

  for(i = addr; i < end;) {
    next_page = (i | 0xff) + 1;
    if(next_page > end) {
      next_page = end;
    }
    p = program_page(i, p, next_page - i);
    i = next_page;
  }

  return size;
}
/*---------------------------------------------------------------------------*/
int
xmem_erase(long size, unsigned long addr)
{
  unsigned long end = addr + size;

  if(size % XMEM_ERASE_UNIT_SIZE != 0) {
    PRINTF("xmem_erase: bad size\n");
    return -1;
  }

  if(addr % XMEM_ERASE_UNIT_SIZE != 0) {
    PRINTF("xmem_erase: bad offset\n");
    return -1;
  }

  for (; addr < end; addr += XMEM_ERASE_UNIT_SIZE) {
    erase_sector(addr);
  }

  return size;
}
/*---------------------------------------------------------------------------*/
