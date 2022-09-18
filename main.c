/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.c
 * Author: Erik
 *
 * Created on September 15, 2022, 3:09 PM
 */
#define _CRT_RAND_S

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "src/microFS.h"
#include "microFSconfig.h"

void writeTraceToFile(void);

#define FAKE_PROM_SIZE 0x2000
#define FAKE_PROM_SECTOR_SIZE 64
#define FAKE_PROM_TABLE_SECTORS                                                \
  ((FAKE_PROM_SIZE / FAKE_PROM_SECTOR_SIZE) / FAKE_PROM_SECTOR_SIZE)

uint8_t block[FAKE_PROM_SIZE];

#define TAKE_DOWN_READ (1UL << 0)
#define TAKE_DOWN_WRITE (1UL << 1)

#define TRACE_BUFFER_SIZE (10 * 1024 * 1024)

// Testing
uint32_t POWER_CYCLE_COUNT = 2000;
uint32_t takeDownPeriod = 0;
uint32_t takeDownTest = 0;
uint32_t takeDownFlags = 0;

FILE *traceFile = NULL;
char *traceBuffer = NULL;
uint32_t traceLocation = 0;

uint32_t read_block_device(uint32_t address, uint8_t *data, uint32_t len) {
  if (address + len >= FAKE_PROM_SIZE) {
    UFAT_ASSERT(0);
  }
  if (takeDownTest && (takeDownFlags & TAKE_DOWN_READ)) {

    if (takeDownPeriod != 0) {
      takeDownPeriod--;
    } else {
      // printf("Power failed at read 0x%X .......\r\n", address);
      memcpy(data, &block[address], len / 2);
      return 1;
    }
  }
  memcpy(data, &block[address], len);
  return 0;
}

uint32_t write_block_page(uint32_t address, uint8_t *data, uint32_t length) {
  uint32_t i;
  if (address <
      FAKE_PROM_TABLE_SECTORS * UFAT_TABLE_COUNT * FAKE_PROM_SECTOR_SIZE) {
    traceHandler("write_block_page(0x%X)(%i)\r\n", address, length);
  } else {
    traceHandler("write_file__page(0x%X)(%i)\r\n", address, length);
  }
  if (address + length > FAKE_PROM_SIZE) {
    writeTraceToFile();
    UFAT_ASSERT(0);
  }
  if (takeDownTest && (takeDownFlags & TAKE_DOWN_WRITE)) {

    if (takeDownPeriod != 0) {
      takeDownPeriod--;
    } else {
      // Randomize
      uint32_t failLen = rand() % length;
      if (failLen == 0 || failLen == length) {
        for (i = 0; i < length; i++) {
          block[address + i] &= (rand() % 0xFF);
        }
      } else {
        for (i = 0; i < failLen; i++) {
          block[address + i] = data[i];
        }
      }
      return 1;
    }
  }
  for (i = 0; i < length; i++) {
    block[address + i] = data[i];
  }
  if (block[512] == 0xFF) {
    return 0;
  }
  return 0;
}

void assertHandler(char *file, int line) {
  printf("UFAT_ASSERT(%s:%i\r\n", file, line);
  int a = 0;
#ifdef _DEBUG
  while (1) {
    a++;
  }
#else
  exit(1);
#endif
}

static uint32_t getRand(void) {
  unsigned int ret, a = 0;
  if (rand_s(&ret) == EINVAL) {
#ifdef _DEBUG
    while (1) {
      a++;
    }
#else
    exit(1);
#endif
  }
  return ret;
}

int traceHandler(const char *format, ...) {
  char buf[1024]; // Not thread safe
  int n;
  va_list argptr;
  va_start(argptr, format);
  n = vsprintf(buf, format, argptr);
  va_end(argptr);
  if (traceBuffer != NULL) {
    if (traceLocation + n > TRACE_BUFFER_SIZE) {
      uint32_t tail = TRACE_BUFFER_SIZE - traceLocation;
      memcpy(&traceBuffer[traceLocation], buf, tail);
      n -= tail;
      memcpy(traceBuffer, &buf[tail], n);
      traceLocation = n;
    } else {
      memcpy(&traceBuffer[traceLocation], buf, n);
      traceLocation += n;
    }
  }
  return 0;
}

void writeTraceToFile(void) {
  traceFile = fopen("ufat_trace.txt", "wb");
  if (traceFile != NULL) {
    uint32_t tail = TRACE_BUFFER_SIZE - traceLocation;
    fwrite(&traceBuffer[traceLocation], 1, tail, traceFile);
    fwrite(traceBuffer, 1, traceLocation, traceFile);
    fclose(traceFile);
  }
}

int PowerFailOnWriteTest(ufat_fs_t *fs) {
  ufat_FILE f;
  uint32_t i, j, tl, testLength, powered;
  uint32_t rnd = 0;
  int32_t res = 0;
  uint8_t *validate = malloc(0x10000);
  uint8_t *newwrite = malloc(0x10000);
  uint8_t *compare = malloc(0x10000);
  uint8_t buf[128];
  assert(compare);
  assert(validate);
  assert(newwrite);
  srand((unsigned)time(NULL));
  j = 0;
  for (i = 0; i < 0x10000; i++) {
    validate[i] = (uint8_t)getRand();
    newwrite[i] = (uint8_t)getRand();
  }
  res = i = 0;
  res = ufat_format(fs);
  res = ufat_mount(fs);
  res = ufat_fopen(fs, "validate.bin", "w", &f);
  res = ufat_fwrite(fs, validate, 1, 0x123, &f);
  res = ufat_fclose(fs, &f);

  res = ufat_mount(fs);
  if (res) {
    printf("Mount failed err %i\r\n", res);
    goto finalize;
  }
  // Write halfway
  if (memcmp(compare, newwrite, 0x123) == 0) {
    printf("What!!?\r\n");
    res = 1;
    goto finalize;
  }
  res = ufat_fopen(fs, "validate.bin", "w", &f);
  res = ufat_fwrite(fs, newwrite, 1, 0x8000, &f);
  // Power failure here.
  res = ufat_mount(fs);
  res = ufat_fopen(fs, "validate.bin", "r", &f);
  if (res != UFAT_OK) {
    printf("File open failure\r\n");
    res = 1;
    goto finalize;
  }
  res = ufat_fread(fs, compare, 1, 0x123, &f);
  if (res != 0x123) {
    printf("File Failed %i %i\r\n", res, ufat_ferror(&f));
    if (res == 0) {
      res = ufat_ferror(&f);
    }
    goto finalize;
    ;
  }
  if (memcmp(compare, validate, 0x123)) {
    printf("File Failed\r\n");
    res = 1;
    goto finalize;
  }
  res = ufat_fclose(fs, &f);
  if (res) {
    printf("File close Failed %i\r\n", res);
    goto finalize;
  }
  printf("Half write test passed\r\n");
finalize:
  free(newwrite);
  free(validate);
  free(compare);
  return res;
}

int32_t traceHelper[64];
uint32_t traceIndex = 0;
void tracePoint(int32_t p) {
  traceHelper[traceIndex] = p;
  traceIndex++;
  if (traceIndex == 64) {
    traceIndex = 0;
  }
}

int PowerStressTest(ufat_fs_t *fs) {
  uint32_t i, j, tl, testLength, powered;
  uint32_t powerCycleTest = 0;
  uint32_t powerCycleTestResult = 0;
  uint32_t powerCycleValidate = 0;
  uint64_t bytesWritten = 0;
  uint32_t rnd = 0;
  uint32_t cycles = POWER_CYCLE_COUNT;
  int32_t res = 0;
  uint8_t *test = malloc(0x10000);
  uint8_t *validate = malloc(0x10000);
  uint8_t *compare = malloc(0x10000);
  uint8_t buf[128];
  ufat_FILE f;
  assert(test);
  assert(compare);
  assert(validate);
  srand((unsigned)time(NULL));
  j = 0;
  for (i = 0; i < 0x10000; i++) {
    test[i] = (uint8_t)getRand();
  }
  for (i = 0; i < 0x10000; i++) {
    validate[i] = (uint8_t)getRand();
  }
  res = i = 0;
  res = ufat_format(fs);
  res = ufat_mount(fs);
  if (res != UFAT_OK) {
    return res;
  }
  res = ufat_fopen(fs, "validate.bin", "w", &f);
  res = ufat_fwrite(fs, validate, 1, 0x123, &f);
  res = ufat_fclose(fs, &f);
#if 1
  res = ufat_fopen(fs, "powercycles.txt", "w", &f);
  if (res == UFAT_ERR_FILE_NOT_FOUND) {
    return ufat_errno(fs);
  }
  res = ufat_fwrite(fs, &powerCycleTest, 1, 4, &f);
  res = ufat_fclose(fs, &f);
#endif
  while (res == 0) {
    powered = 1;
    takeDownTest = 1;
    switch (getRand() % 3) {
    case 0:
      takeDownFlags = TAKE_DOWN_WRITE;
      takeDownPeriod = 1 + (getRand() % 1500);
      break;
    case 1:
      takeDownFlags = TAKE_DOWN_WRITE | TAKE_DOWN_READ;
      takeDownPeriod = 1 + (getRand() % 10);
      break;
    case 2:
    default:
      takeDownFlags = TAKE_DOWN_WRITE;
      takeDownPeriod = 1 + (getRand() % 2500);
      break;
    }

    // printf("Mount attempt .......\r\n");
    res = ufat_mount(fs);
    if (res == UFAT_ERR_IO) {
      res = 0;
      continue;
    }
    if (res) {
      printf("Mount failed err %i\r\n", res);
      break;
    }
    if (cycles % 100 == 0) {
      printf(".");
    }

    if (cycles % 1000 == 0) {
      printf("%05i", cycles);
    }

    if (cycles % 5000 == 0) {
      printf("\r\n");
    }
    takeDownTest = 0;
    res = ufat_fopen(fs, "validate.bin", "r", &f);
    if (res != UFAT_OK) {
      if (ufat_errno(fs) != UFAT_ERR_IO) {
        printf("File Failed %i\r\n", res);
        break;
      }
      res = 0;
      continue;
    }
    res = ufat_fread(fs, compare, 1, 0x123, &f);
    if (res != 0x123) {
      printf("File Failed %i\r\n", res);
      if (res == 0) {
        res = ufat_ferror(&f);
        if (res == UFAT_ERR_IO) {
          res = 0;
          continue;
        }
      }
      break;
    }
    if (memcmp(compare, validate, 0x123)) {
      printf("File Failed\r\n");
      break;
    }
    res = ufat_fclose(fs, &f);
    if (res) {
      printf("File close Failed %i\r\n", res);
      break;
    }
    takeDownTest = 1;
    // powerCycleTest = 0;
    res = ufat_fopen(fs, "powercycles.txt", "rb", &f);
    // if (res != UFAT_ERR_FILE_NOT_FOUND) {
    if (res == UFAT_OK) {
      res = ufat_fread(fs, &powerCycleTestResult, 1, 4, &f);
      res = ufat_fclose(fs, &f);
      if (res == 0) {
        if (powerCycleValidate > powerCycleTestResult) {
          res = 10;
          break;
        }
        powerCycleValidate = powerCycleTestResult;
      }
      powerCycleTest = powerCycleTestResult + 1;
      res = ufat_fopen(fs, "powercycles.txt", "wb", &f);
      if (res == UFAT_OK) {
        res = ufat_fwrite(fs, &powerCycleTest, 1, 4, &f);
        res = ufat_fclose(fs, &f);
      }
    } else {
      if (ufat_errno(fs) != UFAT_ERR_IO) {
        return ufat_errno(fs);
      }
    }

    while (res == 0) {
      // Save and stuff until it dies
      sprintf(buf, "test%i.txt", i++ % 3);
      res = ufat_fopen(fs, buf, "w", &f);
      if (res == UFAT_ERR_FILE_NOT_FOUND) {
        if (ufat_errno(fs) != UFAT_ERR_IO) {
          res = UFAT_ERR_NULL;
        } else {
          res = UFAT_ERR_IO;
        }
        break;
      }
      testLength = (test[i % 0x8000] << 8) + test[i % 0x7999];
      if (testLength == 0) {
        testLength = 1;
      }
      testLength &= 0x3;
      j = testLength;
      while (j) {
        tl = test[j]; // Some random test length
        tl &= 0xF;
        if (!tl) {
          tl = rand();
          tl &= 0xF;
        }
        if (tl > j) {
          tl = j;
        }
        if (!tl) {
          tl++;
        }
        res = ufat_fwrite(fs, &test[testLength - j], 1, tl, &f);
        if (res != tl) {
          if (res == 0) {
            res = ufat_ferror(&f);
          }
          break;
        }
        bytesWritten += res;
        j -= tl;
      }
      res = ufat_fclose(fs, &f);
      if (res) {
        if (res != UFAT_ERR_IO) {
          printf("Error on close res %i\r\n", res);
          break;
        }
      }
    }
    if (res != UFAT_ERR_IO) {
      printf("\r\nPower test stress failed err %i\r\n", res);
      break;
    }
    res = 0;
    if (cycles-- == 0) {
      printf("\r\nPower stress test passed (%i/%i)\r\n%i KB written\r\n",
             powerCycleTestResult, POWER_CYCLE_COUNT,
             (int)(bytesWritten / 1000));
      break;
    }
  }
  free(test);
  free(validate);
  free(compare);
  if (res) {
    return res;
  }
  takeDownTest = 0;
  res = ufat_mount(fs);
  if (res) {
    printf("Mount failed err %i\r\n", res);
  }
  char pBuff[1024];
  (void)ufat_fsinfo(fs, pBuff, sizeof(pBuff));
  printf("%s", pBuff);
#ifdef UFAT_COVERAGE_TEST
  (void)ufat_fsMetaData(pBuff, sizeof(pBuff));
  printf("%s", pBuff);
#endif
  return res;
}

int fillupTest(ufat_fs_t *fs) {
  uint32_t i, j;
  uint32_t cycles;
  int32_t res = 0;
  uint8_t *test = malloc(0x10000);
  uint8_t buf[128];
  ufat_FILE f;
  assert(test);
  srand((unsigned)time(NULL));
  j = 0;
  for (i = 0; i < 0x10000; i++) {
    test[i] = (uint8_t)getRand();
  }
  res = i = 0;
  res = ufat_format(fs);
  res = ufat_mount(fs);
  // Does it really fill up?
  res = 0;
  while (res == 0) {
    sprintf(buf, "test%i.txt", i++);
    res = ufat_fopen(fs, buf, "w", &f);
    if (res == UFAT_ERR_FILE_NOT_FOUND) {
      res = UFAT_ERR_NULL;
    }
    uint32_t wl = (test[i] << 8) + test[i];
    if (!wl) {
      wl = 1;
    }
    res = ufat_fwrite(fs, test, 1, wl, &f);
    if (res <= 0) {
      ufat_fclose(fs, &f);
      break;
    }
    res = ufat_fclose(fs, &f);
  }
  free(test);
  if (res != UFAT_ERR_FULL) {
    printf("Test failed, did not fill up err %i\r\n", res);
    return 1;
  } else {
    printf("Fill up test passed\r\n");
    return 0;
  }
}

int randomWriteLengths(ufat_fs_t *fs) {
  int32_t testCount = 1000;
  uint32_t i, j, tl, testLength;
  int32_t res = 0;
  uint8_t *test = malloc(0x10000);
  uint8_t *compare = malloc(0x10000);
  uint8_t buf[128];
  ufat_FILE f;
  assert(test);
  assert(compare);
  srand((unsigned)time(NULL));
  j = 0;
  for (i = 0; i < 0x10000; i++) {
    test[i] = (uint8_t)getRand();
    ;
  }

  // Rollover
  res = ufat_format(fs);
  res = ufat_mount(fs);
  res = 0;
  i = 0;
  while (res == 0) {
    sprintf(buf, "test%i.txt", i++ % 10);
    res = ufat_fopen(fs, buf, "w", &f);
    if (res == UFAT_ERR_FILE_NOT_FOUND) {
      res = UFAT_ERR_NULL;
      break;
    }
    testLength = (uint8_t)getRand();
    testLength %= 0xFFFF;
    if (testLength == 0) {
      testLength = 1;
    }
    j = testLength;
    while (j) {
      // tl = test[j];//Some random test length

      tl = (uint8_t)getRand();
      tl %= testLength;
      if (!tl) {
        tl = 1;
      }
      if (tl > j) {
        tl = j;
      }
      res = ufat_fwrite(fs, &test[testLength - j], 1, tl, &f);
      if (res != tl) {
        // ufat_fclose(fs, f);
        break;
      }
      j -= tl;
    }

    res = ufat_fclose(fs, &f);
    if (testCount-- >= 0) {
      break;
    }
    res = ufat_fopen(fs, buf, "r", &f);
    if (res == UFAT_ERR_FILE_NOT_FOUND) {
      res = UFAT_ERR_NULL;
    } else {
      res = ufat_fread(fs, compare, 1, testLength, &f);
      if (res != testLength) {
        ufat_fclose(fs, &f);
        printf("Test file did not read\r\n");
        break;
      }
      if (memcmp(test, compare, testLength)) {
        ufat_fclose(fs, &f);
        for (i = 0; i < testLength; i++) {
          if (test[i] != compare[i]) {
            break;
          }
        }
        res = 10;
        printf("Test file did not match at %i 0x%X 0x%X 0x%p 0x%p\r\n", i,
               test[i], compare[i], &test[i], &compare[i]);
        break;
      }
      res = ufat_fclose(fs, &f);
    }
  }
  free(test);
  free(compare);
  if (res != UFAT_OK) {
    printf("Test failed, some rollover issue\r\n");
    return 1;
  } else {
    printf("\r\n");
    char pBuff[512];
    ufat_fsinfo(fs, pBuff, sizeof(pBuff));
    printf("\r\nRollover test succeeded\r\n");
    return 0;
  }
}

int deleteTest(ufat_fs_t *fs) {
  int res;
  ufat_FILE f;
  res = ufat_format(fs);
  res = ufat_mount(fs);
  res = ufat_fopen(fs, "testfile.bin", "wb", &f);
  if (res == UFAT_ERR_FILE_NOT_FOUND) {
    return 1;
  }
  res = ufat_fwrite(fs, "Hello world!", 1, 12, &f);
  if (res != 12) {
    ufat_fclose(fs, &f);
    return 1;
  }
  res = ufat_fclose(fs, &f);
  if (res) {
    return 1;
  }
  if (ufat_exists(fs, "testfile.bin") != 12) {
    printf("File doesn't exist where it should\r\n");
    return 1;
  }
  res = ufat_remove(fs, "testfile.bin");
  if (res) {
    printf("File remove error\r\n");
    return 1;
  }
  if (ufat_exists(fs, "testfile.bin") != 0) {
    printf("File exists where it shouldn't\r\n");
    return 1;
  }
  printf("File remove test passed\r\n");
  return 0;
}

int runTestSuite(ufat_fs_t *fs) {
  int res = 0;
  res = PowerStressTest(fs);
  if (res) {
    printf("Power cycle stress test failed err %s\r\n", ufat_errstr(res));
    writeTraceToFile();
    return res;
  }
  res = deleteTest(fs);
  if (res) {
    printf("Delete test err %s\r\n", ufat_errstr(res));
    writeTraceToFile();
    return res;
  }

  res = fillupTest(fs);
  if (res) {
    printf("Fill up test err %s\r\n", ufat_errstr(res));
    writeTraceToFile();
    return res;
  }
  res = randomWriteLengths(fs);
  if (res) {
    printf("Random write length test err %s\r\n", ufat_errstr(res));
    writeTraceToFile();
    return res;
  }

  res = PowerFailOnWriteTest(fs);
  if (res) {
    printf("Half write test failed %s\r\n", ufat_errstr(res));
    writeTraceToFile();
    return res;
  }
  printf("Passed all tests\r\n");
  return res;
}

/*
 *
 */
int main(int argv, char **argc) {
  ufat_fs_t fs1 = {.addressStart = 0,
                   .sectors = FAKE_PROM_SIZE / FAKE_PROM_SECTOR_SIZE,
                   .sectorSize = FAKE_PROM_SECTOR_SIZE,
                   .tableSectors = (FAKE_PROM_SIZE / FAKE_PROM_SECTOR_SIZE) *
                                   UFAT_TABLE_COUNT / FAKE_PROM_SECTOR_SIZE,
                   .write_block_device = write_block_page,
                   .read_block_device = read_block_device};

    printf("norFAT test jig 1.00, norFAT Version %s\r\n", UFAT_VERSION);
  if (argv > 1) {
    POWER_CYCLE_COUNT = strtol(argc[1], NULL, 10);
    printf("Testing cycles set to %i\r\n", POWER_CYCLE_COUNT);
  }
  memset(block, 0, FAKE_PROM_SIZE);
  fs1.buff = malloc(FAKE_PROM_TABLE_SECTORS * FAKE_PROM_SECTOR_SIZE *
                    sizeof(ufat_sector_t));
  fs1.fat = malloc(FAKE_PROM_TABLE_SECTORS * FAKE_PROM_SECTOR_SIZE *
                   sizeof(ufat_sector_t));
  traceBuffer = malloc(TRACE_BUFFER_SIZE);
  // traceFile = fopen("ufat_trace.txt", "wb");
  int32_t res = ufat_mount(&fs1);
  if (res == UFAT_ERR_EMPTY) {
    res = ufat_format(&fs1);
    res = ufat_mount(&fs1);
  }
  res = runTestSuite(&fs1);
  if (res) {
    printf("Press Enter to close\n");
    (void)getchar();
    return res;
  }
  free(traceBuffer);
  printf("Press Enter to close\n");
  (void)getchar();
  return res;

  return (EXIT_SUCCESS);
}

