/* This is a simple ATA disk dirver for NCTU OSDI course.
 *  Reference: http://wiki.osdev.org/ATA_PIO_Mode#ATA_Driver, http://wiki.osdev.org/IDE
 */

#include "disk.h"
#include <inc/x86.h>
#include <inc/mmu.h>

#define SECTOR_SIZE 512
#define FALSE 0
#define TRUE 1

unsigned char ide_buf[2048] = {0};
unsigned static char ide_irq_invoked = 0;

unsigned static char ide_status = 0;

void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2, unsigned int BAR3, unsigned int BAR4);
unsigned char ide_read(unsigned char channel, unsigned char reg);
void ide_write(unsigned char channel, unsigned char reg, unsigned char data);
void ide_read_buffer(unsigned char channel, unsigned char reg, unsigned int buffer, unsigned int quads);
unsigned char ide_print_error(unsigned int drive, unsigned char err);

unsigned char ide_ata_access(unsigned char direction, unsigned char drive, unsigned int lba, 
		unsigned char numsects, unsigned short selector, unsigned int edi);


int disk_init()
{
	static unsigned char init = FALSE;
	if(!init){
		ide_initialize(0x1F0, 0x3F6, 0x170, 0x376, 0x000);
		init = TRUE;
	}
	return 0;
}

void disk_test()
{
	unsigned char buf[SECTOR_SIZE];
	int i, j, ret;

	for (i = 0; i < SECTOR_SIZE; i++)
		if (i % 2)
			buf[i] = 0x55;
		else
			buf[i] = 0xAA;

	for (i = 0; i < 10; i++)
	{
		ide_write_sectors(1, 1, i, (unsigned int)buf);
	}


	for (i = 0; i < 10; i++)
	{    
		for (j = 0; j < SECTOR_SIZE; j++)
			buf[j] = 0x0;
		ide_read_sectors(1, 1, i, (unsigned int)buf);
		for (j = 0; j < SECTOR_SIZE; j++)
		{
			if (j % 2)
			{
				if (buf[j] != 0x55)
					printk("Read error at sector %d address %d, read=%d!\n", i, j, buf[j]);
				assert(buf[j] == 0x55);
			}
			else
			{
				if (buf[j] != 0xAA)
					printk("Read error at sector %d address %d, read=%d!\n", i, j, buf[j]);
				assert(buf[j] == 0xAA);
			}
		}
	}

	printk("Disk test pass!\n");
}

int ide_read_sectors(unsigned char drive, unsigned char numsects, unsigned int lba,
		unsigned int edi) {
	int retVal = 0;
	// 1: Check if the drive presents:
	// ==================================
	if (drive > 3 || ide_devices[drive].Reserved == 0) ide_status = 0x1;      // Drive Not Found!

	// 2: Check if inputs are valid:
	// ==================================
	else if (((lba + numsects) > ide_devices[drive].Size) && (ide_devices[drive].Type == IDE_ATA))
		ide_status = 0x2;                     // Seeking to invalid position.

	// 3: Read in PIO Mode through Polling & IRQs:
	// ============================================
	else {
		unsigned char err;
		if (ide_devices[drive].Type == IDE_ATA)
		{
			err = ide_ata_access(ATA_READ, drive, lba, numsects, GD_KD, edi);
			//printk("read code=%d\n", err);
		}
		else if (ide_devices[drive].Type == IDE_ATAPI)
			panic("ATAPI not supported!");
		// for (i = 0; i < numsects; i++)
		//    err = ide_atapi_read(drive, lba + i, 1, es, edi + (i*2048));
		ide_status = ide_print_error(drive, err);
	}
	// printk("ide_status = %x ,min:%x\n",ide_status,-0x2);
	return -ide_status;
}

int ide_write_sectors(unsigned char drive, unsigned char numsects, unsigned int lba,
		unsigned int edi) {
	int retVal = 0;
	// 1: Check if the drive presents:
	// ==================================
	if (drive > 3 || ide_devices[drive].Reserved == 0)
		ide_status = 0x1;      // Drive Not Found!
	// 2: Check if inputs are valid:
	// ==================================
	else if (((lba + numsects) > ide_devices[drive].Size) && (ide_devices[drive].Type == IDE_ATA))
		ide_status = 0x2;                     // Seeking to invalid position.
	// 3: Read in PIO Mode through Polling & IRQs:
	// ============================================
	else {
		unsigned char err;
		if (ide_devices[drive].Type == IDE_ATA)
			err = ide_ata_access(ATA_WRITE, drive, lba, numsects, GD_KD, edi);
		else if (ide_devices[drive].Type == IDE_ATAPI)
			err = 4; // Write-Protected.
		ide_status = ide_print_error(drive, err);
	}

	return -ide_status;
}

static void __delay(int ms)
{
	int i;
	for( i = 0; i < 10*ms; i++){}
}

void ide_initialize(unsigned int BAR0, unsigned int BAR1, unsigned int BAR2, unsigned int BAR3, unsigned int BAR4)
{
	int i, j, k, count = 0;

	// 1- Detect I/O Ports which interface IDE Controller:
	channels[ATA_PRIMARY  ].base  = (BAR0 & 0xFFFFFFFC) + 0x1F0 * (!BAR0);
	channels[ATA_PRIMARY  ].ctrl  = (BAR1 & 0xFFFFFFFC) + 0x3F6 * (!BAR1);
	channels[ATA_SECONDARY].base  = (BAR2 & 0xFFFFFFFC) + 0x170 * (!BAR2);
	channels[ATA_SECONDARY].ctrl  = (BAR3 & 0xFFFFFFFC) + 0x376 * (!BAR3);
	channels[ATA_PRIMARY  ].bmide = (BAR4 & 0xFFFFFFFC) + 0; // Bus Master IDE
	channels[ATA_SECONDARY].bmide = (BAR4 & 0xFFFFFFFC) + 8; // Bus Master IDE
	// 2- Disable IRQs:
	ide_write(ATA_PRIMARY  , ATA_REG_CONTROL, 2);
	ide_write(ATA_SECONDARY, ATA_REG_CONTROL, 2);
	// 3- Detect ATA-ATAPI Devices:
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++) {

			unsigned char err = 0, type = IDE_ATA, status;
			ide_devices[count].Reserved = 0; // Assuming that no drive here.

			// (I) Select Drive:
			ide_write(i, ATA_REG_HDDEVSEL, 0xA0 | (j << 4)); // Select Drive.
			__delay(1); // Wait 1ms for drive select to work.
			// (II) Send ATA Identify Command:
			ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
			__delay(1); // This function should be implemented in your OS. which waits for 1 ms.
			// it is based on System Timer Device Driver.

			// (III) Polling:
			if (ide_read(i, ATA_REG_STATUS) == 0) {
				continue;
			} // If Status = 0, No Device.
			// printk("--Get device %d\n",i);
			while(1) {
				status = ide_read(i, ATA_REG_STATUS);
				if ((status & ATA_SR_ERR)) {err = 1; break;} // If Err, Device is not ATA.
				if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break; // Everything is right.
			}

			// (IV) Probe for ATAPI Devices:
			if (err != 0) {
				unsigned char cl = ide_read(i, ATA_REG_LBA1);
				unsigned char ch = ide_read(i, ATA_REG_LBA2);

				if (cl == 0x14 && ch ==0xEB)
					type = IDE_ATAPI;
				else if (cl == 0x69 && ch == 0x96)
					type = IDE_ATAPI;
				else
					continue; // Unknown Type (may not be a device).

				ide_write(i, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
				__delay(1);
			}

			// (V) Read Identification Space of the Device:
			ide_read_buffer(i, ATA_REG_DATA, (unsigned int) ide_buf, 128);

			// (VI) Read Device Parameters:
			ide_devices[count].Reserved     = 1;
			ide_devices[count].Type         = type;
			ide_devices[count].Channel      = i;
			ide_devices[count].Drive        = j;
			ide_devices[count].Signature    = *((unsigned short *)(ide_buf + ATA_IDENT_DEVICETYPE));
			ide_devices[count].Capabilities = *((unsigned short *)(ide_buf + ATA_IDENT_CAPABILITIES));
			ide_devices[count].CommandSets  = *((unsigned int *)(ide_buf + ATA_IDENT_COMMANDSETS));

			// (VII) Get Size:
			if (ide_devices[count].CommandSets & (1 << 26))
			{
				// Device uses 48-Bit Addressing:
				//printk("Device %d use 48-Bit Addressing\n", count);
				ide_devices[count].Size   = *((unsigned int *)(ide_buf + ATA_IDENT_MAX_LBA_EXT));
			}
			else{
				// Device uses CHS or 28-bit Addressing:
				//printk("Device %d use CHS Addressing\n", count);
				ide_devices[count].Size   = *((unsigned int *)(ide_buf + ATA_IDENT_MAX_LBA));
			}

			// (VIII) String indicates model of device (like Western Digital HDD and SONY DVD-RW...):
			for(k = 0; k < 40; k += 2) {
				ide_devices[count].Model[k] = ide_buf[ATA_IDENT_MODEL + k + 1];
				ide_devices[count].Model[k + 1] = ide_buf[ATA_IDENT_MODEL + k];}
			ide_devices[count].Model[40] = 0; // Terminate String.

			count++;
		}

	// 4- Print Summary:
	for (i = 0; i < 4; i++)
		if (ide_devices[i].Reserved == 1) {
			printk(" Slot %d found %s Drive %dMB - %s\n", i,
					(const char *[]){"ATA", "ATAPI"}[ide_devices[i].Type],         /* Type */
					ide_devices[i].Size / 2048 ,               /* Size */
					ide_devices[i].Model);
		}
}

/* This function reads/writes sectors from ATA-Drive. If direction is 0 we are reading, else we are writing.
   - drive is the drive number which can be from 0 to 3.
   - lba is the LBA address which allows us to access disks up to 2TB.
   - numsects is the number of sectors to be read, it is a char, as reading more than 256 sector immediately may performance issues. If numsects is 0, the ATA controller will know that we want 256 sectors.
   - selector is the segment selector to read from, or write to.
   - edi is the offset in that segment.
 */
unsigned char ide_ata_access(unsigned char direction, unsigned char drive, unsigned int lba, 
		unsigned char numsects, unsigned short selector, unsigned int edi) 
{
	unsigned char lba_mode /* 0: CHS, 1:LBA28, 2: LBA48 */, dma /* 0: No DMA, 1: DMA */, cmd;
	unsigned char lba_io[6];
	unsigned int  channel      = ide_devices[drive].Channel; // Read the Channel.
	unsigned int  slavebit      = ide_devices[drive].Drive; // Read the Drive [Master/Slave]
	unsigned int  bus = channels[channel].base; // Bus Base, like 0x1F0 which is also data port.
	unsigned int  words      = 256; // Almost every ATA drive has a sector-size of 512-byte.
	unsigned short cyl, i;
	unsigned char head, sect, err;

	ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN = (ide_irq_invoked = 0x0) + 0x02);

	// (I) Select one from LBA28, LBA48 or CHS;
	if (lba >= 0x10000000) { // Sure Drive should support LBA in this case, or you are
		// giving a wrong LBA.
		// LBA48:
		lba_mode  = 2;
		lba_io[0] = (lba & 0x000000FF) >> 0;
		lba_io[1] = (lba & 0x0000FF00) >> 8;
		lba_io[2] = (lba & 0x00FF0000) >> 16;
		lba_io[3] = (lba & 0xFF000000) >> 24;
		lba_io[4] = 0; // LBA28 is integer, so 32-bits are enough to access 2TB.
		lba_io[5] = 0; // LBA28 is integer, so 32-bits are enough to access 2TB.
		head      = 0; // Lower 4-bits of HDDEVSEL are not used here.
	} else if (ide_devices[drive].Capabilities & 0x200)  { // Drive supports LBA?
		// LBA28:
		lba_mode  = 1;
		lba_io[0] = (lba & 0x00000FF) >> 0;
		lba_io[1] = (lba & 0x000FF00) >> 8;
		lba_io[2] = (lba & 0x0FF0000) >> 16;
		lba_io[3] = 0; // These Registers are not used here.
		lba_io[4] = 0; // These Registers are not used here.
		lba_io[5] = 0; // These Registers are not used here.
		head      = (lba & 0xF000000) >> 24;
	} else {
		// CHS:
		lba_mode  = 0;
		sect      = (lba % 63) + 1;
		cyl       = (lba + 1  - sect) / (16 * 63);
		lba_io[0] = sect;
		lba_io[1] = (cyl >> 0) & 0xFF;
		lba_io[2] = (cyl >> 8) & 0xFF;
		lba_io[3] = 0;
		lba_io[4] = 0;
		lba_io[5] = 0;
		head      = (lba + 1  - sect) % (16 * 63) / (63); // Head number is written to HDDEVSEL lower 4-bits.
	}

	// (II) See if drive supports DMA or not;
	dma = 0; // We don't support DMA

	// (III) Wait if the drive is busy;
	while (ide_read(channel, ATA_REG_STATUS) & ATA_SR_BSY)
		; // Wait if busy.

	// (IV) Select Drive from the controller;
	if (lba_mode == 0)
		ide_write(channel, ATA_REG_HDDEVSEL, 0xA0 | (slavebit << 4) | head); // Drive & CHS.
	else
		ide_write(channel, ATA_REG_HDDEVSEL, 0xE0 | (slavebit << 4) | head); // Drive & LBA

	// (V) Write Parameters;
	if (lba_mode == 2) {
		ide_write(channel, ATA_REG_SECCOUNT1,   0);
		ide_write(channel, ATA_REG_LBA3,   lba_io[3]);
		ide_write(channel, ATA_REG_LBA4,   lba_io[4]);
		ide_write(channel, ATA_REG_LBA5,   lba_io[5]);
	}
	ide_write(channel, ATA_REG_SECCOUNT0,   numsects);
	ide_write(channel, ATA_REG_LBA0,   lba_io[0]);
	ide_write(channel, ATA_REG_LBA1,   lba_io[1]);
	ide_write(channel, ATA_REG_LBA2,   lba_io[2]);

	// (VI) Select the command and send it;
	if (lba_mode == 0 && dma == 0 && direction == 0) cmd = ATA_CMD_READ_PIO;
	if (lba_mode == 1 && dma == 0 && direction == 0) cmd = ATA_CMD_READ_PIO;   
	if (lba_mode == 2 && dma == 0 && direction == 0) cmd = ATA_CMD_READ_PIO_EXT;   
	if (lba_mode == 0 && dma == 1 && direction == 0) cmd = ATA_CMD_READ_DMA;
	if (lba_mode == 1 && dma == 1 && direction == 0) cmd = ATA_CMD_READ_DMA;
	if (lba_mode == 2 && dma == 1 && direction == 0) cmd = ATA_CMD_READ_DMA_EXT;
	if (lba_mode == 0 && dma == 0 && direction == 1) cmd = ATA_CMD_WRITE_PIO;
	if (lba_mode == 1 && dma == 0 && direction == 1) cmd = ATA_CMD_WRITE_PIO;
	if (lba_mode == 2 && dma == 0 && direction == 1) cmd = ATA_CMD_WRITE_PIO_EXT;
	if (lba_mode == 0 && dma == 1 && direction == 1) cmd = ATA_CMD_WRITE_DMA;
	if (lba_mode == 1 && dma == 1 && direction == 1) cmd = ATA_CMD_WRITE_DMA;
	if (lba_mode == 2 && dma == 1 && direction == 1) cmd = ATA_CMD_WRITE_DMA_EXT;
	ide_write(channel, ATA_REG_COMMAND, cmd);               // Send the Command.
	if (dma)
		//if (direction == 0);
		// DMA Read.
		//else;
		// DMA Write.  
		return -1; //No Support!
	else
		if (direction == 0)
		{
			// PIO Read.
			for (i = 0; i < numsects; i++) {
				if (err = ide_polling(channel, 1))
					return err; // Polling, set error and exit if there is.
				asm("rep insw" : : "c"(words), "d"(bus), "D"(edi)); // Receive Data.
			} 
		}
		else 
		{
			// PIO Write.
			for (i = 0; i < numsects; i++) {
				ide_polling(channel, 0); // Polling.
				asm("rep outsw"::"c"(words), "d"(bus), "S"(edi)); // Send Data
			}
			ide_write(channel, ATA_REG_COMMAND, (char []) {   ATA_CMD_CACHE_FLUSH,
					ATA_CMD_CACHE_FLUSH,
					ATA_CMD_CACHE_FLUSH_EXT}[lba_mode]);
			ide_polling(channel, 0); // Polling.
		}

	return 0; // Easy, isn't it?
}
unsigned char ide_print_error(unsigned int drive, unsigned char err) {
	if (err == 0)
		return err;

	printk("IDE:");
	if (err == 1) {printk("- Device Fault\n     "); err = 19;}
	else if (err == 2) {
		unsigned char st = ide_read(ide_devices[drive].Channel, ATA_REG_ERROR);
		if (st & ATA_ER_AMNF)   {printk("- No Address Mark Found\n     ");   err = 7;}
		if (st & ATA_ER_TK0NF)   {printk("- No Media or Media Error\n     ");   err = 3;}
		if (st & ATA_ER_ABRT)   {printk("- Command Aborted\n     ");      err = 20;}
		if (st & ATA_ER_MCR)   {printk("- No Media or Media Error\n     ");   err = 3;}
		if (st & ATA_ER_IDNF)   {printk("- ID mark not Found\n     ");      err = 21;}
		if (st & ATA_ER_MC)   {printk("- No Media or Media Error\n     ");   err = 3;}
		if (st & ATA_ER_UNC)   {printk("- Uncorrectable Data Error\n     ");   err = 22;}
		if (st & ATA_ER_BBK)   {printk("- Bad Sectors\n     ");       err = 13;}
	} else  if (err == 3)           {printk("- Reads Nothing\n     "); err = 23;}
	else  if (err == 4)  {printk("- Write Protected\n     "); err = 8;}
	printk("- [%s %s] %s\n",
			(const char *[]){"Primary", "Secondary"}[ide_devices[drive].Channel], // Use the channel as an index into the array
			(const char *[]){"Master", "Slave"}[ide_devices[drive].Drive], // Same as above, using the drive
			ide_devices[drive].Model);

	return err;
}

unsigned char ide_polling(unsigned char channel, unsigned int advanced_check) 
{
	int i;
	// (I) Delay 400 nanosecond for BSY to be set:
	// -------------------------------------------------
	for(i = 0; i < 4; i++)
		ide_read(channel, ATA_REG_ALTSTATUS); // Reading the Alternate Status port wastes 100ns; loop four times.

	// (II) Wait for BSY to be cleared:
	// -------------------------------------------------
	while (ide_read(channel, ATA_REG_STATUS) & ATA_SR_BSY)
		; // Wait for BSY to be zero.

	if (advanced_check) {
		unsigned char state = ide_read(channel, ATA_REG_STATUS); // Read Status Register.

		// (III) Check For Errors:
		// -------------------------------------------------
		if (state & ATA_SR_ERR)
			return 2; // Error.

		// (IV) Check If Device fault:
		// -------------------------------------------------
		if (state & ATA_SR_DF)
			return 1; // Device Fault.

		// (V) Check DRQ:
		// -------------------------------------------------
		// BSY = 0; DF = 0; ERR = 0 so we should check for DRQ now.
		if ((state & ATA_SR_DRQ) == 0)
			return 3; // DRQ should be set

	}

	return 0; // No Error.

}
unsigned char ide_read(unsigned char channel, unsigned char reg) {
	unsigned char result;
	if (reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
	if (reg < 0x08)
		result = inb(channels[channel].base + reg - 0x00);
	else if (reg < 0x0C)
		result = inb(channels[channel].base  + reg - 0x06);
	else if (reg < 0x0E)
		result = inb(channels[channel].ctrl  + reg - 0x0A);
	else if (reg < 0x16)
		result = inb(channels[channel].bmide + reg - 0x0E);
	if (reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
	return result;
}
void ide_write(unsigned char channel, unsigned char reg, unsigned char data) {
	if (reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
	if (reg < 0x08)
		outb(channels[channel].base  + reg - 0x00, data);
	else if (reg < 0x0C)
		outb(channels[channel].base  + reg - 0x06, data);
	else if (reg < 0x0E)
		outb(channels[channel].ctrl  + reg - 0x0A, data);
	else if (reg < 0x16)
		outb(channels[channel].bmide + reg - 0x0E, data);
	if (reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

void ide_read_buffer(unsigned char channel, unsigned char reg, unsigned int buffer,
		unsigned int quads) {
	/* WARNING: This code contains a serious bug. The inline assembly trashes ES and
	 *           ESP for all of the code the compiler generates between the inline
	 *           assembly blocks.
	 */
	if (reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, 0x80 | channels[channel].nIEN);
	//asm("pushw %es; movw %ds, %ax; movw %ax, %es");
	if (reg < 0x08)
		insl(channels[channel].base  + reg - 0x00, buffer, quads);
	else if (reg < 0x0C)
		insl(channels[channel].base  + reg - 0x06, buffer, quads);
	else if (reg < 0x0E)
		insl(channels[channel].ctrl  + reg - 0x0A, buffer, quads);
	else if (reg < 0x16)
		insl(channels[channel].bmide + reg - 0x0E, buffer, quads);
	//asm("popw %es;");
	if (reg > 0x07 && reg < 0x0C)
		ide_write(channel, ATA_REG_CONTROL, channels[channel].nIEN);
}

