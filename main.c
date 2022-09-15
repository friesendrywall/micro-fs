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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "src/microFS.h"

/*
 * 
 */
int main(int argc, char **argv) {
  uint8_t test[1024];
  micro_fs_format();
  uint32_t i;
  int16_t res;
  uint8_t random[256];
  for (i = 0; i < 256; i++) {
    random[i] = i;
  }
  micro_fwrite("test.bin", random, 64);
  res = micro_fread("test.bin", test, 256);
  res = micro_fs_stat("test.bin");
  // res = miniFsErase("test.bin");
  res = micro_fs_stat("test.bin");
  res = micro_fwrite("tester.bin", random, 256);
  res = micro_fread("tester.bin", test, 256);
  res = micro_fwrite("tsting.bin", random, 64);
  res = micro_fs_info(test, sizeof(test));
  printf(test);
  return (EXIT_SUCCESS);
}

