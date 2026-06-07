#ifndef BLOCK_ATA_COMMANDS_H
#define BLOCK_ATA_COMMANDS_H

// all commands marked as _EXT are LBA48 and thus are from ATA-6 onwards

#define ATA_READ_SECTORS      0x20
#define ATA_READ_SECTORS_EXT  0x24
#define ATA_READ_DMA_EXT      0x25

#define ATA_WRITE_SECTORS     0x30
#define ATA_WRITE_SECTORS_EXT 0x34
#define ATA_WRITE_DMA_EXT     0x35

#define ATA_READ_DMA          0xC8
#define ATA_WRITE_DMA         0xCA

#define ATA_FLUSH_CACHE       0xE7 // ATA-4 onwards!

#define ATA_IDENTIFY_DEVICE   0xEC

#endif