#define _CRT_RAND_S
#include <stdlib.h>
#include "unity.h"
#include "unity_fixture.h"
#include "microFS.h"
#include "microFSconfig.h"
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _CRT_RAND_S
int rand_s(unsigned int *randomValue);
#endif

#define FAKE_PROM_SIZE 0x2000
#define FAKE_PROM_SECTOR_SIZE 64
#define FAKE_PROM_TABLE_SECTORS                                                \
  ((FAKE_PROM_SIZE / FAKE_PROM_SECTOR_SIZE) / FAKE_PROM_SECTOR_SIZE)

static uint8_t block[FAKE_PROM_SIZE];

#define TAKE_DOWN_READ (1UL << 0)
#define TAKE_DOWN_WRITE (1UL << 1)

uint32_t POWER_CYCLE_COUNT = 49999;
uint32_t takeDownPeriod = 0;
uint32_t takeDownTest = 0;
uint32_t takeDownFlags = 0;
uint8_t *test;
uint8_t *validate;
uint8_t *compare;

uint32_t read_block_device(uint32_t address, uint8_t *data, uint32_t len) {
  TEST_ASSERT_MESSAGE(address + len <= FAKE_PROM_SIZE,
                      "Out of range address at read_block_device");
  if (takeDownTest && (takeDownFlags & TAKE_DOWN_READ)) {

    if (takeDownPeriod != 0) {
      takeDownPeriod--;
    } else {
      memcpy(data, &block[address], len / 2);
      return 1;
    }
  }
  memcpy(data, &block[address], len);
  return 0;
}

uint32_t write_block_page(uint32_t address, uint8_t *data, uint32_t length) {
  uint32_t i;
  TEST_ASSERT_MESSAGE(address + length <= FAKE_PROM_SIZE,
                      "Out of range address at read_block_device");
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
  return 0;
}

static uint32_t getRand(void) {
  unsigned int ret;
  if (rand_s(&ret) == EINVAL) {
    TEST_FAIL_MESSAGE("getRand failure");
  }
  return ret;
}

ufat_fs_t fs1 = {.addressStart = 0,
                 .sectors = FAKE_PROM_SIZE / FAKE_PROM_SECTOR_SIZE,
                 .sectorSize = FAKE_PROM_SECTOR_SIZE,
                 .tableSectors = (FAKE_PROM_SIZE / FAKE_PROM_SECTOR_SIZE) *
                                 UFAT_TABLE_COUNT / FAKE_PROM_SECTOR_SIZE,
                 .write_block_device = write_block_page,
                 .read_block_device = read_block_device};

TEST_GROUP(POWERSTRESS);

TEST_SETUP(POWERSTRESS) {
  takeDownTest = 0;
  memset(block, 0, FAKE_PROM_SIZE);
  fs1.buff = malloc(FAKE_PROM_TABLE_SECTORS * FAKE_PROM_SECTOR_SIZE *
                    sizeof(ufat_sector_t));
  fs1.fat = malloc(FAKE_PROM_TABLE_SECTORS * FAKE_PROM_SECTOR_SIZE *
                   sizeof(ufat_sector_t));
  test = malloc(0x2000);
  validate = malloc(0x2000);
  compare = malloc(0x2000);
}

TEST_TEAR_DOWN(POWERSTRESS) { 
    free(fs1.buff); 
    free(fs1.fat);
    free(test);
    free(validate);
    free(compare);
}

int PowerStressTest(ufat_fs_t *fs) {
  uint32_t i, j, tl, testLength;
  uint32_t powerCycleTest = 0;
  uint32_t powerCycleTestResult = 0;
  uint32_t powerCycleValidate = 0;
  uint64_t bytesWritten = 0;
  uint32_t cycles = POWER_CYCLE_COUNT;
  int32_t res = 0;
  char buf[128];
  ufat_FILE f;
  TEST_ASSERT(test);
  TEST_ASSERT(compare);
  TEST_ASSERT(validate);
  srand((unsigned)time(NULL));
  TEST_MESSAGE("Starting PowerStressTest");
  j = 0;
  for (i = 0; i < 0x1000; i++) {
    test[i] = (uint8_t)getRand();
  }

  for (i = 0; i < 0x1000; i++) {
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

  res = ufat_fopen(fs, "powercycles.txt", "w", &f);
  if (res == UFAT_ERR_FILE_NOT_FOUND) {
    TEST_MESSAGE("UFAT_ERR_FILE_NOT_FOUND");
    return ufat_errno(fs);
  }
  res = ufat_fwrite(fs, &powerCycleTest, 1, 4, &f);
  res = ufat_fclose(fs, &f);

  while (res == 0) {
    takeDownTest = 1;
    switch (getRand() % 3) {
    case 0:
      takeDownFlags = TAKE_DOWN_WRITE;
      takeDownPeriod = 1 + (getRand() % 250);
      break;
    case 1:
      takeDownFlags = TAKE_DOWN_WRITE | TAKE_DOWN_READ;
      takeDownPeriod = 1 + (getRand() % 10);
      break;
    case 2:
    default:
      takeDownFlags = TAKE_DOWN_WRITE;
      takeDownPeriod = 1 + (getRand() % 500);
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

    if (cycles % 1000 == 0) {
      printf(".");
    }

    if (cycles % 10000 == 0) {
      printf("%06i", cycles);
    }

    if (cycles % 50000 == 0) {
      printf("\r\n");
    }

    takeDownTest = 0;
    res = ufat_fopen(fs, "validate.bin", "r", &f);
    if (res != UFAT_OK) {
      if (ufat_errno(fs) != UFAT_ERR_IO) {
        printf("File Failed %i\r\n", ufat_errstr(res));
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
        printf("Failing at powercycle find err %s\r\n",
               ufat_errstr(ufat_errno(fs)));
        return ufat_errno(fs);
      }
    }

    while (res == 0) {
      // Save and stuff until it dies
      sprintf(buf, "test%i.txt", i++ % 5);
      res = ufat_fopen(fs, buf, "w", &f);
      if (res == UFAT_ERR_FILE_NOT_FOUND) {
        if (ufat_errno(fs) != UFAT_ERR_IO) {
          res = UFAT_ERR_NULL;
        } else {
          res = UFAT_ERR_IO;
        }
        break;
      }
      testLength = (test[i % 0x800] << 8) + test[i % 0x799];
      if (testLength == 0) {
        testLength = 1;
      }
      testLength &= 0x31;
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
        if (res != (int32_t)tl) {
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
      printf("\r\nPower test stress failed err %s\r\n", ufat_errstr(res));
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
  if (res) {
    TEST_MESSAGE("Failed");
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
  TEST_MESSAGE("Power Stress Test passed");
  return res;
}

int deleteTest(ufat_fs_t *fs) {
  int res;
  ufat_FILE f;
  res = ufat_format(fs);
  res = ufat_mount(fs);
  res = ufat_fopen(fs, "testfile.bin", "wb", &f);
  if (res == UFAT_ERR_FILE_NOT_FOUND) {
    TEST_MESSAGE("testfile.bin UFAT_ERR_FILE_NOT_FOUND");
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
    TEST_MESSAGE("File doesn't exist where it should");
    return 1;
  }
  res = ufat_remove(fs, "testfile.bin");
  if (res) {
    TEST_MESSAGE("File remove error");
    return 1;
  }
  if (ufat_exists(fs, "testfile.bin") != 0) {
    TEST_MESSAGE("File exists where it shouldn't");
    return 1;
  }
  TEST_MESSAGE("File remove test passed");
  return 0;
}

int fillupTest(ufat_fs_t *fs) {
  int32_t res = 0;
  uint32_t i;
  char buf[128];
  ufat_FILE f;
  assert(test);
  srand((unsigned)time(NULL));
  for (i = 0; i < 0x1000; i++) {
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
    sprintf(buf, "Test failed, did not fill up err %s", ufat_errstr(res));
    TEST_MESSAGE((const char *)buf);
    return 1;
  } else {
    TEST_MESSAGE("Fill up test passed");
    return 0;
  }
}

int randomWriteLengths(ufat_fs_t *fs) {
  int32_t testCount = 1000;
  int32_t i, j;
  int32_t res = 0;
  int32_t tl, testLength;
  char buf[128];
  ufat_FILE f;
  assert(test);
  assert(compare);
  srand((unsigned)time(NULL));
  j = 0;
  for (i = 0; i < 0x1000; i++) {
    test[i] = (uint8_t)getRand();
    ;
  }

  // Rollover
  res = ufat_format(fs);
  TEST_ASSERT_MESSAGE(res == UFAT_OK, "Format failed");
  res = ufat_mount(fs);
  TEST_ASSERT_MESSAGE(res == UFAT_OK, "Mount failed");
  res = 0;
  i = 0;
  while (res == 0) {
    sprintf(buf, "test%i.txt", i++ % 10);
    res = ufat_fopen(fs, buf, "w", &f);
    if (res == UFAT_ERR_FILE_NOT_FOUND) {
      res = UFAT_ERR_NULL;
      TEST_FAIL_MESSAGE("UFAT_ERR_FILE_NOT_FOUND");
      break;
    }
    // sprintf(buf, "ufat_fopen error = %s", ufat_errstr(res));
    TEST_ASSERT_EQUAL_INT32_MESSAGE(res, UFAT_OK, "ufat_fopen error");
    // TEST_ASSERT_MESSAGE(res == UFAT_OK, "ufat_fopen error");
    testLength = (uint8_t)getRand();
    testLength %= 0xFF;
    if (testLength == 0) {
      testLength = 1;
    }
    j = testLength;
    while (j) {
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
        break;
      }
      j -= tl;
    }
    res = ufat_fclose(fs, &f);
    if (testCount-- == 0) {
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
            printf("Test failed at %i\r\n", i);
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
  if (res != UFAT_OK) {
    sprintf(buf, "Test failed, some rollover issue %s", ufat_errstr(res));
    TEST_MESSAGE(buf);
    return 1;
  } else {
    printf("\r\n");
    char pBuff[2048];
    ufat_fsinfo(fs, pBuff, sizeof(pBuff));
    printf(pBuff);
    TEST_MESSAGE("Rollover test succeeded");
    return 0;
  }
}

TEST(POWERSTRESS, TestPowerStress) {
  TEST_ASSERT_EQUAL(UFAT_OK, PowerStressTest(&fs1));
  TEST_ASSERT_EQUAL(0, deleteTest(&fs1));
  TEST_ASSERT_EQUAL(0, fillupTest(&fs1));
  TEST_ASSERT_EQUAL(0, randomWriteLengths(&fs1));
  printf("Completed");
}