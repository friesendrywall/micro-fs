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

typedef struct {
  uint8_t next : 6;
  uint8_t file : 1;
  uint8_t used : 1;
} micro_fat_t;

typedef struct {
  uint8_t data[MICRO_FS_SECTOR_BYTES];
} micro_fat_sector_t;

typedef struct {
  uint8_t name[MICRO_FS_FILENAME_LEN];
  uint16_t length;
} micro_fat_file_t;

/* Statically allocated ram drive */
struct {
  micro_fat_t fat_table[MICRO_FS_SECTORS] MICRO_FS_RAM_ATTR;
  micro_fat_sector_t sector[MICRO_FS_SECTORS] MICRO_FS_RAM_ATTR;
} micro_fs;

int16_t micro_fs_info(char *buff, int16_t maxLen) {
  char *pin = buff;
  uint32_t i, len;
  uint32_t bytesFree = 0;
  uint32_t bytesUsed = 0;
  uint32_t bytesAvailable = 0;
  uint32_t fileCount = 0;
  micro_fat_file_t *f;
  char nameBuf[16];
  len = snprintf(buff, maxLen,
                 "\r\nmicroFAT Version %s"
                 "\r\nVolume info: Capacity %i B\r\n",
                 MICRO_FS_VERSION, (int)sizeof(micro_fs.sector));
  maxLen -= len;
  buff += len;
  for (i = 0; i < MICRO_FS_SECTORS; i++) {
    if (micro_fs.fat_table[i].file) {
      f = (micro_fat_file_t *)micro_fs.sector[i].data;
      snprintf(nameBuf, MICRO_FS_FILENAME_LEN + 1, f->name);
      len = snprintf(buff, maxLen > 0 ? maxLen : 0, "    %9i %s\r\n",
                     (int)f->length % 1000, nameBuf);
      maxLen -= len;
      buff += len;
      bytesUsed += f->length;
      fileCount++;
    } else if (!micro_fs.fat_table[i].used) {
      bytesAvailable += MICRO_FS_SECTOR_BYTES;
      bytesFree += MICRO_FS_SECTOR_BYTES;
    }
  }

  buff += snprintf(buff, maxLen > 0 ? maxLen : 0,
                   "     Files    %9i\r\n"
                   "     Used     %9i\r\n"
                   "     Free     %9i\r\n",
                   fileCount, bytesUsed, bytesFree);
  return (int)(buff - pin);
}

void micro_fs_format(void) {
  memset(&micro_fs, 0, sizeof(micro_fs));
  uint16_t i;
  for (i = 0; i < MICRO_FS_SECTORS; i++) {
    micro_fs.fat_table[i].next = MICRO_FS_SECTORS;
  }
}

int16_t micro_fs_search(char *fileName) {
  uint16_t i;
  for (i = 0; i < MICRO_FS_SECTORS; i++) {
    if (micro_fs.fat_table[i].file) {
      if (strncmp(micro_fs.sector[i].data, fileName, MICRO_FS_FILENAME_LEN) == 0) {
        return i;
      }
    }
  }
  return MICRO_FS_FILE_NOT_FOUND;
}

int16_t micro_fs_stat(char *fileName) {
  int16_t res = micro_fs_search(fileName);
  if (res == MICRO_FS_FILE_NOT_FOUND) {
    return res;
  }
  return ((micro_fat_file_t *)micro_fs.sector[res].data)->length;
}

int16_t micro_fdel(char *fileName) {
  int16_t index = micro_fs_search(fileName);
  uint16_t next;
  if (index == MICRO_FS_FILE_NOT_FOUND) {
    return index;
  }
  while (micro_fs.fat_table[index].used) {
    next = micro_fs.fat_table[index].next;
    micro_fs.fat_table[index].file = 0;
    micro_fs.fat_table[index].used = 0;
    micro_fs.fat_table[index].next = MICRO_FS_SECTORS;
    index = next;
  }
  return MICRO_FS_OK;
}

int16_t micro_fread(char *fileName, void *buff, uint16_t maxLen) {
  int16_t index = micro_fs_search(fileName);
  uint16_t next;
  int16_t remLen = 0;
  uint16_t readLen = 0;
  int16_t max;
  if (index == MICRO_FS_FILE_NOT_FOUND) {
    return index;
  }
  remLen = ((micro_fat_file_t *)micro_fs.sector[index].data)->length;
  while (micro_fs.fat_table[index].used && remLen > 0 && maxLen > 0) {
    if (micro_fs.fat_table[index].file) {
      max = MICRO_FS_SECTOR_BYTES - sizeof(micro_fat_file_t);
      max = maxLen > max ? max : maxLen;
      max = remLen > max ? max : remLen;
      memcpy(buff, &micro_fs.sector[index].data[sizeof(micro_fat_file_t)], max);
    } else {
      max = maxLen > MICRO_FS_SECTOR_BYTES ? MICRO_FS_SECTOR_BYTES : maxLen;
      max = remLen > max ? max : remLen;
      memcpy(&((uint8_t *)buff)[readLen], micro_fs.sector[index].data, max);
    }
    maxLen -= max;
    readLen += max;
    remLen -= max;
    next = micro_fs.fat_table[index].next;
    index = next;
  }
  return readLen;
}

int16_t micro_fwrite(char *fileName, void *buff, uint16_t len) {
  uint16_t i;
  uint16_t first = 1;
  int16_t wLen = 0;
  int16_t max;
  int16_t last = MICRO_FS_SECTORS;
  if (strlen(fileName) >= 11) {
    return MICRO_FS_FILENAME_ERR;
  }
  (void)micro_fdel(fileName);
  while (len) {
    for (i = 0; i < MICRO_FS_SECTORS; i++) {
      if (!micro_fs.fat_table[i].used) {
        break;
      }
    }
    if (i == MICRO_FS_SECTORS) {
      (void)micro_fdel(fileName);
      return MICRO_FS_MEM_FULL;
    }
    micro_fs.fat_table[i].file = first;
    micro_fs.fat_table[i].used = 1;
    if (last != MICRO_FS_SECTORS) {
      micro_fs.fat_table[last].next = i;
    }
    if (first) {
      strncpy(((micro_fat_file_t *)micro_fs.sector[i].data)->name, fileName,
              MICRO_FS_FILENAME_LEN);
      ((micro_fat_file_t *)micro_fs.sector[i].data)->length = len;
      max = MICRO_FS_SECTOR_BYTES - sizeof(micro_fat_file_t);
      max = len > max ? max : len;
      first = 0;
      memcpy(&micro_fs.sector[i].data[sizeof(micro_fat_file_t)],
             &((uint8_t *)buff)[wLen], max);
    } else {
      max = MICRO_FS_SECTOR_BYTES;
      max = len > max ? max : len;
      memcpy(micro_fs.sector[i].data, &((uint8_t *)buff)[wLen], max);
    }
    wLen += max;
    len -= max;
    last = i;
  }

  return wLen;
}
