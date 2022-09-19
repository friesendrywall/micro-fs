/**
 * MIT License
 *
 * Copyright (c) 2022 Erik Friesen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MICRO_FS_H
#define MICRO_FS_H

#include <stdint.h>

#define UFAT_VERSION "1.0"

#define UFAT_MAX_SECTORS (0xFFF)
#define UFAT_MAX_NAMELEN (14)
#define UFAT_TABLE_COUNT 2

enum {
  UFAT_OK = 0,
  UFAT_ERR_IO = -32,
  UFAT_ERR_FILE_NOT_FOUND,
  UFAT_ERR_CRC,
  UFAT_ERR_CORRUPT,
  UFAT_ERR_EMPTY,
  UFAT_ERR_FULL,
  UFAT_ERR_UNSUPPORTED,
  UFAT_ERR_FILECRC,
  UFAT_ERR_NULL
};

typedef struct {
  uint16_t next : 12;
  /* Start of file flag */
  uint16_t sof : 1;
  uint16_t available : 1;
  /* commited */
  uint16_t written : 1;
} ufat_sector_t;

typedef struct {
  uint32_t tableCrc; // 2 sectors space or what?
  // uint32_t formatID;
  ufat_sector_t sector[]; // Bug here, >= 127 is into next
} ufat_table_t; /* must equal sector size */

typedef struct {
  /* Physical address of media */
  const uint32_t addressStart;
  /* Number of sectors used in media */
  const uint32_t sectors;
  /* Sector size */
  const uint32_t sectorSize;
  /* Number of sectors per table */
  const uint32_t tableSectors;
  /* buff is used for all IO, so if driver uses DMA, allocate accordingly */
  uint8_t *buff; // User allocated to sectorSize
  /* fat is used to store the working copy of the table */
  ufat_table_t *fat; // User allocated to sectorSize
  uint32_t (*read_block_device)(uint32_t address, uint8_t *data, uint32_t len);
  uint32_t (*write_block_device)(uint32_t address, uint8_t *data,
                                 uint32_t length);
  // Non userspace stuff
  uint32_t volumeMounted;
  int lastError;

} ufat_fs_t;

typedef struct {
  uint8_t name[UFAT_MAX_NAMELEN];
  uint16_t len;
  uint32_t timeStamp;
  uint32_t crc;
} ufat_file_t;

typedef struct {
  uint32_t startSector;
  uint32_t position;
  ufat_file_t fh;
  int32_t oldFileSector;
  int32_t currentSector;
  uint32_t rwPosInSector;
  uint32_t openFlags;
  int lastError;
  uint32_t zeroCopy : 1;
  uint32_t error : 1;
  uint32_t crcValidate;
} ufat_FILE;

int ufat_mount(ufat_fs_t *fs);
int ufat_format(ufat_fs_t *fs);
int ufat_fopen(ufat_fs_t *fs, const char *filename, const char *mode,
                 ufat_FILE *file);
int ufat_fclose(ufat_fs_t *fs, ufat_FILE *stream);
size_t ufat_fwrite(ufat_fs_t *fs, const void *ptr, size_t size, size_t count,
                     ufat_FILE *stream);
size_t ufat_fread(ufat_fs_t *fs, void *ptr, size_t size, size_t count,
                    ufat_FILE *stream);
int ufat_remove(ufat_fs_t *fs, const char *filename);
size_t ufat_flength(ufat_FILE *file);
int ufat_fsinfo(ufat_fs_t *fs, char *buff, int32_t maxLen);
int ufat_exists(ufat_fs_t *fs, const char *filename);
int ufat_ferror(ufat_FILE *file);
int ufat_errno(ufat_fs_t *fs);
char *ufat_errstr(int err);

#endif