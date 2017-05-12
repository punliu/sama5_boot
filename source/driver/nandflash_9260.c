/* ----------------------------------------------------------------------------
 *         ATMEL Microcontroller Software Support
 * ----------------------------------------------------------------------------
 * Copyright (c) 2006, Atmel Corporation

 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "common.h"
#include "hardware.h"
#include "board.h"
#include "arch/at91_pio.h"
#include "gpio.h"
#include "debug.h"
#include "nand.h"
#include "hamming.h"
#include "timer.h"
#include "div.h"

#define ECC_CORRECT_ERROR  0xfe

static struct nand_chip nand_ids[] = {
	{0xecda, 0x800, 0x20000, 0x800, 0x40, 0x0},	/* Samsung K9F2G08U0M 256MB */
	{0xecaa, 0x800, 0x20000, 0x800, 0x40, 0x0},	/* Samsung K9F2G08U0A 256MB */
	{0x2cca, 0x800, 0x20000, 0x800, 0x40, 0x1},	/* Micron MT29F2G16AAB 256MB */
	{0x2cda, 0x800, 0x20000, 0x800, 0x40, 0x0},	/* Micron MT29F2G08AAC 256MB  */
	{0x2caa, 0x800, 0x20000, 0x800, 0x40, 0x0},	/* Micron MT29F2G08ABD 256MB */
	{0x2c38, 0x800, 0x80000, 0x1000, 0xe0, 0x0},	/* Mircon MT29H8G08ACAH1 1GB */
	{0,}
};

static struct nand_ooblayout nand_oob_layout;

/*
 * NAND Commands
 */

/* 8 bits devices */
static void nand_command(unsigned char cmd)
{
	volatile unsigned long ioaddr = (unsigned long)CONFIG_SYS_NAND_BASE
						| CONFIG_SYS_NAND_MASK_CLE;

	writeb(cmd, ioaddr);
}

static void nand_address(unsigned char addr)
{
	volatile unsigned long ioaddr = (unsigned long)CONFIG_SYS_NAND_BASE
						| CONFIG_SYS_NAND_MASK_ALE;

	writeb(addr, ioaddr);
}

static unsigned char read_byte(void)
{
	return readb((unsigned long)CONFIG_SYS_NAND_BASE);
}

/* 16 bits devices */
static void nand_command16(unsigned short cmd)
{
	volatile unsigned long ioaddr = (unsigned long)CONFIG_SYS_NAND_BASE
						| CONFIG_SYS_NAND_MASK_CLE;

	writew(cmd, ioaddr);
}

static void nand_address16(unsigned short addr)
{
	volatile unsigned long ioaddr = (unsigned long)CONFIG_SYS_NAND_BASE
						| CONFIG_SYS_NAND_MASK_ALE;

	writew(addr, ioaddr);
}

static unsigned short read_word(void)
{
	return readw((unsigned long)CONFIG_SYS_NAND_BASE);
}

static void nand_wait_ready(void)
{
	unsigned int timeout = 10000;

	nand_command(CMD_STATUS);
	while((!(read_byte() & STATUS_READY)) && timeout--);
}

static void nand_cs_enable(void)
{
	pio_set_value(CONFIG_SYS_NAND_ENABLE_PIN, 0);
}

static void nand_cs_disable(void)
{
	pio_set_value(CONFIG_SYS_NAND_ENABLE_PIN, 1);
}

static void config_nand_ooblayout(struct nand_ooblayout *layout, struct nand_chip *chip)
{
	unsigned int i;

	switch (chip->pagesize) {
	case 256:
		layout->badblockpos = 5;
		layout->eccbytes = 3;
		layout->oobavail_offset = 6;
		break;

	case 512:
		layout->badblockpos = 5;
		layout->eccbytes = 6;
		layout->oobavail_offset = 6;
		break;

	case 2048:
		layout->badblockpos = 0;
		layout->eccbytes = 24;
		layout->oobavail_offset = 1;
		break;

	case 4096:
		layout->badblockpos = 0;
		layout->eccbytes = 48;
		layout->oobavail_offset = 1;
		break;

	default:
		break;
	}

	for (i = 0; i < layout->eccbytes; i++)
		layout->eccpos[i] = chip->oobsize - layout->eccbytes + i;

	layout->oobavailbytes = chip->oobsize - layout->eccbytes - layout->oobavail_offset;
}

static void nand_info_init(struct nand_info *nand, struct nand_chip *chip)
{
	/* number of blocks in device */
	nand->numblocks = chip->numblocks;
	/* number of data bytes in a block */
	nand->blocksize = chip->blocksize;
	/* number of bytes in page area */
	nand->pagesize = chip->pagesize;
	/* number of pages in block */
	nand->pages_block = div(nand->blocksize, nand->pagesize);
	/* number of pages in device */
	nand->pages_device = nand->numblocks * nand->pages_block;
	/* number of bytes in oob area */
	nand->oobsize = chip->oobsize;
	/* Total number of bytes in a sector */
	nand->sectorsize = nand->pagesize + nand->oobsize;
	/* the layout of the spare area */
	config_nand_ooblayout(&nand_oob_layout, chip);
	nand->ecclayout = &nand_oob_layout;
	/* data bus width (8/16 bits) */
	nand->buswidth = chip->buswidth;
	if (nand->buswidth)
		nand->ecclayout->badblockpos *= 2;
}

static void nandflash_reset(void)
{
	nand_cs_enable();

	nand_command(CMD_RESET);

	nand_wait_ready();

	nand_cs_disable();
}

static struct nand_chip *nand_find_type(void)
{
	unsigned int chipid, i = 0;
	unsigned char manf_id, dev_id;
	struct nand_chip *chip = NULL;

	nand_cs_enable();
	nand_command(CMD_READID);
	nand_address(0x0);

	manf_id = read_byte();
	dev_id = read_byte();

	nand_cs_disable();

	chipid = (manf_id << 8) | dev_id;

	while (nand_ids[i].chip_id != 0) {
		if (nand_ids[i].chip_id == chipid) {
			chip = &nand_ids[i];
			break;
		}
		i++;
	}

	return chip;
}

static int nandflash_get_type(struct nand_info *nand)
{
	struct nand_chip *chip;

	nandflash_reset();

	chip = nand_find_type();
	if (chip == NULL) {
		dbg_log(1, "Not Found the NANDFlash!\n\r");
		return -1;
	}

	nand_info_init(nand, chip);

	if (nand->buswidth == 0)
		nandflash_config_buswidth(0);
	else
		nandflash_config_buswidth(1);

	return 0;
}

static void write_column_address(struct nand_info *nand,
					unsigned int column_address)
{
	volatile unsigned int page_size = nand->pagesize;

	if (nand->buswidth)
		column_address >>= 1;

	while (page_size > 2) {
		if (nand->buswidth)
			nand_address16(column_address & 0xff);
		else
			nand_address(column_address & 0xff);

		page_size >>= 8;
		column_address >>= 8;
	}
}

static void write_row_address(struct nand_info *nand, unsigned int row_address)
{
	volatile unsigned int num_pages = nand->pages_device;

	while(num_pages) {
		if (nand->buswidth)
			nand_address16(row_address & 0xff);
		else
			nand_address(row_address & 0xff);

		num_pages >>= 8;
		row_address >>= 8;
	}
}

#ifdef NANDFLASH_SMALL_BLOCKS
static int nand_read_sector(struct nand_info *nand,
			unsigned int row_address,
			unsigned char *buffer,
			unsigned int zone_flag)
{
	unsigned int readbytes, i;
	unsigned char command;

	switch (zone_flag) {
	case ZONE_DATA:
		readbytes = nand->pagesize;
		command = CMD_READ_A0;
		break;

	case ZONE_INFO:
		readbytes = nand->oobsize;
		buffer += nand->pagesize;
		command = CMD_READ_C;
		break;

	case ZONE_DATA | ZONE_INFO:
		readbytes = nand->sectorsize;
		command = CMD_READ_A0;
		break;

	default:
		return -1;
	}

	nand_cs_enable();

	/* Write specific command, Read from start */
	if (nand->buswidth)
		nand_command16(command);
	else
		nand_command(command);

	if (nand->buswidth) {
		nand_address16(0x00);
		nand_address16((row_address >> 0) & 0xff);
		nand_address16((row_address >> 8) & 0xff);
		nand_address16((row_address >> 16) & 0xff);
	} else {
		nand_address(0x00);
		nand_address((row_address >> 0) & 0xff);
		nand_address((row_address >> 8) & 0xff);
		nand_address((row_address >> 16) & 0xff);
	}

	nand_wait_ready();
	nand_command(CMD_READ_A0);

	if (nand->buswidth) {
		for (i = 0; i < readbytes / 2; i++) {
			*((short *)buffer) = read_word();
			buffer += 2;
		}
	} else {
		if (command == CMD_READ_C) {
			for (i = 0; i < readbytes; i++) {
				*buffer = read_byte();
				buffer++;
			}
		} else {
			for (i = 0; i < readbytes / 2; i++) {
				*buffer = read_byte();
				buffer++;
			}

			nand_command(CMD_READ_A1);
			nand_address(0x00);
			nand_address((row_address >> 0) & 0xff);
			nand_address((row_address >> 8) & 0xff);
			nand_address((row_address >> 16) & 0xff);

			nand_wait_ready();
			nand_command(CMD_READ_A0);

			for (i = 0; i < (readbytes / 2); i++) {
				*buffer = read_byte();
				buffer++;
			}
		}
	}

	nand_cs_disable();

	return 0;
}

#else /* large blocks */
static int nand_read_sector(struct nand_info *nand,
			unsigned int row_address,
			unsigned char *buffer,
			unsigned int zone_flag)
{
	unsigned int readbytes, i;
	unsigned int column_address;
	int ret = 0;

	column_address = 0x00;
	switch (zone_flag) {
	case ZONE_DATA:
		readbytes = nand->pagesize;
		break;

	case ZONE_INFO:
		readbytes = nand->oobsize;
		buffer += nand->pagesize;
		column_address = nand->pagesize;
		break;

	case ZONE_DATA | ZONE_INFO:
		readbytes = nand->sectorsize;
		break;

	default:
		return -1;
	}

	nand_cs_enable();

	if (nand->buswidth)
		nand_command16(CMD_READ_1);
	else
		nand_command(CMD_READ_1);

	write_column_address(nand, column_address);
	write_row_address(nand, row_address);

	if (nand->buswidth)
		nand_command16(CMD_READ_2);
	else
		nand_command(CMD_READ_2);

	nand_wait_ready();
	if (nand->buswidth)
		nand_command16(CMD_READ_1);
	else
		nand_command(CMD_READ_1);

	/* Read loop */
	if (nand->buswidth)
		for (i = 0; i < readbytes / 2; i++) {
			*((short *)buffer) = read_word();
			buffer += 2;
		}
	else
		for (i = 0; i < readbytes; i++)
			*buffer++ = read_byte();

	nand_cs_disable();

	return ret;
}
#endif /* #ifdef NANDFLASH_SMALL_BLOCKS */

static int nand_check_badblock(struct nand_info *nand,
				unsigned int block,
				unsigned char *buffer)
{
	unsigned int page;
	unsigned int row_address = block * nand->pages_block;

	/* Read the first page and second page oob zone to detect if block is bad */
	for (page = 0; page < 2; page++) {
		nand_read_sector(nand, row_address + page, buffer, ZONE_INFO);
		if (*(buffer + nand->pagesize + nand->ecclayout->badblockpos) != 0xff)
			return -1;
	}

	return 0;
}

static void nand_read_ecc(struct nand_ooblayout *ooblayout,
				unsigned char *buffer,
				unsigned char *ecc)
{
	unsigned int i;

	for (i = 0; i < ooblayout->eccbytes; i++)
		ecc[i] = buffer[ooblayout->eccpos[i]];
}

static int nand_read_page(struct nand_info *nand,
			unsigned int block,
			unsigned int page,
			unsigned int zone_flag,
			unsigned char *buffer)
{
	int retval;
	unsigned char hamming[48];
	unsigned char error;
	unsigned int row_address = block * nand->pages_block + page;

	retval = nand_read_sector(nand, row_address, buffer, ZONE_DATA | ZONE_INFO);
	if (retval)
		return -1;

	nand_read_ecc(nand->ecclayout, buffer + nand->pagesize, hamming);

	error = Hamming_Verify256x(buffer, nand->pagesize, hamming);
	if (error && (error != Hamming_ERROR_SINGLEBIT)) {
		dbg_log(1, "Hamming ECC error!\n\r");
		return ECC_CORRECT_ERROR;
	}

	return 0;
}

#ifdef CONFIG_NANDFLASH_RECOVERY
static int nand_erase_block0(struct nand_info *nand)
{
	unsigned int row_address = 0;

	nand_cs_enable();

	nand_command(CMD_ERASE_1);
	write_row_address(nand, row_address);
	nand_command(CMD_ERASE_2);

	udelay(2000);

	nand_wait_ready();

	nand_cs_disable();

	return 0;
}

static int nandflash_recovery(struct nand_info *nand)
{
	int ret = -1;

	/*
	 * If Recovery Button is pressed during boot sequence,
	 * erase nandflash block0
	*/
	if ((pio_get_value(CONFIG_SYS_RECOVERY_BUTTON_PIN)) == 0) {
		dbg_log(1, "Nand: The recovery button (%s) is pressed\n\r",
				RECOVERY_BUTTON_NAME);
		dbg_log(1, "Nand: The block 0 is erasing ...\n\r");

		ret = nand_erase_block0(nand);
	}

	return ret;
}
#endif /* #ifdef CONFIG_NANDFLASH_RECOVERY */

int load_nandflash(struct image_info *img_info)
{
	struct nand_info nand;
	unsigned int offset = img_info->offset;
	unsigned int size = img_info->length;
	unsigned char *buffer = img_info->dest;

	unsigned int length, readsize;
	unsigned int block = 0;
	unsigned int page;
	unsigned int start_page = 0;
	unsigned int end_page;
	unsigned int numpages = 0;
	unsigned int offsetpage = 0;
	int ret;

	nandflash_hw_init();

	if (nandflash_get_type(&nand))
		return -1;

#ifdef CONFIG_NANDFLASH_RECOVERY
	if (nandflash_recovery(&nand) == 0)
		return -2;
#endif

	dbg_log(1, "Nand: Copy %d bytes from %d to %d\r\n", size, offset, buffer);

	division(offset, nand.blocksize, &block, &start_page);
	start_page = div(start_page, nand.pagesize);

	length = size;
	while (length > 0) {
		/* read a buffer corresponding to a block */
		if (length < nand.blocksize)
			readsize = length;
		else
			readsize = nand.blocksize;

		/* adjust the number of pages to read */
		division(readsize, nand.pagesize, &numpages, &offsetpage);
		if (offsetpage)
			numpages++;

		end_page = start_page + numpages;

		/* check the bad block */
		while (1) {
			if (nand_check_badblock(&nand, block, buffer) != 0) {
				block++; /* skip this block */
				dbg_log(1, "Bad block: #%d\n\r", block);
			} else
				break;
		}

		/* read pages of a block */
		for (page = start_page; page < end_page; page++) {
			ret = nand_read_page(&nand, block, page, ZONE_DATA, buffer);
			if (ret == ECC_CORRECT_ERROR)
				return -1;
			else
				buffer += nand.pagesize;
		}
		length -= readsize;

		block++;
		start_page = 0;
	}

	return 0;
}
