#include <stdio.h>
#include <stdlib.h>
#include "zobrist.h"                                                /* Have a single authority for the total number of hash keys. */

/* Print a comma-separated string of unsigned chars representing the byte array that becomes a Zobrist Hasher.
   The first number is always ZHASH_TABLE_SIZE, the total number of hash keys.
   Then, every set of 8 numbers is: [value as 8 unsigned bytes].
   Counting sets of 8 using ctr initialized to 0,
   e.g. [209,251,103,184,60,79,96,161] is
          ctr  ==> (209 << 0) | (251 << 8) | (103 << 16) | (184 << 24) | (60 << 32) | (79 << 40) | (96 << 48) | (161 << 56)
         WP_A2 ==> (209 << 0) | (251 << 8) | (103 << 16) | (184 << 24) | (60 << 32) | (79 << 40) | (96 << 48) | (161 << 56)
           0   ==> 11628381360081075153 */

#define ZHASH_BYTE_SIZE (ZHASH_TABLE_SIZE * 8)

int main(void)
  {
    unsigned char buffer[ZHASH_BYTE_SIZE];
    FILE *fp;
    unsigned int i;

    fp = fopen("/dev/urandom", "rb");
    if(fp == NULL)
      {
        fprintf(stderr, "Could not open /dev/urandom.\n");
        return EXIT_FAILURE;
      }

    if(fread(buffer, 1, ZHASH_BYTE_SIZE, fp) != ZHASH_BYTE_SIZE)
      {
        fprintf(stderr, "Could not read enough random bytes.\n");
        fclose(fp);
        return EXIT_FAILURE;
      }

    fclose(fp);

    printf("%d,", ZHASH_TABLE_SIZE);

    for(i = 0; i < ZHASH_BYTE_SIZE; i++)
      {
        printf("%u", (unsigned int)buffer[i]);

        if(i + 1 < ZHASH_BYTE_SIZE)
          printf(",");
      }

    return EXIT_SUCCESS;
  }