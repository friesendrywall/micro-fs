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

#define MICRO_FS_VERSION "1.0"
#define MICRO_FS_OK 0
#define MICRO_FS_FILE_NOT_FOUND -1
#define MICRO_FS_MEM_FULL -2
#define MICRO_FS_FILENAME_ERR -3

void micro_fs_format(void);
int16_t micro_fs_info(char *buff, int16_t maxLen);
int16_t micro_fs_search(char *fileName);
int16_t micro_fs_stat(char *fileName);
int16_t micro_fdel(char *fileName);
int16_t micro_fread(char *fileName, void *buff, uint16_t maxLen);
int16_t micro_fwrite(char *fileName, void *buff, uint16_t len);


#endif