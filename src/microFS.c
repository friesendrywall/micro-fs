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

#include "microFS.h"
#include "microFSconfig.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UFAT_FILE_NOT_FOUND (-1)
#define UFAT_INVALID_SECTOR (0xFFFF)
#define UFAT_EOF (0x0FFF)

#define UFAT_TABLE_GOOD 0
#define UFAT_TABLE_OLD 1
#define UFAT_TABLE_CRC 2

#define UFAT_FLAG_READ 1
#define UFAT_FLAG_WRITE 2
#define UFAT_FLAG_ZERO_COPY 4 // Not implemented for writes
#define UFAT_FILE_CRC_CHECK 8

#define UFAT_TABLE_SIZE(sectors) (sizeof(ufat_sector_t) * sectors)
/* #define UFAT_BUFF_SIZE(sectors)                                                \
  (sizeof(ufat_sector_t) * sectors + sizeof(ufat_table_t))*/
#define UFAT_FIRST_SECTOR(tableSectors) (tableSectors * UFAT_TABLE_COUNT)

#ifndef UFAT_CRC
/* crc routines written by unknown public source */
static uint32_t crc32_table[256];
void init_crc32(void);
static uint32_t crc32(void *buf, int len, uint32_t Seed);

#define CRC32_POLY 0x04c11db7 /* AUTODIN II, Ethernet, & FDDI 0x04C11DB7 */
#define UFAT_CRC crc32

uint32_t crc32(void *buf, int len, uint32_t Seed) {
  UFAT_TRACE(("CRC:0x%p|%i|0x%X\r\n", buf, len, Seed));
  unsigned char *p;
  uint32_t crc = Seed;
  if (!crc32_table[1]) { /* if not already done, */
    init_crc32();        /* build table */
  }
  for (p = buf; len > 0; ++p, --len) {
    crc = (crc << 8) ^ crc32_table[(crc >> 24) ^ *p];
  }
  return crc; /* transmit complement, per CRC-32 spec */
}

void init_crc32(void) {
  int i, j;
  uint32_t c;
  for (i = 0; i < 256; ++i) {
    for (c = i << 24, j = 8; j > 0; --j) {
      c = c & 0x80000000 ? (c << 1) ^ CRC32_POLY : (c << 1);
    }
    crc32_table[i] = c;
  }
}

#endif

static int32_t scanTable(ufat_fs_t *fs, ufat_table_t *fat) {
  uint32_t i;
  uint32_t wasRepaired = 0;
  UFAT_TRACE(("scanTable()\r\n"));
  for (i = (UFAT_TABLE_COUNT * fs->tableSectors); i < fs->sectors; i++) {
    if (!fat->sector[i].written && !fat->sector[i].available) {
      UFAT_DEBUG(("Sector %i recovered\r\n", i));
      UFAT_TRACE(("SECTOR:recover %i\r\n", i));
      fat->sector[i].available = 1;
      wasRepaired = 1;
    }
  }
  return wasRepaired;
}

static int32_t copyTable(ufat_fs_t *fs, uint32_t toIndex, uint32_t fromIndex) {
  UFAT_TRACE(("copyTable(%i -> %i)\r\n", fromIndex, toIndex));
  if (fs->read_block_device(
          fs->addressStart + ((fs->sectorSize * fs->tableSectors) * fromIndex),
          (uint8_t *)fs->buff, (fs->sectorSize * fs->tableSectors))) {
    fs->lastError = UFAT_ERR_IO;
    UFAT_TRACE(("UFAT_ERR_IO\r\n"));
    return UFAT_ERR_IO;
  }
  if (fs->write_block_device(
          fs->addressStart + ((fs->sectorSize * fs->tableSectors) * toIndex),
          (uint8_t *)fs->buff, (fs->sectorSize * fs->tableSectors))) {
    fs->lastError = UFAT_ERR_IO;
    UFAT_TRACE(("UFAT_ERR_IO\r\n"));
    return UFAT_ERR_IO;
  }
  return UFAT_OK;
}

static uint32_t loadTable(ufat_fs_t *fs, uint32_t tableIndex) {
  uint32_t crcRes;
  UFAT_TRACE(("loadTable(%i)\r\n", tableIndex));
  if (fs->read_block_device(
          fs->addressStart + ((fs->sectorSize * fs->tableSectors) * tableIndex),
          (uint8_t *)fs->fat, (fs->sectorSize * fs->tableSectors))) {
    fs->lastError = UFAT_ERR_IO;
    UFAT_TRACE(("UFAT_ERR_IO\r\n"));
    return UFAT_ERR_IO;
  }
  crcRes =
      UFAT_CRC(fs->fat->sector,
               UFAT_TABLE_SIZE(fs->sectors) - sizeof(ufat_table_t), 0xFFFFFFFF);
  if (crcRes != fs->fat->tableCrc) {
    UFAT_TRACE(("loadTable:failure 0x%X != 0x%X\r\n", 
        crcRes, fs->fat->tableCrc));
    UFAT_ERROR(("Table %i crc failure\r\n", tableIndex));
    return UFAT_ERR_CRC;
  }
  UFAT_TRACE(("loadTable:CRC 0x%X\r\n", crcRes));
  UFAT_DEBUG(("Table %i crc match 0x%X\r\n", tableIndex, crcRes));
  return UFAT_OK;
}

static int32_t validateTable(ufat_fs_t *fs, uint32_t tableIndex,
                             uint32_t *crc) {
  int32_t res = UFAT_TABLE_GOOD;
  uint32_t crcRes;
  ufat_table_t *fat = (ufat_table_t *)fs->buff;
  UFAT_TRACE(("validateTable(%i)\r\n", tableIndex));
  if (fs->read_block_device(
          fs->addressStart + ((fs->sectorSize * fs->tableSectors) * tableIndex),
          (uint8_t *)fat, (fs->sectorSize * fs->tableSectors))) {
    fs->lastError = UFAT_ERR_IO;
    UFAT_TRACE(("UFAT_ERR_IO\r\n"));
    return UFAT_ERR_IO;
  }

  crcRes =
      UFAT_CRC(fat->sector, UFAT_TABLE_SIZE(fs->sectors) - sizeof(ufat_table_t),
               0xFFFFFFFF);
  if (crcRes != fat->tableCrc) {
    UFAT_TRACE(("validateTable:failure 0x%X != 0x%X (%i)\r\n", crcRes,
                  fat->tableCrc, UFAT_TABLE_SIZE(fs->sectors)));
    return UFAT_TABLE_CRC;
  }
  if (crc) {
    *crc = crcRes;
  }
  UFAT_TRACE(("validateTable:CRC 0x%X\r\n", crcRes));
  UFAT_DEBUG(("Table %i crc match 0x%X\r\n", tableIndex, crcRes));
  return res;
}

static int32_t findEmptySector(ufat_fs_t *fs) {
  uint32_t i;
  // int32_t res;
  uint32_t sp = UFAT_RAND() % fs->sectors;
  UFAT_TRACE(("findEmptySector().."));
  if (sp < (UFAT_TABLE_COUNT * fs->tableSectors)) {
    sp = fs->sectors / 2;
  }
  for (i = sp; i < fs->sectors; i++) {
    if (fs->fat->sector[i].available) {
      fs->fat->sector[i].available = 0;
      UFAT_TRACE(("[%i]\r\n", i));
      return i;
    }
  }
  for (i = (UFAT_TABLE_COUNT * fs->tableSectors); i < sp; i++) {
    if (fs->fat->sector[i].available) {
      fs->fat->sector[i].available = 0;
      UFAT_TRACE(("[%i]\r\n", i));
      return i;
    }
  }
  return UFAT_ERR_FULL;
}

static int fileSearch(ufat_fs_t *fs, const char *fileName, uint32_t *sector,
                      ufat_file_t *fh, uint32_t *len) {
  uint32_t i;
  int foundFile = UFAT_ERR_FILE_NOT_FOUND;
  *sector = UFAT_INVALID_SECTOR;
  UFAT_TRACE(("fileSearch(%s)..", fileName));
  for (i = UFAT_FIRST_SECTOR(fs->tableSectors); i < fs->sectors; i++) {
    if (fs->fat->sector[i].sof) {
      if (fs->read_block_device(fs->addressStart + (i * fs->sectorSize),
                                fs->buff, sizeof(ufat_file_t))) {
        fs->lastError = UFAT_ERR_IO;
        UFAT_TRACE(("UFAT_ERR_IO\r\n"));
        return UFAT_ERR_IO;
      }
#ifdef UFAT_TRACE
      char name[UFAT_MAX_NAMELEN + 1];
      snprintf(name, UFAT_MAX_NAMELEN, "%s", fs->buff);
      UFAT_TRACE(("[%s]", name));
#endif
      if (strncmp(fs->buff, fileName, UFAT_MAX_NAMELEN) == 0) {
        *sector = i;
        if (fh) {
          memcpy(fh, fs->buff, sizeof(ufat_file_t));
        }
        if (len) {
          ufat_file_t *f = (ufat_file_t *)fs->buff;
          *len = f->len;
        }
        foundFile = UFAT_OK;
        break;
      }
    }
  }
  return foundFile;
}

static int commitChanges(ufat_fs_t *fs) {
  UFAT_TRACE(("commitChanges..\r\n"));
  if (fs->lastError == UFAT_ERR_IO) {
    return UFAT_ERR_IO;
  }

  fs->fat->tableCrc =
      UFAT_CRC(fs->fat->sector,
               UFAT_TABLE_SIZE(fs->sectors) - sizeof(ufat_table_t), 0xFFFFFFFF);
  // validateTable(fs, 0, &i);
  UFAT_TRACE(("TESTCRC: 0x%X\r\n", fs->fat->tableCrc));
  /* Copy 1 */
  UFAT_TRACE(("commitChanges:Program[0]\r\n"));
  if (fs->write_block_device(fs->addressStart, (uint8_t *)fs->fat,
                             UFAT_TABLE_SIZE(fs->sectors))) {
    return UFAT_ERR_IO;
  }
  /* Copy 2 */
  UFAT_TRACE(("commitChanges:Program[1]\r\n"));
  if (fs->write_block_device(
          fs->addressStart + (fs->tableSectors * fs->sectorSize),
          (uint8_t *)fs->fat, UFAT_TABLE_SIZE(fs->sectors))) {
    return UFAT_ERR_IO;
  }
  return UFAT_OK;
}

ufat_mount(ufat_fs_t *fs) {
  uint32_t t1State, t2State;
  uint32_t crc1, crc2;
  uint32_t scenario;
  uint32_t res;
  uint32_t tablesValid = 0;
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->buff);
  UFAT_ASSERT(fs->fat);
  UFAT_ASSERT(fs->sectors < UFAT_MAX_SECTORS);
  UFAT_ASSERT(fs->read_block_device);
  UFAT_ASSERT(fs->write_block_device);
  UFAT_TRACE(("Table Bytes = 0x%X\r\n", UFAT_TABLE_SIZE(fs->sectors)));
  fs->lastError = UFAT_OK;
  t1State = validateTable(fs, 0, &crc1);
  t2State = validateTable(fs, 1, &crc2);
  if (t1State == UFAT_TABLE_GOOD && t2State == UFAT_TABLE_GOOD &&
      crc1 != crc2) {
    t2State = UFAT_TABLE_OLD;
  }
  if (t1State == UFAT_ERR_IO || t2State == UFAT_ERR_IO) {
    return UFAT_ERR_IO;
  }
  if (t1State != UFAT_TABLE_GOOD && t2State != UFAT_TABLE_GOOD) {
    UFAT_TRACE(("ufat_mount:Volume empty\r\n"));
    UFAT_DEBUG(("Mounted volume is empty\r\n"));
    return UFAT_ERR_EMPTY;
  } 
  scenario = t1State << 4 + t2State;
  switch (scenario) {
  case 0x00: /* |GOOD |GOOD | */
    res = loadTable(fs, 0);
    if (res) {
      return res;
    }
    tablesValid = 1;
    break;
  case 0x20: /* | BAD |GOOD | */
    /* Repair */
    res = copyTable(fs, 0, 1);
    if (res) {
      return res;
    }
    /* Load */
    res = loadTable(fs, 1);
    if (res) {
      return res;
    }
    tablesValid = 1;
    break;
  case 0x01: /* |GOOD | OLD | */
    /* fallthrough */
  case 0x02: /* |GOOD | BAD | */
    res = copyTable(fs, 1, 0);
    if (res) {
      return res;
    }
    res = loadTable(fs, 1);
    if (res) {
      return res;
    }
    tablesValid = 1;
    break;
  default:
    /* No suitable action */
    UFAT_DEBUG(("No suitable action for %02x\r\n", scenario));
    UFAT_TRACE(("ufat_mount:!x%02X\r\n", scenario));
    UFAT_ASSERT(0);
    break;
  }
  if (!tablesValid) {
    UFAT_TRACE(("ufat_mount:No valid tables\r\n"));
    fs->lastError = UFAT_ERR_CORRUPT;
    return UFAT_ERR_CORRUPT;
  }
  UFAT_TRACE(("ufat_mount:0x%02X\r\n", scenario));
  /* scan for unclosed files */
  if (scanTable(fs, fs->fat)) {
    commitChanges(fs);
    UFAT_DEBUG(("Tables repaired\r\n"));
    UFAT_TRACE(("ufat_mount:tables repaired\r\n"));
  }
  fs->volumeMounted = 1;
  UFAT_TRACE(("ufat_mount:mounted\r\n"));
  UFAT_INFO(("Volume is mounted\r\n"));
  return 0;
}

int ufat_format(ufat_fs_t *fs) {
  uint32_t i;
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->buff);
  UFAT_ASSERT(fs->fat);
  UFAT_ASSERT(fs->sectors < UFAT_MAX_SECTORS);
  UFAT_ASSERT(fs->read_block_device);
  UFAT_ASSERT(fs->write_block_device);
  UFAT_TRACE(("ufat_format()\r\n"));
  /* check sizes */
  UFAT_ASSERT(fs->tableSectors * fs->sectorSize <=
              UFAT_TABLE_SIZE(fs->sectors));
  for (i = 0; i < fs->sectors; i++) {
    if (i < UFAT_FIRST_SECTOR(fs->tableSectors)) {
      fs->fat->sector[i].next = 0;
      fs->fat->sector[i].available = 0;
    } else {
      fs->fat->sector[i].next = UFAT_MAX_SECTORS;
      fs->fat->sector[i].available = 1;
    }
    fs->fat->sector[i].sof = 0;
    fs->fat->sector[i].written = 0;
  }
  fs->fat->tableCrc =
      UFAT_CRC(fs->fat->sector,
               UFAT_TABLE_SIZE(fs->sectors) - sizeof(ufat_table_t), 0xFFFFFFFF);
  /* Copy 1 */
  if (fs->write_block_device(fs->addressStart, (uint8_t *)fs->fat,
                             UFAT_TABLE_SIZE(fs->sectors))) {
    UFAT_TRACE(("UFAT_ERR_IO\r\n"));
    return UFAT_ERR_IO;
  }
  /* Copy 2 */
  if (fs->write_block_device(fs->addressStart +
                                 (fs->tableSectors * fs->sectorSize),
          (uint8_t *)fs->fat, UFAT_TABLE_SIZE(fs->sectors))) {
    UFAT_TRACE(("UFAT_ERR_IO\r\n"));
    return UFAT_ERR_IO;
  }
  UFAT_INFO(("Volume is formatted\r\n"));
  UFAT_TRACE(("FORMAT:done\r\n"));
  return UFAT_OK;
}

int ufat_fsinfo(ufat_fs_t *fs, char *buff, int32_t maxLen) {
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->volumeMounted);
  char *pin = buff;
  uint32_t i, len;
  uint32_t bytesFree = 0;
  uint32_t bytesUsed = 0;
  uint32_t bytesUncollected = 0;
  uint32_t bytesAvailable = 0;
  uint32_t fileCount = 0;
  uint32_t tableOverhead =
      (fs->tableSectors * UFAT_TABLE_COUNT * fs->sectorSize);
  ufat_file_t f;
  struct tm ts;
  time_t now;
  char buf[32];
  char name[UFAT_MAX_NAMELEN + 1];
  len = UFAT_INFO_SNPRINT(
      (buff, maxLen,
       "\r\nnorFAT Version %s"
       "\r\nVolume info:Capacity %9i B\r\n",
       UFAT_VERSION,
       (fs->sectors * fs->sectorSize) - tableOverhead));
  maxLen -= len;
  buff += len;
  for (i = (UFAT_TABLE_COUNT * fs->tableSectors); i < fs->sectors; i++) {
    if (fs->fat->sector[i].sof) {
      if (fs->read_block_device(fs->addressStart + (i * fs->sectorSize),
                                fs->buff, sizeof(ufat_file_t))) {
        return UFAT_ERR_IO;
      }

      memcpy(&f, fs->buff, sizeof(ufat_file_t));
      now = (time_t)f.timeStamp;
      ts = *localtime(&now);
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ts);
      snprintf(name, UFAT_MAX_NAMELEN + 1, "%s", f.name);
      len = UFAT_INFO_SNPRINT((buff, maxLen > 0 ? maxLen : 0, "%s  %9i %s\r\n",
                               buf, (int)f.len, name));
      maxLen -= len;
      buff += len;
      bytesUsed += f.len;
      fileCount++;
    } else if (fs->fat->sector[i].available) {
      bytesAvailable += fs->sectorSize;
      bytesFree += fs->sectorSize;
    } 
  }

  buff += UFAT_INFO_SNPRINT((buff, maxLen > 0 ? maxLen : 0,
                               "     Files    %9i\r\n"
                               "     Used     %9i\r\n"
                               "     Free     %9i\r\n",
                               fileCount, bytesUsed, bytesFree));
  return (int)(buff - pin);
}

int ufat_fopen(ufat_fs_t *fs, const char *filename, const char *mode,
                 ufat_FILE *file) {

  uint32_t sector;
  uint32_t flags;
  int retVal;
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->volumeMounted);
  /* Protect fs state */
  if (fs->lastError == UFAT_ERR_IO) {
    return UFAT_ERR_IO;
  }
  if (strcmp("r", mode) == 0) {
    flags = UFAT_FLAG_READ;
#ifdef UFAT_FILE_CHECK
    flags |= UFAT_FILE_CRC_CHECK;
#endif
  } else if (strcmp("rb", mode) == 0) {
    flags = UFAT_FLAG_READ;
#ifdef UFAT_FILE_CHECK
    flags |= UFAT_FILE_CRC_CHECK;
#endif
  } else if (strcmp("w", mode) == 0) {
    flags = UFAT_FLAG_WRITE;
  } else if (strcmp("wb", mode) == 0) {
    flags = UFAT_FLAG_WRITE;
  } else {
    fs->lastError = UFAT_ERR_UNSUPPORTED;
    UFAT_TRACE(("ufat_fopen:unsupported\r\n"));
    return UFAT_ERR_UNSUPPORTED;
  }
  memset(file, 0, sizeof(ufat_file_t));
  retVal = fileSearch(fs, filename, &sector, &file->fh, NULL);
  if (flags & UFAT_FLAG_READ) {
    if (retVal == UFAT_OK) {
      file->startSector = sector;
      file->currentSector = sector;
      file->rwPosInSector = sizeof(ufat_file_t);
      file->openFlags = flags;
      if (flags & UFAT_FLAG_ZERO_COPY) {
        file->zeroCopy = 1;
      }
      file->crcValidate = 0xFFFFFFFF;
      UFAT_TRACE(("ufat_fopen:file opened for reading\r\n"));
      UFAT_DEBUG(("FILE %s opened for reading\r\n", filename));
      return UFAT_OK;
    } else {
      if (fs->lastError == UFAT_ERR_IO) {
        return UFAT_ERR_IO;
      }
      UFAT_TRACE(("UFAT_ERR_FILE_NOT_FOUND\r\n"));
      fs->lastError = UFAT_ERR_FILE_NOT_FOUND;
      return UFAT_ERR_FILE_NOT_FOUND; // File not found
    }
  } else if (flags & UFAT_FLAG_WRITE) {
    if (retVal == UFAT_ERR_FILE_NOT_FOUND && sector == UFAT_INVALID_SECTOR &&
        fs->lastError == UFAT_ERR_IO) {
      UFAT_TRACE(("UFAT_ERR_IO\r\n"));
      return UFAT_ERR_IO;
    }
    // memset(file, 0, sizeof(ufat_FILE));
    file->oldFileSector = UFAT_FILE_NOT_FOUND;
    file->startSector = UFAT_INVALID_SECTOR;
    file->openFlags = flags;
    file->currentSector = -1;
    if (retVal == UFAT_OK) { // File found
      file->oldFileSector = sector; // Mark for removal
      UFAT_ASSERT(sector >= UFAT_TABLE_COUNT);
      UFAT_DEBUG(("Sector %i marked for removal\r\n", sector));
      UFAT_TRACE(("ufat_fopen:sector[%i] marked to remove\r\n", sector));
    } else {
      memset(&file->fh, 0, sizeof(ufat_file_t));
      uint32_t nameLen = strlen(filename);
      nameLen = nameLen + 1 < UFAT_MAX_NAMELEN ? nameLen : UFAT_MAX_NAMELEN;
      strncpy(file->fh.name, filename, nameLen);
    }
    UFAT_TRACE(("ufat_fopen:file opened for writing\r\n"));
    UFAT_DEBUG(("FILE %s opened for writing\r\n", filename));
    return UFAT_OK;
  }
  fs->lastError = UFAT_ERR_UNSUPPORTED;
  UFAT_TRACE(("ufat_fopen:unsupported fallthrough\r\n"));
  return UFAT_ERR_UNSUPPORTED;
}

int ufat_fclose(ufat_fs_t *fs, ufat_FILE *stream) {
  // Write header to page
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->volumeMounted);
  UFAT_ASSERT(stream);
  int32_t ret;
  uint32_t limit;
  uint32_t current;
  uint32_t next;
  /* Protect fs state */
  if (fs->lastError == UFAT_ERR_IO) {
    ret = UFAT_ERR_IO;
    goto finalize;
  }

  if (stream->error && stream->openFlags & UFAT_FLAG_WRITE) {
    // invalidate the last
    if (stream->startSector != UFAT_INVALID_SECTOR) {
      limit = fs->sectors;
      current = stream->startSector;
      next = fs->fat->sector[current].next;
      UFAT_DEBUG(("..INVALID[%i]..%i.%i", stream->position, current, next));
      UFAT_TRACE(("ufat_fclose:INVALID[%i]:%i.%i\r\n", stream->position,
                    current, next));
      for (;;) {
        fs->fat->sector[current].available = 1;
        fs->fat->sector[current].written = 0;
        fs->fat->sector[current].sof = 0;
        fs->fat->sector[current].next = UFAT_MAX_SECTORS;
        if (next == UFAT_EOF ||
            (stream->lastError == UFAT_ERR_FULL && next == UFAT_MAX_SECTORS)) {
          break;
        }
        if (next < UFAT_TABLE_COUNT ||
            (next >= fs->sectors && next != UFAT_EOF)) {
          UFAT_ERROR(("Corrupt file system next = %i\r\n", next));
          UFAT_TRACE(("UFAT_ERR_CORRUPT next \r\n", next));
          ret = UFAT_ERR_CORRUPT;
          goto finalize;
        }
        current = next;
        next = fs->fat->sector[next].next;
        UFAT_DEBUG((".%i", next));
        UFAT_TRACE((".%i", next));
        if (--limit < 1) {
          ret = UFAT_ERR_CORRUPT;
          goto finalize;
        }
      }
      UFAT_DEBUG((".\r\n"));
      UFAT_TRACE((".\r\n", next));
    }
    ret = fs->lastError;
    goto finalize;
  }
  if (stream->openFlags & UFAT_FLAG_WRITE &&
      stream->startSector != UFAT_INVALID_SECTOR) {
    // Write the header
    stream->fh.len = stream->position;
    stream->fh.timeStamp = time(NULL);
    memcpy(fs->buff, &stream->fh, sizeof(ufat_file_t));
    if (fs->write_block_device(fs->addressStart +
                                   (stream->startSector * fs->sectorSize),
                               fs->buff, sizeof(ufat_file_t))) {
      ret = UFAT_ERR_IO;
      goto finalize;
    }
    // Commit to _FAT table
    fs->fat->sector[stream->startSector].written = 1;
    limit = fs->sectors;
    current = stream->startSector;
    next = fs->fat->sector[current].next;
    UFAT_DEBUG(("..WRITE[%i]..%i.%i.", stream->position, current, next));
    UFAT_TRACE(
        ("ufat_fclose:WRITE[%i]:%i.%i.", stream->position, current, next));
    for (;;) {
      if (next == UFAT_EOF) {
        break;
      }
      if (next < UFAT_TABLE_COUNT ||
          (next >= fs->sectors && next != UFAT_EOF)) {
        UFAT_ERROR(("Corrupt file system next = %i\r\n", next));
        UFAT_TRACE(("UFAT_ERR_CORRUPT next \r\n", next));
        ret = UFAT_ERR_CORRUPT;
        goto finalize;
      }
      fs->fat->sector[next].written = 1;
      current = next;
      next = fs->fat->sector[next].next;
      UFAT_DEBUG(("%i.", next));
      UFAT_TRACE(("%i.", next));
      if (--limit < 1) {
        ret = UFAT_ERR_CORRUPT;
        goto finalize;
      }
    }
    UFAT_DEBUG(("\r\n"));
    UFAT_TRACE(("\r\n"));
  }

  // Delete old file
  if (stream->openFlags & UFAT_FLAG_WRITE &&
      stream->oldFileSector != UFAT_FILE_NOT_FOUND) {

    limit = fs->sectors;
    current = stream->oldFileSector;
    next = fs->fat->sector[current].next;
    UFAT_TRACE(("ufat_fclose:DELETE:%i.%i.", current, next));
    for (;;) {
      fs->fat->sector[current].available = 1;
      fs->fat->sector[current].sof = 0;
      fs->fat->sector[current].written = 0;
      fs->fat->sector[current].next = UFAT_MAX_SECTORS;
      if (next == UFAT_EOF) {
        break;
      }
      if (next < UFAT_TABLE_COUNT ||
          (next >= fs->sectors && next != UFAT_EOF)) {
        UFAT_ERROR(("Corrupt file system next = %i\r\n", next));
        UFAT_TRACE(("UFAT_ERR_CORRUPT next \r\n", next));
        ret = UFAT_ERR_CORRUPT;
        goto finalize;
      }
      current = next;
      next = fs->fat->sector[next].next;
      UFAT_DEBUG(("%i.", next));
      UFAT_TRACE(("%i.", next));
      if (--limit < 1) {
        UFAT_TRACE(("UFAT_ERR_CORRUPT limit\r\n"));
        ret = UFAT_ERR_CORRUPT;
        goto finalize;
      }
    }
    UFAT_TRACE(("\r\n"));
  }
  if (stream->openFlags & UFAT_FLAG_WRITE) {
    ret = commitChanges(fs);
    if (ret) {
      UFAT_DEBUG(("FILE %s commit failed\r\n", stream->fh.name));
      UFAT_TRACE(
          ("ufat_fclose(%s):commit failed\r\n", stream->fh.name));
    } else {
      UFAT_DEBUG(("FILE %s committed\r\n", stream->fh.name));
      UFAT_TRACE(("ufat_fclose(%s):committed\r\n", stream->fh.name));
    }
  } else {
    ret = UFAT_OK;
  }
finalize:
  UFAT_DEBUG(("FILE %s closed\r\n", stream->fh.name));
  UFAT_TRACE(("ufat_fclose(%s):finalize\r\n", stream->fh.name));
  return ret;
}

size_t ufat_fwrite(ufat_fs_t *fs, const void *ptr, size_t size, size_t count,
                     ufat_FILE *stream) {
  int32_t nextSector;
  uint32_t writeable;
  uint32_t address;
  uint32_t DataLengthToWrite;
  uint8_t *out = (uint8_t *)ptr;
  uint32_t len = size * count;
  UFAT_TRACE(("ufat_fwrite(%i)\r\n", len));
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->volumeMounted);
  UFAT_ASSERT(stream);
  UFAT_ASSERT(stream->openFlags & UFAT_FLAG_WRITE);
  UFAT_ASSERT(size * count > 0);
  /* Protect fs state */
  if (fs->lastError == UFAT_ERR_IO) {
    return 0;
  }
  if (stream->currentSector == -1) {
    stream->currentSector = findEmptySector(fs);
    if (stream->currentSector == UFAT_ERR_FULL) {
      stream->error = 1;
      stream->lastError = UFAT_ERR_FULL;
      UFAT_TRACE(("ufat_fwrite:UFAT_ERR_FULL\r\n"));
      return UFAT_ERR_FULL;
    } else if (stream->currentSector == UFAT_ERR_IO) {
      stream->error = 1;
      fs->lastError = stream->lastError = UFAT_ERR_IO;
      UFAT_TRACE(("ufat_fwrite:UFAT_ERR_IO\r\n"));
      return UFAT_ERR_IO;
    }
    UFAT_DEBUG(("New file sector %i\r\n", stream->currentSector));
    UFAT_TRACE(("ufat_fwrite:add sector[%i]\r\n", stream->currentSector));
    // New file
    fs->fat->sector[stream->currentSector].sof = 1;
    stream->startSector = stream->currentSector;
    stream->rwPosInSector = sizeof(ufat_file_t);
    stream->fh.crc = 0xFFFFFFFF;
    UFAT_ASSERT(stream->startSector >= UFAT_TABLE_COUNT &&
                  stream->startSector != UFAT_INVALID_SECTOR);
  }
  // At this point we should have a writeable area

  while (len) {
    // Calculate available space to write in this sector
    writeable = fs->sectorSize - stream->rwPosInSector;
    if (writeable == 0) {
      nextSector = findEmptySector(fs);
      if (nextSector == UFAT_ERR_FULL) {
        stream->error = 1; // Flag for fclose delete
        stream->lastError = UFAT_ERR_FULL;
        UFAT_TRACE(("ufat_fwrite:UFAT_ERR_FULL\r\n"));
        return UFAT_ERR_FULL;
      } else if (nextSector == UFAT_ERR_IO) {
        stream->error = 1; // Flag for fclose delete
        fs->lastError = stream->lastError = UFAT_ERR_IO;
        UFAT_TRACE(("ufat_fwrite:UFAT_ERR_IO\r\n"));
        return UFAT_ERR_IO;
      }
      UFAT_TRACE(("ufat_fwrite:add sector[%i]->[%i]\r\n",
                    stream->currentSector, nextSector));
      UFAT_DEBUG(("File sector added %i -> %i\r\n", stream->currentSector,
                    nextSector));
      fs->fat->sector[stream->currentSector].next = nextSector;
      fs->fat->sector[nextSector].sof = 0;
      stream->currentSector = nextSector;
      writeable = fs->sectorSize;
      stream->rwPosInSector = 0;
    }
    address =
        (stream->currentSector * fs->sectorSize) + stream->rwPosInSector;

    DataLengthToWrite = len > writeable ? writeable : len;
    if (fs->write_block_device(fs->addressStart + address, out,
                               DataLengthToWrite)) {
      fs->lastError = stream->lastError = UFAT_ERR_IO;
      UFAT_TRACE(("UFAT_ERR_IO\r\n"));
      return UFAT_ERR_IO;
    }
    stream->fh.crc = UFAT_CRC(out, DataLengthToWrite, stream->fh.crc);
    stream->position += DataLengthToWrite;
    stream->rwPosInSector += DataLengthToWrite;
    out += DataLengthToWrite;
    len -= DataLengthToWrite;
  }
  return (size * count);
}

size_t ufat_fread(ufat_fs_t *fs, void *ptr, size_t size, size_t count,
                    ufat_FILE *stream) {
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->volumeMounted);
  UFAT_ASSERT(stream);
  UFAT_ASSERT(stream->openFlags & UFAT_FLAG_READ);
  uint32_t next;
  uint32_t readable;
  uint32_t remaining;
  uint32_t rlen;
  uint32_t rawAdr;
  int32_t readCount = 0;
  uint8_t *in = (uint8_t *)ptr;
  uint32_t len = size * count;
  UFAT_TRACE(("ufat_fread(%i)\r\n", len));
  /* Protect fs state */
  if (fs->lastError == UFAT_ERR_IO) {
    return 0;
  }
  while (len) {
    readable = fs->sectorSize - stream->rwPosInSector;
    remaining = stream->fh.len - stream->position;
    if (remaining == 0) {
      break;
    }
    if (readable == 0) {
      next = fs->fat->sector[stream->currentSector].next;
      UFAT_TRACE(("ufat_fread:next sector[%i]\r\n", next));
      if (next == UFAT_EOF) {
        break;
      }
      stream->rwPosInSector = 0;
      stream->currentSector = next;
      readable = fs->sectorSize;
    }

    rlen = len > readable ? readable : len;
    rlen = rlen > remaining ? remaining : rlen;
    // TODO: only allow reads up to real len
    // uint32_t rawlen = wlen;
    rawAdr = (stream->currentSector * fs->sectorSize) + stream->rwPosInSector;
    if (stream->zeroCopy) {
      // Requires user implemented cache free operation
      if (fs->read_block_device(fs->addressStart + rawAdr, in, rlen)) {
        fs->lastError = stream->lastError = UFAT_ERR_IO;
        UFAT_TRACE(("UFAT_ERR_IO\r\n"));
        return 0;
      }
    } else {
      if (fs->read_block_device(fs->addressStart + rawAdr, fs->buff, rlen)) {
        fs->lastError = stream->lastError = UFAT_ERR_IO;
        UFAT_TRACE(("UFAT_ERR_IO\r\n"));
        return 0;
      }
      memcpy(in, fs->buff, rlen);
    }
    if (stream->openFlags & UFAT_FILE_CRC_CHECK) {
      stream->crcValidate = UFAT_CRC(in, rlen, stream->crcValidate);
    }
    stream->position += rlen;
    stream->rwPosInSector += rlen;
    in += rlen;
    len -= rlen;
    readCount += rlen;
  }
  if (stream->openFlags & UFAT_FILE_CRC_CHECK &&
      stream->position == stream->fh.len) {
    if (stream->crcValidate != stream->fh.crc) {
      return UFAT_ERR_FILECRC;
    }
  }
  UFAT_TRACE(("ufat_fread:read %i\r\n", readCount));
  return readCount;
}

int ufat_remove(ufat_fs_t *fs, const char *filename) {
  uint32_t sector;
  int ret = UFAT_OK;
  uint32_t limit, current, next;
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->volumeMounted);
  UFAT_TRACE(("ufat_remove(%s)\r\n", filename));
  /* Protect fs state */
  if (fs->lastError == UFAT_ERR_IO) {
    return UFAT_ERR_IO;
  }
  ret = fileSearch(fs, filename, &sector, NULL, NULL);
  if (ret == UFAT_ERR_FILE_NOT_FOUND) {
    return UFAT_OK;
  }

  limit = fs->sectors;
  current = sector;
  next = fs->fat->sector[current].next;
  UFAT_TRACE(("ufat_remove:DELETE:%i.%i.", current, next));
  while (1) {
    if (next == UFAT_EOF) {
      break;
    }
    if (next < UFAT_TABLE_COUNT || (next >= fs->sectors && next != UFAT_EOF)) {
      UFAT_ERROR(("Corrupt file system next = %i\r\n", next));
      UFAT_TRACE(("UFAT_ERR_CORRUPT\r\n", next));
      fs->lastError = UFAT_ERR_CORRUPT;
      ret = UFAT_ERR_CORRUPT;
      goto finalize;
    }
    current = next;
    next = fs->fat->sector[next].next;
    if (--limit < 1) {
      UFAT_TRACE(("UFAT_ERR_CORRUPT limit\r\n"));
      fs->lastError = UFAT_ERR_CORRUPT;
      fs->lastError = UFAT_ERR_CORRUPT;
      ret = UFAT_ERR_CORRUPT;
      goto finalize;
    }
  }
  ret = commitChanges(fs);
  UFAT_TRACE(("ufat_remove:committed\r\n"));
  UFAT_DEBUG(("FILE %s delete\r\n", filename));
finalize:
  UFAT_TRACE(("ufat_remove:finalize\r\n"));
  return ret;
}

int ufat_exists(ufat_fs_t *fs, const char *filename) {
  uint32_t sector;
  uint32_t fLen;
  int ret = 0;
  UFAT_ASSERT(fs);
  UFAT_ASSERT(fs->volumeMounted);
  UFAT_TRACE(("ufat_exists(%s)\r\n", filename));
  /* Protect fs state */
  if (fs->lastError == UFAT_ERR_IO) {
    return UFAT_ERR_IO;
  }
  ret = fileSearch(fs, filename, &sector, NULL, &fLen);

  if (ret == UFAT_OK) {
    ret = fLen;
  } else {
    if (fs->lastError == UFAT_ERR_IO) {
      return UFAT_ERR_IO;
    }
  }
  return ret == UFAT_ERR_FILE_NOT_FOUND ? 0 : ret;
}

int ufat_ferror(ufat_FILE *file) { return file->error; }

int ufat_errno(ufat_fs_t *fs) { return fs->lastError; }

size_t ufat_flength(ufat_FILE *f) {
  UFAT_ASSERT(f);
  if (f == NULL) {
    return 0;
  }
  return f->fh.len;
}

char *ufat_errstr(int err) {
  char errstr[12];
  switch (err) {
  case UFAT_OK:
    return "OK";
  case UFAT_ERR_IO:
    return "IO";
  case UFAT_ERR_FILE_NOT_FOUND:
    return "FILE NOT FOUND";
  case UFAT_ERR_CRC:
    return "CRC";
  case UFAT_ERR_CORRUPT:
    return "CORRUPT";
  case UFAT_ERR_EMPTY:
    return "EMPTY";
  case UFAT_ERR_FULL:
    return "FULL";
  case UFAT_ERR_UNSUPPORTED:
    return "UNSUPPORTED";
  case UFAT_ERR_FILECRC:
    return "FILE CRC";
  case UFAT_ERR_NULL:
    return "NULL";
  default:
    snprintf(errstr, sizeof(errstr), "%i", err);
    return errstr;
  }
}