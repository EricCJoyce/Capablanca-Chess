#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ZHASH_TABLE_SIZE 1257                                       /* Total number of hash keys. */

/* Print a comma-separated string of unsigned chars representing the byte array that becomes a Zobrist Hasher.
   The first number is always ZHASH_TABLE_SIZE, the total number of hash keys.
   Then, every set of 8 numbers is: [value as 8 unsigned bytes].
   Counting sets of 8 using ctr initialized to 0,
   e.g. [209,251,103,184,60,79,96,161] is
          ctr  ==> (209 << 0) | (251 << 8) | (103 << 16) | (184 << 24) | (60 << 32) | (79 << 40) | (96 << 48) | (161 << 56)
         WP_A2 ==> (209 << 0) | (251 << 8) | (103 << 16) | (184 << 24) | (60 << 32) | (79 << 40) | (96 << 48) | (161 << 56)
           0   ==> 11628381360081075153 */
int main(void)
  {
    unsigned long long h;
    unsigned char buffer8[8];
    unsigned int i;
    unsigned char j;

    srand(time(NULL));                                              //  Seed randomizer.

    printf("%d,", ZHASH_TABLE_SIZE);                                //  Output the number of hash keys.

    for(i = 0; i < ZHASH_TABLE_SIZE; i++)
      {
        h = rand() ^ ((unsigned long long)rand() << 15) ^
                     ((unsigned long long)rand() << 30) ^
                     ((unsigned long long)rand() << 45) ^
                     ((unsigned long long)rand() << 60);

        memcpy(buffer8, (unsigned char*)(&h), 8);                   //  Force the unsigned long long into an 8-byte buffer.
        for(j = 0; j < 8; j++)                                      //  Output bytes.
          {
            printf("%d", buffer8[j]);
            if(j < 7)
              printf(",");
          }
        if(i < ZHASH_TABLE_SIZE - 1)
          printf(",");
      }

    return 0;
  }