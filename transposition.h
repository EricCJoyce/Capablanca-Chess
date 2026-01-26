#ifndef __TRANSPOSITION_H
#define __TRANSPOSITION_H

#include <stdlib.h>
#include <string.h>                                                 /* Needed for memcpy(). */

#define _MOVE_BYTE_SIZE                3                            /* Number of bytes needed to store a Move structure. */
#define _NONE                         80                            /* Don't include gamestate just to get a constant. */

#define _TRANSPO_RECORD_BYTE_SIZE     18                            /* Number of bytes needed to store a TranspoRecord object. */
#define _TRANSPO_TABLE_SIZE       524288                            /* Number of TranspoRecords, each _TRANSPO_RECORD_BYTE_SIZE bytes.
                                                                       524,288 = 2^19.
                                                                       1 + _TRANSPO_TABLE_SIZE * _TRANSPO_RECORD_BYTE_SIZE == 9 MB,
                                                                       deemed sensible for Mobile/Tablets */
#define _TRANSPO_AGE_THRESHOLD        40                            /* Old enough to be replaced. */

#define NODE_TYPE_NONE                 0                            /* No entry. */
#define NODE_TYPE_PV                   1                            /* Score is exact. */
#define NODE_TYPE_ALL                  2                            /* Score is an upper bound. */
#define NODE_TYPE_CUT                  3                            /* Score is a lower bound. */

/**************************************************************************************************
 Typedefs  */

typedef struct TranspoRecordType                                    //  TOTAL: 18 = _TRANSPO_RECORD_BYTE_SIZE
  {
    unsigned long long lock;                                        //  (8 bytes) A copy of the Zobrist hash to match against.
                                                                    //            (MUCH cheaper than storing the game state's bytes!)
    unsigned char bestMove[_MOVE_BYTE_SIZE];                        //  (3 bytes) The best move for this record, stored as a byte array.
    unsigned char depth;                                            //  (1 byte)  The depth FROM WHICH evaluation of this node is ratified.
    float score;                                                    //  (4 bytes) 32 bits are plenty.
    unsigned char type;                                             //  (1 byte)  In {NODE_TYPE_NONE, NODE_TYPE_PV, NODE_TYPE_ALL, NODE_TYPE_CUT} as
                                                                    //            score is exact, an upper bound, or lower bound, respectively.
    unsigned char age;                                              //  (1 byte)  Used to determine when a record should be removed.
                                                                    //            If age == 0, then this entry is free to be overwritten.
  } TranspoRecord;

/**************************************************************************************************
 Prototypes  */

bool fetchRecord(unsigned int, TranspoRecord*);
unsigned char getGeneration(void);
void incGeneration(void);
unsigned int hashIndex(unsigned long long);
void serializeTranspoRecord(TranspoRecord*, unsigned char*);
void deserializeTranspoRecord(unsigned char*, TranspoRecord*);

/**************************************************************************************************
 Globals  */
                                                                    //  9,437,185 bytes = 1 + 524,288 * 18.
                                                                    //  Global array containing the serialized transposition table:
                                                                    //  Generation-Byte + sizeof(TranspoRecord) * size-of-table.
unsigned char transpositionTableBuffer[1 + _TRANSPO_TABLE_SIZE * _TRANSPO_RECORD_BYTE_SIZE];

/**************************************************************************************************
 Functions  */

/* Load whatever is at the given index and return whether its 'age' field is greater than zero. */
bool fetchRecord(unsigned int index, TranspoRecord* ttRecord)
  {
    deserializeTranspoRecord(1 + transpositionTableBuffer + index * _TRANSPO_RECORD_BYTE_SIZE, ttRecord);
    return (ttRecord->age > 0);
  }

/* The first byte of the transposition table's byte array is the "generation" for all records created at a given time. */
unsigned char getGeneration(void)
  {
    return transpositionTableBuffer[0];
  }

/* Enforce wrap-around from 255 to 1. */
void incGeneration(void)
  {
    if(transpositionTableBuffer[0] == 255)
      transpositionTableBuffer[0] = 1;
    else
      transpositionTableBuffer[0]++;
    return;
  }

/* Return an index into the transposition table buffer. */
unsigned int hashIndex(unsigned long long h)
  {
    return h % _TRANSPO_TABLE_SIZE;
  }

/* Encode the given TranspoRecord to the given byte array.
   When "buffer" is the global byte array "transpositionTableBuffer", it is at an offset into that array, treated locally. */
void serializeTranspoRecord(TranspoRecord* ttRecord, unsigned char* buffer)
  {
    unsigned int i = 0, j;
    unsigned char buffer4[4];

    memcpy(buffer4, (unsigned char*)(&ttRecord->lock), 4);          //  Force the unsigned int into a 4-byte temp buffer.
    for(j = 0; j < 4; j++)                                          //  Copy bytes to serial buffer.
      buffer[i++] = buffer4[j];

    for(j = 0; j < _MOVE_BYTE_SIZE; j++)                            //  Copy best-move's byte array to the serial buffer.
      buffer[i++] = ttRecord->bestMove[j];

    buffer[i++] = ttRecord->depth;                                  //  Copy the depth to the serial buffer.

    memcpy(buffer4, (unsigned char*)(&ttRecord->score), 4);         //  Force the float into a 4-byte temp buffer.
    for(j = 0; j < 4; j++)                                          //  Copy bytes to serial buffer.
      buffer[i++] = buffer4[j];

    buffer[i++] = ttRecord->type;                                   //  Copy the node's type to the serial buffer.

    buffer[i++] = ttRecord->age;                                    //  Copy the node's age to the serial buffer.

    return;
  }

/* Decode, from the given byte array, a TranspoRecord.
   When "buffer" is the global byte array "transpositionTableBuffer", it is at an offset into that array, treated locally. */
void deserializeTranspoRecord(unsigned char* buffer, TranspoRecord* ttRecord)
  {
    unsigned int i = 0, j;
    unsigned char buffer4[4];
    unsigned int ui4;
    float f4;

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the serial buffer.
      buffer4[j] = buffer[i++];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    ttRecord->lock = ui4;                                           //  Restore lock to the TranspoRecord.

    for(j = 0; j < _MOVE_BYTE_SIZE; j++)                            //  Restore bestMove to the TranspoRecord.
      ttRecord->bestMove[j] = buffer[i++];

    ttRecord->depth = buffer[i++];                                  //  Restore depth to the TranspoRecord.

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the serial buffer.
      buffer4[j] = buffer[i++];
    memcpy(&f4, buffer4, 4);                                        //  Force the 4-byte buffer into a float.
    ttRecord->score = f4;                                           //  Restore score to the TranspoRecord.

    ttRecord->type = buffer[i++];                                   //  Restore type to the TranspoRecord.

    ttRecord->age = buffer[i++];                                    //  Restore age to the TranspoRecord.

    return;
  }

#endif