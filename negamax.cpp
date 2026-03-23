/*

sudo docker run --rm -v $(pwd):/src -u $(id -u):$(id -g) --mount type=bind,source=$(pwd),target=/home/src emscripten-c em++ -I ./ -Os -s STANDALONE_WASM -s INITIAL_MEMORY=19857408 -s STACK_SIZE=1048576 -s EXPORTED_FUNCTIONS="['_getMaxPly','_getInputBuffer','_getParametersBuffer','_getQueryGameStateBuffer','_getQueryMoveBuffer','_getAnswerGameStateBuffer','_getAnswerMovesBuffer','_getOutputBuffer','_getZobristHashBuffer','_getTranspositionTableBuffer','_getNegamaxSearchBuffer','_getNegamaxMovesBuffer','_getKillerMovesBuffer','_getHistoryTableBuffer','_setSearchId','_getSearchId','_getStatus','_setControlFlag','_unsetControlFlag','_getControlByte','_setTargetDepth','_getTargetDepth','_getDepthAchieved','_setDeadline','_getDeadline','_resetNodesSearched','_getNodesSearched','_finalDepthAchieved','_finalScore','_getNodeStackSize','_getMovesArenaSize','_initSearch','_negamax']" -Wl,--no-entry "negamax.cpp" -o "negamax.wasm"

*/

#include <algorithm>                                                /* For std::max() and std::min(). */
#include <limits>                                                   /* For std::numeric_limits. */

#include "transposition.h"                                          /* Include the Transposition Table library. */
#include "zobrist.h"                                                /* Include the Zobrist hasher, which is an array of unsigned long longs (64-bit ints). */

#define _GAMESTATE_BYTE_SIZE                    84                  /* Number of bytes needed to encode a game state. */
#define _MOVE_BYTE_SIZE                          3                  /* Number of bytes needed to encode a move. */
#define _MAX_MOVES                             512                  /* A (generous) upper bound on how many moves may be made by a team in a single turn. */
#define _NONE                                   80                  /* Required as a "blank" value without #include "gamestate.h". */
#define _NO_PROMO                                0                  /* Required as a "blank" value without #include "gamestate.h". */

#define _WHITE_TO_MOVE                           0                  /* Copied from gamestate.h. */
#define _BLACK_TO_MOVE                           1                  /* Copied from gamestate.h. */

#define _MAX_PLY                                15                  /* Deepest possible depth. */
#define _QUIESCENCE_MAX_PLY                      4                  /* Maximum extension for quiescence search. */

#define _PARAMETER_ARRAY_SIZE                   16                  /* Number of bytes needed to store search parameters. */

#define _TREE_SEARCH_ARRAY_SIZE              65536                  /* Number of (game-state bytes, move-bytes). */
#define _NEGAMAX_NODE_BYTE_SIZE                138                  /* Number of bytes needed to store a negamax node. */
#define _NEGAMAX_MOVE_BYTE_SIZE                  4                  /* Number of bytes needed to store a negamax move. */

#define _KILLER_MOVE_PER_PLY                     2                  /* Typical for other chess engines. */
#define _KILLER_MOVE_MAX_DEPTH                  64                  /* Simply something "comfortably large". */
#define KILLER_NOT_FOUND                         0
#define KILLER_FOUND_FIRST                       1
#define KILLER_FOUND_SECOND                      2

#define PARAM_BUFFER_SEARCHID_OFFSET          0x00                  /* Bytes into "inputParametersBuffer", where the search ID begins. */
#define PARAM_BUFFER_STATUS_OFFSET            0x04                  /* Bytes into "inputParametersBuffer", where the status byte exists. */
#define PARAM_BUFFER_COMMAND_OFFSET           0x05                  /* Bytes into "inputParametersBuffer", where the command byte exists. */
#define PARAM_BUFFER_TARGETDEPTH_OFFSET       0x06                  /* Bytes into "inputParametersBuffer", where the target depth byte exists. */
#define PARAM_BUFFER_DEPTHACHIEVED_OFFSET     0x07                  /* Bytes into "inputParametersBuffer", where the depth achieved byte exists. */
#define PARAM_BUFFER_DEADLINE_OFFSET          0x08                  /* Bytes into "inputParametersBuffer", where the deadline (milliseconds) begins. */
#define PARAM_BUFFER_NODESSEARCHED_OFFSET     0x0C                  /* Bytes into "inputParametersBuffer", where the node count begins. */

#define STATUS_IDLE                           0x00                  /* No search running. Awaiting instructions. */
#define STATUS_RUNNING                        0x01                  /* Search running. */
#define STATUS_DONE                           0x02                  /* Search complete. */
#define STATUS_STOP_REQUESTED                 0x03                  /* Will halt the present search at the next safe point. */
#define STATUS_STOP_TIME                      0x04                  /* Will halt the present search at the next safe point, owing to time constraints. */
#define STATUS_ABORTED                        0x05                  /* Search was hard-killed: be wary of partial results. */
#define STATUS_ERROR                          0xFF                  /* An error has occurred. */

#define CTRL_STOP_REQUESTED                   0x01                  /* Set this byte in commandFlags to request that the present search stop. */
#define CTRL_HARD_ABORT                       0x02                  /* Set this byte in commandFlags to request that the present search abort. */
#define CTRL_TIME_ENABLED                     0x04                  /* Set this byte in commandFlags to indicate that search is timed. */
#define CTRL_PONDERING                        0x08                  /* Set this byte in commandFlags to indicate that search occurs during opponent's turn. */

#define _PHASE_ENTER_NODE                        0                  /* Go to  when entering negamax(). */
#define _PHASE_GEN_AND_ORDER                     1                  /* Go to  when entering negamax(). */
#define _PHASE_NEXT_MOVE                         2                  /* Go to  when entering negamax(). */
#define _PHASE_AFTER_CHILD                       3                  /* Go to  when entering negamax(). */
#define _PHASE_FINISH_NODE                       4                  /* Go to  when entering negamax(). */
#define _PHASE_COMPLETE                          5                  /* Write to output buffer when entering negamax() and signal search completion.  */

#define NN_FLAG_NULL_TRIED                    0x01                  /* Indicates that we already tried a null move here. */
#define NN_FLAG_NULL_IN_PROGRESS              0x02                  /* Indicates that there is currently a null-move child. */
#define NN_FLAG_IS_NULL_CHILD                 0x04                  /* Indicates that this node itself was reached via null move. */

#define NN_FLAG_IS_PV                         0x08                  /* Indicates a PV node. */
#define NN_FLAG_AT_ROOT                       0x10                  /* Indicates a root node. */
#define NN_FLAG_IN_CHECK                      0x20                  /* Indicates that the side to move is in check here (cached). */
                                                                    /* Convenience macros. */
#define NN_SET_FLAG(node, f)    ((node)->flags |=  (f))
#define NN_CLEAR_FLAG(node, f)  ((node)->flags &= ~(f))
#define NN_HAS_FLAG(node, f)   (((node)->flags &   (f)) != 0)

#define NULL_MOVE_BASE_REDUCTION                 2                  /*  */
#define NULL_MOVE_EXTRA_REDUCTION                1                  /*  */

#define MOVE_SORTING_TRANSPO_BEST_MOVE_BONUS 10000                  /* Huge bonus to the move already listed as the best one to make. */
#define MOVE_SORTING_KILLER_MOVE_1_BONUS       800                  /* Sizable bonus for being a (fresh) killer move. */
#define MOVE_SORTING_KILLER_MOVE_2_BONUS       700                  /* Slightly diminished bonus for being a (less fresh) killer move. */

#define MOVEFLAG_QUIET                           0                  /* The move is neither a capture, nor a promotion. */
#define MOVEFLAG_NOISY                           1                  /* The move is a capture, or a promotion (or both). */

/**************************************************************************************************
 Typedefs  */

typedef struct NegamaxNodeType                                      //  TOTAL: 138 = _NEGAMAX_NODE_BYTE_SIZE bytes.
  {
    unsigned char gs[_GAMESTATE_BYTE_SIZE];                         //  (84 bytes) The given gamestate as a byte array.
    unsigned int parent;                                            //  (4 bytes) Index into "negamaxSearchBuffer" of this node's parent.
    unsigned char parentMove[_MOVE_BYTE_SIZE];                      //  (3 bytes) Describe the move that led from "parent" to this node.
    unsigned char bestMove[_MOVE_BYTE_SIZE];                        //  (3 bytes) Best move found so far from this node.

    unsigned int moveOffset;                                        //  (4 bytes) Index (inclusive) into a global move buffer where a chunk of child-moves STARTS.
    unsigned int moveCount;                                         //  (4 bytes) How many moves generated for this node.
    unsigned int moveNextPtr;                                       //  (4 bytes) The index of the MOVE TO TRY NEXT.

    signed char depth;                                              //  (1 byte) Counts down to 0, or to -1 if quiescence searching.
    unsigned char ply;                                              //  (1 byte) Distance from root (starts at 0).

    float originalAlpha;                                            //  (4 bytes) The alpha value saved at the top of the negamax call.
    float alpha;                                                    //  (4 bytes) Upper bound.
    float beta;                                                     //  (4 bytes) Lower bound.
    float value;                                                    //  (4 bytes) Value computed over this node's children and/or eventually returned.

    unsigned long long zhash;                                       //  (8 bytes) Zobrist hash of the given gamestate byte array.
    unsigned int hIndex;                                            //  (4 bytes) Zobrist hash modulo table length, adjusted as necessary using linear probing.

    unsigned char phase;                                            //  (1 byte) In {_PHASE_ENTER_NODE, _PHASE_GEN_AND_ORDER, _PHASE_NEXT_MOVE,
                                                                    //               _PHASE_AFTER_CHILD, _PHASE_FINISH_NODE}.
    unsigned char flags;                                            //  (1 byte) Covers [NN_FLAG_NULL_TRIED, NN_FLAG_NULL_IN_PROGRESS, NN_FLAG_IS_NULL_CHILD,
                                                                    //                   NN_FLAG_IS_PV, NN_FLAG_AT_ROOT, NN_FLAG_IN_CHECK].
  } NegamaxNode;

typedef struct NegamaxMoveType                                      //  TOTAL: 4 = _NEGAMAX_MOVE_BYTE_SIZE bytes.
  {
    unsigned char moveByteArray[_MOVE_BYTE_SIZE];                   //  (3 bytes) Enough bytes to encode a move.
    unsigned char quietMove;                                        //  (1 byte)  (Should be a bool.) "Quiet" moves are neither captures nor promotions.
  } NegamaxMove;

/**************************************************************************************************
 Prototypes  */

 /*  Imported from the Player object's evaluationEngine module.
     This gives you linguistic independence: leave tree-search to C++. */
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Prepare to query the Evaluation Engine by copying the Negamax Engine's
                                                                    //  query-gamestate-buffer contents into the Evaluation Engine's input-gamestate buffer.
__attribute__((import_module("env"), import_name("_copyQuery2EvalGSInput"))) void copyQuery2EvalGSInput();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Prepare to query the Evaluation Engine by copying the Negamax Engine's
                                                                    //  query-move-buffer contents into the Evaluation Engine's input-move buffer.
__attribute__((import_module("env"), import_name("_copyQuery2EvalMoveInput"))) void copyQuery2EvalMoveInput();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Copy the Evaluation Engine's output-gamestate
                                                                    //  to the Negamax Engine's answer-gamestate buffer.
__attribute__((import_module("env"), import_name("_copyEvalOutput2AnswerGSBuffer"))) void copyEvalOutput2AnswerGSBuffer();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Copy the Evaluation Engine's output-moves
                                                                    //  to the Negamax Engine's answer-moves buffer.
                                                                    //  (That is, copy the given number n of byte-chunks to the answer-moves buffer.)
__attribute__((import_module("env"), import_name("_copyEvalOutput2AnswerMovesBuffer"))) void copyEvalOutput2AnswerMovesBuffer(unsigned int);
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  Which side is to move in the GameState encoded in Evaluation Engine's input buffer?
__attribute__((import_module("env"), import_name("_sideToMove"))) unsigned char sideToMove();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  Is the GameState encoded in Evaluation Engine's input buffer terminal?
__attribute__((import_module("env"), import_name("_isTerminal"))) bool isTerminal();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  Is the side to move in the GameState encoded in Evaluation Engine's input buffer in check?
__attribute__((import_module("env"), import_name("_isSideToMoveInCheck"))) bool isSideToMoveInCheck();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  How much non-pawn material does the side to move in the GameState encoded in Evaluation Engine's input buffer have?
__attribute__((import_module("env"), import_name("_nonPawnMaterial"))) unsigned char nonPawnMaterial();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  Make Evaluation Engine write the child-state bytes resulting from the given move
                                                                    //  to the Evaluation Engine's output buffer.
__attribute__((import_module("env"), import_name("_makeMove"))) void makeMove();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  Make Evaluation Engine write the child-state, bytes resulting from a null move
                                                                    //  to the Evaluation Engine's output buffer.
__attribute__((import_module("env"), import_name("_makeNullMove"))) void makeNullMove();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  Evaluate the GameState encoded in Evaluation Engine's input buffer?
__attribute__((import_module("env"), import_name("_evaluate"))) float evaluate();
                                                                    //  Bridge between WebAssembly Modules:
                                                                    //  Query the Evaluation Engine.
                                                                    //  Make Evaluation Engine write a sorted list of (child-state, move) tuples
                                                                    //  to the Evaluation Engine's output buffer.
__attribute__((import_module("env"), import_name("_getMoves"))) unsigned int getMoves();

extern "C"
  {
    unsigned char getMaxPly(void);

    unsigned char* getInputBuffer(void);
    unsigned char* getParametersBuffer(void);
    unsigned char* getOutputBuffer(void);
    unsigned char* getQueryGameStateBuffer(void);
    unsigned char* getQueryMoveBuffer(void);
    unsigned char* getAnswerGameStateBuffer(void);
    unsigned char* getAnswerMovesBuffer(void);
    unsigned char* getZobristHashBuffer(void);
    unsigned char* getTranspositionTableBuffer(void);
    unsigned char* getNegamaxSearchBuffer(void);
    unsigned char* getNegamaxMovesBuffer(void);
    unsigned char* getKillerMovesBuffer(void);
    unsigned char* getHistoryTableBuffer(void);

    void setSearchId(unsigned int);
    unsigned int getSearchId(void);
    unsigned char getStatus(void);
    void setControlFlag(unsigned char);
    void unsetControlFlag(unsigned char);
    unsigned char getControlByte(void);
    void setTargetDepth(unsigned char);
    unsigned char getTargetDepth(void);
    unsigned char getDepthAchieved(void);
    void setDeadline(unsigned int);
    unsigned int getDeadline(void);
    void resetNodesSearched(void);
    unsigned int getNodesSearched(void);
    unsigned char finalDepthAchieved(void);
    float finalScore(void);
    unsigned int getNodeStackSize(void);
    unsigned int getMovesArenaSize(void);

    void initSearch(bool);
    bool negamax(void);
  }

void enterNode_step(unsigned int, NegamaxNode*);
void transpoProbe(unsigned int, NegamaxNode*);
void expansion_step(unsigned int, NegamaxNode*);
void nextMove_step(unsigned int, NegamaxNode*);
void afterChild_step(unsigned int, NegamaxNode*);
void finishNode_step(unsigned int, NegamaxNode*);

void quicksort(bool, signed int*, NegamaxMove*, unsigned int, unsigned int);
unsigned int partition(bool, signed int*, NegamaxMove*, unsigned int, unsigned int);

unsigned int restoreNegamaxSearchBufferLength(void);
unsigned int restoreNegamaxMoveBufferLength(void);
void saveNegamaxSearchBufferLength(unsigned int);
void saveNegamaxMoveBufferLength(unsigned int);
void restoreNode(unsigned int, NegamaxNode* );
void saveNode(NegamaxNode*, unsigned int);
void restoreMove(unsigned int, NegamaxMove*);
void saveMove(NegamaxMove*, unsigned int);

unsigned long long hash(unsigned char*);

unsigned char killerLookup(unsigned char, unsigned char*);
void killerAdd(unsigned char, unsigned char*);

unsigned int historyLookup(unsigned char, unsigned char*);
void historyUpdate(unsigned char, unsigned char, unsigned char*);

void incrementNodeCtr(void);

/**************************************************************************************************
 Globals  */
                                                                    //  84 bytes.
                                                                    //  Global array containing the serialized game state:
unsigned char inputGameStateBuffer[_GAMESTATE_BYTE_SIZE];           //  Input from Player.js to its negamaxEngine.

                                                                    //  16 bytes.
                                                                    //  Search ID:      4 bytes.
                                                                    //  Status:         1 byte.
                                                                    //  Control Flags:  1 byte.
                                                                    //  Target Depth:   1 byte.
                                                                    //  Depth Reached:  1 byte.
                                                                    //  Deadline in ms: 4 bytes.
unsigned char inputParametersBuffer[_PARAMETER_ARRAY_SIZE];         //  Nodes Searched: 4 bytes.

                                                                    //  92 bytes.
                                                                    //  Global array containing: {serialized game state (sanity check),
                                                                    //                            1-byte uchar          (depth achieved),
                                                                    //                            serialized move       (move to make in this state),
                                                                    //                            4-byte float          (score)                      }
                                                                    //  Output from negamaxEngine to Player.js.
unsigned char outputBuffer[_GAMESTATE_BYTE_SIZE + 1 + _MOVE_BYTE_SIZE + 4];

                                                                    //  84 bytes.
                                                                    //  Global array containing the serialized (query) game state:
unsigned char queryGameStateBuffer[_GAMESTATE_BYTE_SIZE];           //  Input from negamaxEngine to evaluationEngine.

                                                                    //  3 bytes.
                                                                    //  Global array containing the serialized (query) move:
unsigned char queryMoveBuffer[_MOVE_BYTE_SIZE];                     //  Input from negamaxEngine to evaluationEngine.

                                                                    //  84 bytes.
                                                                    //  Global array containing a serialized (answer) game state:
unsigned char answerGameStateBuffer[_GAMESTATE_BYTE_SIZE];          //  Output from evaluationEngine to negamaxEngine.

                                                                    //  4096 bytes.
                                                                    //  Global array containing: {serialized (answer-move, rough score, quiet-flag)}:
                                                                    //  Output from evaluationEngine to negamaxEngine.
unsigned char answerMovesBuffer[_MAX_MOVES * (_MOVE_BYTE_SIZE + 5)];//  The actual number of moves is the unsigned char returned by this function.

                                                                    //  10,056 bytes.
                                                                    //  For "zobristHashBuffer" included in "zobrist.h".

                                                                    //  9,437,185 bytes.
                                                                    //  For "transpositionTableBuffer" included in "transposition.h".

                                                                    //  9,043,972 bytes.
                                                                    //  Flat, global array that behaves like a DFS stack for negamax nodes.
                                                                    //  First four bytes are for an unsigned int: the length of the array.
unsigned char negamaxSearchBuffer[4 + _TREE_SEARCH_ARRAY_SIZE * _NEGAMAX_NODE_BYTE_SIZE];

                                                                    //  262,148 bytes.
                                                                    //  Flat, global array that accumulates all moves for all nodes.
                                                                    //  First four bytes are for an unsigned int: the length of the array.
unsigned char negamaxMovesBuffer[4 + _TREE_SEARCH_ARRAY_SIZE * _NEGAMAX_MOVE_BYTE_SIZE];

                                                                    //  256 bytes.
                                                                    //  Each entry is [from_1, to_1, from_2, to_2].
                                                                    //  Number per ply, times 2 bytes per move, times max depth.
                                                                    //  (By definition, killer moves are "quiet"--neither captures nor promotions.
                                                                    //   therefore, no need to store promotion fields we won't use.)
                                                                    //  This buffer is arranged as:
                                                                    //  [ (ply-0 from, ply-0 to)         (ply-0 from, ply-0 to),
                                                                    //    (ply-1 from, ply-1 to)         (ply-1 from, ply-1 to),
                                                                    //    (ply-2 from, ply-2 to)         (ply-2 from, ply-2 to),
                                                                    //                               . . .
                                                                    //    (ply-MAX-1 from, ply-MAX-1 to) (ply-MAX-1 from, ply-MAX-1 to)  ]
unsigned char killerMovesTableBuffer[_KILLER_MOVE_PER_PLY * 2 * _KILLER_MOVE_MAX_DEPTH];

                                                                    //  12,800 bytes.
                                                                    //  2 is for 2 teams, white and black.
                                                                    //  Note that we don't care about promotion choices here; just bump up moves (from, to).
                                                                    //  This buffer is arranged as:
                                                                    //  [ White to move, From-index 0,  To-indices 0 .. 79,
                                                                    //                   From-index 1,  To-indices 0 .. 79,
                                                                    //                   From-index 2,  To-indices 0 .. 79,
                                                                    //                                 . . .
                                                                    //                   From-index 79, To-indices 0 .. 79,
                                                                    //    Black to move, From-index 0,  To-indices 0 .. 79,
                                                                    //                   From-index 1,  To-indices 0 .. 79,
                                                                    //                   From-index 2,  To-indices 0 .. 79,
                                                                    //                                 . . .
unsigned char historyTableBuffer[2 * _NONE * _NONE];                //                   From-index 79, To-indices 0 .. 79  ]

                                                                    //  SUBTOTAL:  18,770,876 bytes.
                                                                    //  Give the stack 1,048,576 bytes.
                                                                    //  TOTAL:     19,819,452 bytes.
                                                                    //  Round to:  19,857,408 = 303 pages (cover units of 65,536).

/**************************************************************************************************
 Maximum ply.  */

unsigned char getMaxPly(void)
  {
    return _MAX_PLY;
  }

/**************************************************************************************************
 Pointer-retrieval functions  */

/* Expose the global array declared here to JavaScript.
   Player.js writes bytes arrays here and then calls its "negamaxEngine". */
unsigned char* getInputBuffer(void)
  {
    return &inputGameStateBuffer[0];
  }

/* Expose the global array declared here to JavaScript.
   Player.js reads and writes byte arrays here. */
unsigned char* getParametersBuffer(void)
  {
    return &inputParametersBuffer[0];
  }

/* Expose the global array declared here to JavaScript.
   "negamaxEngine" writes its output here for Player.js to pick up: {gamestate as byte array, unsigned int as byte array, move as byte array}.
   This stands for {child state, depth searched, move to play in reply}. */
unsigned char* getOutputBuffer(void)
  {
    return &outputBuffer[0];
  }

/* Expose the global array declared here to JavaScript (just so we can address it).
   Player's "negamaxEngine" writes a game states as a byte array here and then calls Player.js's "evaluationEngine". */
unsigned char* getQueryGameStateBuffer(void)
  {
    return &queryGameStateBuffer[0];
  }

/* Expose the global array declared here to JavaScript (just so we can address it).
   Player's "negamaxEngine" writes a move as a byte array here and then calls Player.js's "evaluationEngine". */
unsigned char* getQueryMoveBuffer(void)
  {
    return &queryMoveBuffer[0];
  }

/* Expose the global array decalred here to JavaScript (just so we can address it). */
unsigned char* getAnswerGameStateBuffer(void)
  {
    return &answerGameStateBuffer[0];
  }

/* Expose the global array decalred here to JavaScript (just so we can address it). */
unsigned char* getAnswerMovesBuffer(void)
  {
    return &answerMovesBuffer[0];
  }

/* Expose the global array declared here to JavaScript (just so we can fill it).
   Filled once and read-only throughout a match. */
unsigned char* getZobristHashBuffer(void)
  {
    return &zobristHashBuffer[0];                                   //  Defined in zobrist.h.
  }

/* Expose the global array declared in "transposition.h" to JavaScript (just so we can address it). */
unsigned char* getTranspositionTableBuffer(void)
  {
    return &transpositionTableBuffer[0];
  }

/* Expose the global array decalred here to JavaScript (just so we can address it). */
unsigned char* getNegamaxSearchBuffer(void)
  {
    return &negamaxSearchBuffer[0];
  }

/* Expose the global array decalred here to JavaScript (just so we can address it). */
unsigned char* getNegamaxMovesBuffer(void)
  {
    return &negamaxMovesBuffer[0];
  }

/* Expose the global array decalred here to JavaScript (just so we can address it). */
unsigned char* getKillerMovesBuffer(void)
  {
    return &killerMovesTableBuffer[0];
  }

/* Expose the global array decalred here to JavaScript (just so we can address it). */
unsigned char* getHistoryTableBuffer(void)
  {
    return &historyTableBuffer[0];
  }

/**************************************************************************************************
 Search parameter functions  */

/* Set the search ID. */
void setSearchId(unsigned int searchId)
  {
    unsigned char buffer4[4];
    unsigned char i;

    memcpy(buffer4, (unsigned char*)(&searchId), 4);                //  Force the unsigned int into a 4-byte temp buffer.
    for(i = 0; i < 4; i++)                                          //  Copy bytes to parameters buffer.
      inputParametersBuffer[PARAM_BUFFER_SEARCHID_OFFSET + i] = buffer4[i];

    return;
  }

/* Retrieve the search ID from the parameters buffer. */
unsigned int getSearchId(void)
  {
    unsigned int searchId;
    unsigned char buffer4[4];
    unsigned char i;

    for(i = 0; i < 4; i++)
      buffer4[i] = inputParametersBuffer[PARAM_BUFFER_SEARCHID_OFFSET + i];

    memcpy(&searchId, buffer4, 4);                                  //  Force the 4-byte buffer into an unsigned int.

    return searchId;
  }

/* Retrieve the search status from the parameters buffer. */
unsigned char getStatus(void)
  {
    return inputParametersBuffer[PARAM_BUFFER_STATUS_OFFSET];
  }

/* Set a bit (or bits) in the command byte. */
void setControlFlag(unsigned char flag)
  {
    inputParametersBuffer[PARAM_BUFFER_COMMAND_OFFSET] |= flag;
    return;
  }

/* Unset a bit (or bits) in the command byte. */
void unsetControlFlag(unsigned char flag)
  {
    inputParametersBuffer[PARAM_BUFFER_COMMAND_OFFSET] &= ~(flag);
    return;
  }

/* Retrieve the command byte. */
unsigned char getControlByte(void)
  {
    return inputParametersBuffer[PARAM_BUFFER_COMMAND_OFFSET];
  }

/* Set the target depth. */
void setTargetDepth(unsigned char depth)
  {
    inputParametersBuffer[PARAM_BUFFER_TARGETDEPTH_OFFSET] = depth;
    return;
  }

/* Retrieve the target depth. */
unsigned char getTargetDepth(void)
  {
    return inputParametersBuffer[PARAM_BUFFER_TARGETDEPTH_OFFSET];
  }

/* Retrieve the depth achieved by search (so far). */
unsigned char getDepthAchieved(void)
  {
    return inputParametersBuffer[PARAM_BUFFER_DEPTHACHIEVED_OFFSET];
  }

/* Set the search deadline in milliseconds. */
void setDeadline(unsigned int ms)
  {
    unsigned char buffer4[4];
    unsigned char i;

    memcpy(buffer4, (unsigned char*)(&ms), 4);                      //  Force the unsigned int into a 4-byte temp buffer.
    for(i = 0; i < 4; i++)                                          //  Copy bytes to parameters buffer.
      inputParametersBuffer[PARAM_BUFFER_DEADLINE_OFFSET + i] = buffer4[i];

    return;
  }

/* Retrieve the search deadline in milliseconds. */
unsigned int getDeadline(void)
  {
    unsigned int ms;
    unsigned char buffer4[4];
    unsigned char i;

    for(i = 0; i < 4; i++)
      buffer4[i] = inputParametersBuffer[PARAM_BUFFER_DEADLINE_OFFSET + i];

    memcpy(&ms, buffer4, 4);                                        //  Force the 4-byte buffer into an unsigned int.

    return ms;
  }

/* Reset the number of nodes searched to zero. */
void resetNodesSearched(void)
  {
    unsigned int ctr;
    unsigned char buffer4[4];
    unsigned char i;

    ctr = 0;

    memcpy(buffer4, (unsigned char*)(&ctr), 4);                     //  Force the unsigned int into a 4-byte temp buffer.
    for(i = 0; i < 4; i++)                                          //  Copy bytes to parameters buffer.
      inputParametersBuffer[PARAM_BUFFER_NODESSEARCHED_OFFSET + i] = buffer4[i];

    return;
  }

/* Retrieve the number of nodes searched. */
unsigned int getNodesSearched(void)
  {
    unsigned int ctr;
    unsigned char buffer4[4];
    unsigned char i;

    for(i = 0; i < 4; i++)
      buffer4[i] = inputParametersBuffer[PARAM_BUFFER_NODESSEARCHED_OFFSET + i];

    memcpy(&ctr, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.

    return ctr;
  }

/* Read the depth achieved from "outputBuffer" and return it as a value. */
unsigned char finalDepthAchieved(void)
  {
    return outputBuffer[_GAMESTATE_BYTE_SIZE];
  }

/* Read the score from "outputBuffer" and return it as a value. */
float finalScore(void)
  {
    float score;
    unsigned char buffer4[4];
    unsigned char i;

    for(i = 0; i < 4; i++)
      buffer4[i] = outputBuffer[_GAMESTATE_BYTE_SIZE + 1 + _MOVE_BYTE_SIZE + i];

    memcpy(&score, buffer4, 4);                                     //  Force the 4-byte buffer into a float.

    return score;
  }

/* (Diagnostic) */
unsigned int getNodeStackSize(void)
  {
    return restoreNegamaxSearchBufferLength();
  }

/* (Diagnostic) */
unsigned int getMovesArenaSize(void)
  {
    return restoreNegamaxMoveBufferLength();
  }

/**************************************************************************************************
 Negamax-search functions  */

/* Initialize the root node for (interrupatble) negamax search. */
void initSearch(bool initializeForWhite)
  {
    NegamaxNode root;
    unsigned char depth;
    unsigned int i;
                                                                    //  Set status to RUNNING.
    inputParametersBuffer[PARAM_BUFFER_STATUS_OFFSET] = STATUS_RUNNING;

    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                       //  Copy root gamestate byte array from global "inputGameStateBuffer"
      root.gs[i] = inputGameStateBuffer[i];                         //  to negamax root node.

    root.parent = 0;                                                //  Set root's parent index to...

    root.parentMove[0] = _NONE;                                     //  Set the root's parent-move to a blank-move.
    root.parentMove[1] = _NONE;
    root.parentMove[2] = 0;

    root.bestMove[0] = _NONE;                                       //  Set the root's best-move to a blank-move.
    root.bestMove[1] = _NONE;
    root.bestMove[2] = 0;

    root.moveOffset = 0;                                            //  Set offset into moves buffer for the children of root.
    root.moveCount = 0;                                             //  Set the number of children root has.
    root.moveNextPtr = 0;                                           //  Set the index to which root's children iterator currently points.
                                                                    //  Retrieve target-depth parameter and clamp minimum to 1.
    depth = inputParametersBuffer[PARAM_BUFFER_TARGETDEPTH_OFFSET] > 0 ? inputParametersBuffer[PARAM_BUFFER_TARGETDEPTH_OFFSET] : 1;
    root.depth = (signed char)depth;                                //  Set root's depth to "depth".
    root.ply = 0;                                                   //  Set root's ply to zero.

    root.originalAlpha = -std::numeric_limits<float>::infinity();   //  Initialize root's originalAlpha.
    root.alpha = -std::numeric_limits<float>::infinity();           //  Initialize root's alpha.
    root.beta = std::numeric_limits<float>::infinity();             //  Initialize root's beta.
    root.value = -std::numeric_limits<float>::infinity();           //  Initialize root's value.

    root.zhash = 0L;                                                //  Initialize root's zhash.
    root.hIndex = 0;                                                //  Initialize root's hIndex.

    root.phase = _PHASE_ENTER_NODE;                                 //  Indicate that no work has been done on this node yet:
                                                                    //  the first thing to try is a transposition table lookup.
    root.flags = NN_FLAG_AT_ROOT;                                   //  Set root flag.

    saveNegamaxSearchBufferLength(1);                               //  Write number of NegamaxNodes in "negamaxSearchBuffer".
    saveNode(&root, 0);                                             //  Write serialized node to head of "negamaxSearchBuffer".

    return;
  }

/* HEARTBEAT NEGAMAX

   Depth-first search for a two-player, perfect-information, zero-sum game.
   Uses alpha-beta pruning and a transposition table.

   So that tree search does not overwhelm the client-side CPU, negamax must be redesigned in a "heartbeat" manner. */
bool negamax(void)
  {
    unsigned int gsIndex;
    NegamaxNode node;

    unsigned char controlFlags = inputParametersBuffer[PARAM_BUFFER_COMMAND_OFFSET];

    unsigned char buffer4[4];
    unsigned int i, j;

    //////////////////////////////////////////////////////////////////  Is search halting?
    if(controlFlags & CTRL_HARD_ABORT)                              //  Bail out immediately.
      {
                                                                    //  Set status to aborted.
        inputParametersBuffer[PARAM_BUFFER_STATUS_OFFSET] = STATUS_ABORTED;
        return true;
      }

    //////////////////////////////////////////////////////////////////  Proceed.
    gsIndex = restoreNegamaxSearchBufferLength() - 1;               //  Index for top of stack is length minus one.
    restoreNode(gsIndex, &node);                                    //  Restore the node at the top of the stack.

    if(controlFlags & CTRL_STOP_REQUESTED)                          //  Proceed only if we are wrapping up node work.
      {
        if(node.phase == _PHASE_AFTER_CHILD || node.phase == _PHASE_FINISH_NODE || node.phase == _PHASE_COMPLETE)
          {
                                                                    //  Set status to aborted.
            inputParametersBuffer[PARAM_BUFFER_STATUS_OFFSET] = STATUS_ABORTED;
            return true;
          }
        else
          inputParametersBuffer[PARAM_BUFFER_STATUS_OFFSET] = STATUS_STOP_REQUESTED;
      }

    switch(node.phase)
      {
        //////////////////////////////////////////////////////////////  First operations with a node.
        case _PHASE_ENTER_NODE:                                     //  - Compute the hash for this node.
                                                                    //  - Test whether the given game state is terminal.
                                                                    //  - Probe the transposition table: can we find a value or adjust bounds?
                                                                    //  - Attempt null-move pruning.
          enterNode_step(gsIndex, &node);
          break;

        //////////////////////////////////////////////////////////////  Generate moves/children.
        case _PHASE_GEN_AND_ORDER:                                  //  - Get moves for the given game state (scored fast-n-cheap by Evaluation Module).
                                                                    //  - Update those scores using information from the Negamax Module.
                                                                    //  - Sort moves by these scores.
                                                                    //  - Append sorted (best-first) moves to "negamaxMoveBuffer".
          expansion_step(gsIndex, &node);
          break;

        //////////////////////////////////////////////////////////////  Whichever child-move is next to try for the top node,
                                                                    //  apply it to the game state and make a new node out of it.
        case _PHASE_NEXT_MOVE:                                      //  - Check whether all moves of the given node have been searched already.
                                                                    //  - If not, apply the next move to the node's game state, get a new game state.
                                                                    //  - Make a new node out of this new game state and push it to the node stack.
          nextMove_step(gsIndex, &node);
          break;

        //////////////////////////////////////////////////////////////  Incorporate the node's data into its parent.
        case _PHASE_AFTER_CHILD:                                    //  - Update the node's parent's value.
                                                                    //  - Test for cutoffs.
                                                                    //  - Update killers and history heuristic.
          afterChild_step(gsIndex, &node);
          break;

        //////////////////////////////////////////////////////////////  Write to the Transpo Table.
        case _PHASE_FINISH_NODE:
          finishNode_step(gsIndex, &node);
          break;

        //////////////////////////////////////////////////////////////  Write to outputBuffer and signal search completion.
        case _PHASE_COMPLETE:
          i = 0;
          for(j = 0; j < _GAMESTATE_BYTE_SIZE; j++)                 //  Copy game state to output buffer.
            outputBuffer[i++] = node.gs[j];
                                                                    //  When the root completes, the depth achieved equals the depth requested.
          inputParametersBuffer[PARAM_BUFFER_DEPTHACHIEVED_OFFSET] = inputParametersBuffer[PARAM_BUFFER_TARGETDEPTH_OFFSET];
                                                                    //  Copy depth to output buffer.
          outputBuffer[i++] = inputParametersBuffer[PARAM_BUFFER_DEPTHACHIEVED_OFFSET];

          for(j = 0; j < _MOVE_BYTE_SIZE; j++)                      //  Copy best move for this state, as determined by depth, to output buffer.
            outputBuffer[i++] = node.bestMove[j];

          memcpy(buffer4, (unsigned char*)(&node.value), 4);        //  Force the float into a 4-byte temp buffer.
          for(j = 0; j < 4; j++)                                    //  Copy bytes to output buffer.
            outputBuffer[i++] = buffer4[j];

                                                                    //  Indicate that search is DONE.
          inputParametersBuffer[PARAM_BUFFER_STATUS_OFFSET] = STATUS_DONE;

          break;
      }

    return (node.phase == _PHASE_COMPLETE);                         //  True: search is complete; False: search is ongoing.
  }

/* HEARTBEAT NEGAMAX: _PHASE_ENTER_NODE
   Hash this node and check the transposition table. Test for null-move pruning. */
void enterNode_step(unsigned int gsIndex, NegamaxNode* node)
  {
    unsigned char gamestateByteArray[_GAMESTATE_BYTE_SIZE];         //  Store locally for comparison.
    unsigned int negamaxSearchBufferLength;
    unsigned char material;
    signed char R, newDepth;
    NegamaxNode child;
    float standPat;                                                 //  Score at a given moment in search.
    unsigned int i, j;
    bool b_isTerminal, b_isSideToMoveInCheck;

    //////////////////////////////////////////////////////////////////  Compute the hash for this node.
    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                       //  Copy unique byte-signature for the current game state to a local buffer.
      gamestateByteArray[i] = node->gs[i];

    node->zhash = hash(gamestateByteArray);                         //  Zobrist-hash the game state byte array.
    node->hIndex = hashIndex(node->zhash);                          //  Index modulo size of transposition table.

    //////////////////////////////////////////////////////////////////  Transposition-table probe.
    transpoProbe(gsIndex, node);                                    //  Check the transpo table.
    if(node->phase == _PHASE_FINISH_NODE)                           //  Cut-off produced: we're done here.
      return;

    //////////////////////////////////////////////////////////////////  Terminal test.
    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                       //  Copy "node"s "gs" to "queryGameStateBuffer"
      queryGameStateBuffer[i] = node->gs[i];                        //  for isTerminal() and evaluate().
    copyQuery2EvalGSInput();                                        //  Copy "queryGameStateBuffer" to Evaluation Module's "inputBuffer".

    b_isTerminal = isTerminal();                                    //  (Ask the Evaluation Module) Is the given game state terminal?
    if(b_isTerminal)                                                //  - Terminal-state check.
      {
        node->value = evaluate();                                   //  This imported function handles testing the AI's side.
        node->phase = _PHASE_FINISH_NODE;                           //  Mark for the finishing phase.
        incrementNodeCtr();                                         //  Increase node-evaluation counter by 1.
        saveNode(node, gsIndex);                                    //  Save the updated node.
        return;
      }

    //////////////////////////////////////////////////////////////////  Check whether the side to move is in check.
    b_isSideToMoveInCheck = isSideToMoveInCheck();                  //  (Ask the Evaluation Module) Is the side to move in the given game state in check?
    if(b_isSideToMoveInCheck)
      NN_SET_FLAG(node, NN_FLAG_IN_CHECK);
    else
      NN_CLEAR_FLAG(node, NN_FLAG_IN_CHECK);

    //////////////////////////////////////////////////////////////////  Leaf-node test.
    if(node->depth <= 0)
      {
        if(node->depth <= -(signed char)_QUIESCENCE_MAX_PLY)        //  Prevent runaway quiescence search extensions.
          {
            node->value = evaluate();
            node->phase = _PHASE_FINISH_NODE;
            incrementNodeCtr();
            saveNode(node, gsIndex);
            return;
          }

        if(!NN_HAS_FLAG(node, NN_FLAG_IN_CHECK))
          {
            standPat = evaluate();                                  //  Evaluation as if this were the end of search.
            incrementNodeCtr();                                     //  That evaluation counts.

            if(standPat >= node->beta)                              //  Stand-pat cutoff.
              {
                node->value = node->beta;                           //  Standard quiescence search returns beta on cutoff.
                node->phase = _PHASE_FINISH_NODE;
                saveNode(node, gsIndex);
                return;
              }
            if(standPat > node->alpha)                              //  Improve alpha using stand-pat.
              node->alpha = standPat;
            node->value = standPat;                                 //  Baseline best score is stand-pat (since "do nothing" is allowed when not in check).
          }
        else                                                        //  In check: standing pat is NOT allowed.
          {
            node->value = -std::numeric_limits<float>::infinity();
            node->bestMove[0] = _NONE;
            node->bestMove[1] = _NONE;
            node->bestMove[2] = _NO_PROMO;
          }

        node->phase = _PHASE_GEN_AND_ORDER;                         //  Now search noisy replies (captures/promos), or evasions if in check.
        saveNode(node, gsIndex);
        return;
      }

    //////////////////////////////////////////////////////////////////  Count up non-pawn material.
    material = nonPawnMaterial();

    //////////////////////////////////////////////////////////////////  Attempt null-move pruning.
                                                                    //  Conditions are correct to try null-move pruning.
    if( !NN_HAS_FLAG(node, NN_FLAG_NULL_TRIED) && !NN_HAS_FLAG(node, NN_FLAG_IS_NULL_CHILD) &&
        !NN_HAS_FLAG(node, NN_FLAG_AT_ROOT)    && !NN_HAS_FLAG(node, NN_FLAG_IS_PV)         &&
        !NN_HAS_FLAG(node, NN_FLAG_IN_CHECK)   && node->depth >= 3 && material > 3           )
      {
                                                                    //  - Null-move pruning.
        NN_SET_FLAG(node, NN_FLAG_NULL_TRIED);                      //  Indicate that we did try the null move for this node.
        NN_SET_FLAG(node, NN_FLAG_NULL_IN_PROGRESS);                //  Indicate that this node's null-child is (WILL BE) on the stack.
        R = NULL_MOVE_BASE_REDUCTION;
        if(node->depth >= 6)
          R += NULL_MOVE_EXTRA_REDUCTION;
        newDepth = node->depth - 1 - R;
        if(newDepth < 0)
          newDepth = 0;

        makeNullMove();                                             //  (Ask the Evaluation Module) To make the null-move.
        copyEvalOutput2AnswerGSBuffer();                            //  Copy Evaluation Module's post-null-move game state to Negamax's GS answer buffer.
                                                                    //  Save the current length, before addition of child nodes.
        negamaxSearchBufferLength = restoreNegamaxSearchBufferLength();

        i = 0;
        for(j = 0; j < _GAMESTATE_BYTE_SIZE; j++)                   //  Copy child gamestate byte-array to child node.
          child.gs[j] = answerGameStateBuffer[i++];
        child.parent = gsIndex;                                     //  Set child's parent to the given index into "negamaxSearchBuffer".

        child.parentMove[0] = _NONE;                                //  Set child's parent-move to the null move.
        child.parentMove[1] = _NONE;
        child.parentMove[2] = 0;

        child.bestMove[0] = _NONE;                                  //  Set child's best-move to a blank move.
        child.bestMove[1] = _NONE;
        child.bestMove[2] = 0;

        child.moveOffset = 0;                                       //  Set child's offset to zero.
        child.moveCount = 0;                                        //  Set child's number of moves to zero.
        child.moveNextPtr = 0;                                      //  Set child's move-to-try-next pointer to zero.
        child.depth = newDepth;                                     //  Set child's depth to "newDepth".
        child.ply = node->ply + 1;                                  //  Set child's ply to node's plus one.

        child.originalAlpha = -node->beta;                          //  Set child's alpha to negative parent's beta.
        child.alpha = -node->beta;
        child.beta = -node->alpha;                                  //  Set child's beta to negative parent's alpha.
        child.zhash = 0L;
        child.hIndex = 0;
        child.phase = _PHASE_ENTER_NODE;                            //  Set child's phase.
        child.flags = 0;
        NN_SET_FLAG(&child, NN_FLAG_IS_NULL_CHILD);                 //  Set the flag that says this is a null-move child.
        child.value = -std::numeric_limits<float>::infinity();

        node->phase = _PHASE_AFTER_CHILD;
        node->moveNextPtr++;

        saveNode(&child, negamaxSearchBufferLength);                //  Write serialized node to head of "negamaxSearchBuffer".
        negamaxSearchBufferLength++;                                //  Increment "negamaxSearchBufferLength".
        saveNegamaxSearchBufferLength(negamaxSearchBufferLength);   //  Write number of NegamaxNodes in "negamaxSearchBuffer".
        saveNode(node, gsIndex);                                    //  Save the updated node.
      }
    else                                                            //  Conditions are insufficient to try null-move pruning.
      {
        node->phase = _PHASE_GEN_AND_ORDER;                         //  Mark this node as ready for the _PHASE_GEN_AND_ORDER phase.
        saveNode(node, gsIndex);                                    //  Save the updated node.
      }

    return;
  }

/* Attempt to look up the given game state in the transposition table.
   This step will either find the transposition or it won't. */
void transpoProbe(unsigned int gsIndex, NegamaxNode* node)
  {
    TranspoRecord ttRecord;
    bool foundTTLookup;
    unsigned int original_hIndex;                                   //  For the unlikely case in which we fill the table and search all the way around.
    unsigned char req;                                              //  Required depth.
    unsigned int i;

    node->originalAlpha = node->alpha;                              //  Save alpha as given.
    original_hIndex = node->hIndex;                                 //  Save the modulo-index to where this entry WANTS to go.

    //  LINEAR PROBING:
    //  We first want to see whether the given game state has already been searched.
    //  So, starting at "hIndex", probe extant TT records until the game-state byte-arrays match.
    //  If you encounter a free space, stop and give up: we have not found a pre-searched state.
    foundTTLookup = false;
    while(!foundTTLookup)
      {
        if(!fetchRecord(node->hIndex, &ttRecord))                   //  We've hit a blank: this state has not been found.
          break;
        else                                                        //  We've hit an occupied record--but is it the one we're looking for?
          {
            if(ttRecord.lock == node->zhash)                        //  Zobrist key fits transpo-table entry's lock: call this a HIT!
              foundTTLookup = true;
            else
              {
                if(node->hIndex == (_TRANSPO_TABLE_SIZE - 1))       //  Wrap around.
                  node->hIndex = 0;
                else
                  node->hIndex++;
                if(node->hIndex == original_hIndex)                 //  If we've come back around to where we started, then just quit: state is unfound.
                  break;
              }
          }
      }
                                                                    //  Attempt to look up the given game state (under Zobrist hash) in the transposition table.
    if(foundTTLookup)                                               //  HIT! Found this same game state.
      {
        ttRecord.age = (ttRecord.age > 1) ? ttRecord.age - 1 : 1;   //  This record was useful: decrease its age.
                                                                    //  Save this updated (rejuvenated) record back to the byte array.
        serializeTranspoRecord(&ttRecord, transpositionTableBuffer + 1 + node->hIndex * _TRANSPO_RECORD_BYTE_SIZE);
                                                                    //  Even if it turns out that this is not a cut-off, the best move stored here
                                                                    //  may still be a hint.
        if(ttRecord.bestMove[0] < _NONE && ttRecord.bestMove[1] < _NONE)
          {
            for(i = 0; i < _MOVE_BYTE_SIZE; i++)                    //  Copy from TT record to Node's best move.
              node->bestMove[i] = ttRecord.bestMove[i];
            saveNode(node, gsIndex);
          }

        req = (node->depth > 0) ? node->depth : 0;                  //  Important because node.depth can go negative in quiescence-search.

        if(ttRecord.depth >= req)                                   //  The record has been ratified from a greater depth.
          {
            if(ttRecord.type == NODE_TYPE_PV)                       //  Exact.
              {
                node->value = ttRecord.score;                       //  This node's return value furnished by the transpo lookup.
                node->phase = _PHASE_FINISH_NODE;                   //  Set node's phase to Parent-Update.
                saveNode(node, gsIndex);                            //  Save the updated node.
                return;
              }
            else if(ttRecord.type == NODE_TYPE_CUT)                 //  Lower Bound.
              node->alpha = std::max(node->alpha, ttRecord.score);
            else if(ttRecord.type == NODE_TYPE_ALL)                 //  Upper Bound.
              node->beta = std::min(node->beta, ttRecord.score);

            if(node->alpha >= node->beta)
              {
                if(ttRecord.type == NODE_TYPE_CUT)                  //  Lower bound caused fail-high.
                  node->value = node->beta;
                else                                                //  Upper bound caused fail-low.
                  node->value = node->alpha;

                node->phase = _PHASE_FINISH_NODE;                   //  Set node's phase to Parent-Update.
                saveNode(node, gsIndex);                            //  Save the updated node.
                return;
              }
          }
      }

    return;
  }

/* HEARTBEAT NEGAMAX: _PHASE_GEN_AND_ORDER
   Generate the moves available from the given node and append them to the arena. */
void expansion_step(unsigned int gsIndex, NegamaxNode* node)
  {
    unsigned int negamaxMoveBufferLength;                           //  Size of the MOVE stack.
    unsigned int numMoves;

    NegamaxMove movesBuffer[_MAX_MOVES];                            //  Local storage to be filled, sorted, then appended to "negamaxMovesBuffer".
    signed int scores[_MAX_MOVES];                                  //  Scores furnished by both Evaluation Module (SEE, promotion, check)
                                                                    //  and by Negamax Module (TT best move, killer move, history heuristic).
    NegamaxMove move;
    signed int score;

    unsigned char killerFlag;

    unsigned char buffer4[4];                                       //  Convert 4-byte arrays to their proper data types.
    signed int si4;
    unsigned char toMove;

    unsigned int outCount;
    bool isQ;
    bool inCheck;
    unsigned char ttHint[_MOVE_BYTE_SIZE];

    unsigned int i, j, answerBufferCtr;

    negamaxMoveBufferLength = restoreNegamaxMoveBufferLength();     //  Save the current length of the MOVE stack, before addition of moves.

    isQ = (node->depth <= 0);                                       //  Is the given node a quiescence-search extension?
    inCheck = NN_HAS_FLAG(node, NN_FLAG_IN_CHECK);                  //  Is the given node in check?

    for(j = 0; j < _MOVE_BYTE_SIZE; j++)                            //
      ttHint[j] = node->bestMove[j];

    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                       //  Copy "node"s "gs" to "queryGameStateBuffer" for getSortedMoves().
      queryGameStateBuffer[i] = node->gs[i];

    copyQuery2EvalGSInput();                                        //  Copy Negamax Module's "queryGameStateBuffer" to Evaluation Module's "inputBuffer".

    toMove = sideToMove();                                          //  (Ask the Evaluation Module) Which side is to move?
    numMoves = getMoves();                                          //  (Ask the Evaluation Module) Generate SEE-scored list of moves.
                                                                    //  (These are scored, quick-n-cheap, BUT NOT SORTED.)

                                                                    //  Only reset value/bestMove for NORMAL (not quiescence-extension) nodes.
    if(!isQ)                                                        //  In quiescence search, we already set node->value to standPat
      {                                                             //  (or -inf in check) in enterNode_step.
        node->value = -std::numeric_limits<float>::infinity();      //  Cause the first legal move to be the best found so far.
        node->bestMove[0] = _NONE;
        node->bestMove[1] = _NONE;
        node->bestMove[2] = _NO_PROMO;
      }

    copyEvalOutput2AnswerMovesBuffer( numMoves );                   //  Copy from Evaluation Module's output buffer to Negamax Module's "answerMovesBuffer".

    answerBufferCtr = 0;                                            //  Convert each run of (_GAMESTATE_BYTE_SIZE + _MOVE_BYTE_SIZE) bytes in "answerMovesBuffer"
    outCount = 0;
    for(i = 0; i < numMoves; i++)                                   //  to a NegamaxNode and append them to "negamaxSearchBuffer".
      {
        for(j = 0; j < _MOVE_BYTE_SIZE; j++)                        //  Retrieve the bytes that define the move.
          move.moveByteArray[j] = answerMovesBuffer[answerBufferCtr++];

        for(j = 0; j < 4; j++)                                      //  Copy the 4 bytes of the move's SEE score from the global buffer.
          buffer4[j] = answerMovesBuffer[answerBufferCtr++];
        memcpy(&si4, buffer4, 4);                                   //  Force the 4-byte buffer into a SIGNED int.
        score = si4;                                                //  Save the rough, SEE score to a local array.

        move.quietMove = answerMovesBuffer[answerBufferCtr++];      //  0: quiet; 1: capture or promotion.

        if(isQ && !inCheck)                                         //  Quiescence filtering:
          {                                                         //  - if in check: keep ALL legal moves (evasions)
                                                                    //  - else: keep only noisy moves (captures/promotions)
            if(move.quietMove == MOVEFLAG_QUIET)                    //  Quiet ==> Skip in quiescence search.
              continue;
          }
                                                                    //  TT best-move bump (use ttHint because node->bestMove may have been cleared).
        if(ttHint[0] != _NONE && ttHint[1] != _NONE &&
           move.moveByteArray[0] == ttHint[0] &&
           move.moveByteArray[1] == ttHint[1] &&
           move.moveByteArray[2] == ttHint[2])
          score += MOVE_SORTING_TRANSPO_BEST_MOVE_BONUS;

        if(!isQ && move.quietMove == MOVEFLAG_QUIET)                //  Killer/history (not applied to quiescence search.)
          {
            killerFlag = killerLookup(node->ply, move.moveByteArray);
            if(killerFlag == KILLER_FOUND_FIRST)
              score += MOVE_SORTING_KILLER_MOVE_1_BONUS;
            else if(killerFlag == KILLER_FOUND_SECOND)
              score += MOVE_SORTING_KILLER_MOVE_2_BONUS;
            score += historyLookup(toMove, move.moveByteArray);
          }

        movesBuffer[outCount] = move;                               //  Keep it.
        scores[outCount] = score;
        outCount++;
      }

    node->moveOffset = negamaxMoveBufferLength;                     //  The offset into "negamaxMovesBuffer" of where this node's child-moves begin.
    node->moveCount = outCount;                                     //  Overwrite moveCount with the filtered count.
    node->moveNextPtr = 0;

    if(node->moveCount == 0)                                        //  If there is nothing to search:
      {                                                             //  - In quiescence search when not in check:
                                                                    //    node->value already holds stand-pat from enterNode_step()
                                                                    //  - In check: if truly mated, isTerminal() should have caught it;
                                                                    //    otherwise finish with whatever baseline you set.
        node->phase = _PHASE_FINISH_NODE;
        saveNode(node, gsIndex);
        return;
      }

    quicksort(true, scores, movesBuffer, 0, node->moveCount - 1);   //  Sort DESCENDING according to (shallow) evaluation.

    for(i = 0; i < node->moveCount; i++)                            //  Write the sorted move-data to the global buffer.
      {
        saveMove(movesBuffer + i, negamaxMoveBufferLength);         //  Write the move-data at the (current) end of the "stack".
        negamaxMoveBufferLength++;                                  //  Increase the length of the "stack".
      }

    saveNegamaxMoveBufferLength(negamaxMoveBufferLength);           //  Write the new, expanded length of the "stack".

    node->phase = _PHASE_NEXT_MOVE;                                 //  On next pulse, take the next child.
    saveNode(node, gsIndex);                                        //  Save the newly expanded node.

    return;
  }

/* HEARTBEAT NEGAMAX: _PHASE_NEXT_MOVE
   Push the next move to be explored. */
void nextMove_step(unsigned int gsIndex, NegamaxNode* node)
  {
    unsigned int negamaxSearchBufferLength;                         //  Size of the NODE stack.
    unsigned int globalMoveIndex;                                   //  Compute the global offset of NegamaxMoves into "negamaxMovesBuffer".
    NegamaxNode child;
    NegamaxMove move;
    unsigned int i;

    negamaxSearchBufferLength = restoreNegamaxSearchBufferLength(); //  Save the current length of the NODE stack, before addition of a child node.

    if(node->moveNextPtr >= node->moveCount)                        //  If there are no more moves, then we're done generating children for this node.
      {                                                             //  ( >= rather than == is the "belt + suspenders" approach.)
        node->phase = _PHASE_FINISH_NODE;
        saveNode(node, gsIndex);
        return;                                                     //  End heartbeat.
      }

    globalMoveIndex = node->moveOffset + node->moveNextPtr;         //  Index into "negamaxMovesBuffer".
    restoreMove(globalMoveIndex, &move);                            //  Recover the NegamaxMove struct from the global byte-array.

    node->moveNextPtr++;                                            //  Point to the next move.
    node->phase = _PHASE_AFTER_CHILD;                               //  Advance this node's phase.
    saveNode(node, gsIndex);                                        //  Write the updated node.

    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                       //  Copy "node"s "gs" to "queryGameStateBuffer" for makeMove().
      queryGameStateBuffer[i] = node->gs[i];
    copyQuery2EvalGSInput();                                        //  Copy Negamax Module's "queryGameStateBuffer" to Evaluation Module's "inputGameStateBuffer".

    for(i = 0; i < _MOVE_BYTE_SIZE; i++)                            //  Copy the byte-array of the move to be made to "queryMoveBuffer" for makeMove().
      queryMoveBuffer[i] = move.moveByteArray[i];
    copyQuery2EvalMoveInput();                                      //  Copy Negamax Module's "queryMoveBuffer" to Evaluation Module's "inputMoveBuffer".

    makeMove();                                                     //  (Ask the Evaluation Module) Apply the move to the game state.

    copyEvalOutput2AnswerGSBuffer();                                //  Copy Evaluation Module's "outputGameStateBuffer" to Negamax Module's "answerGameStateBuffer".

    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                       //  Copy "answerGameStateBuffer" to child's "gs".
      child.gs[i] = answerGameStateBuffer[i];

    child.parent = gsIndex;                                         //  Fill in the child node, descended from the given node.
    child.parentMove[0] = move.moveByteArray[0];                    //  Move the parent made to get here.
    child.parentMove[1] = move.moveByteArray[1];
    child.parentMove[2] = move.moveByteArray[2];

    child.bestMove[0] = _NONE;                                      //  Blank.
    child.bestMove[1] = _NONE;
    child.bestMove[2] = _NO_PROMO;

    child.moveOffset = 0;                                           //  Blank.
    child.moveCount = 0;
    child.moveNextPtr = 0;

    child.depth = (signed char)(node->depth - 1);                   //  Depth/ply bookkeeping.
    child.ply = (unsigned char)(node->ply + 1);

    child.originalAlpha = -node->beta;                              //  Negamax window flip.
    child.alpha = -node->beta;
    child.beta = -node->alpha;
    child.value = -std::numeric_limits<float>::infinity();

    child.phase = _PHASE_ENTER_NODE;                                //  Receive at this phase.
    child.flags = 0;

    saveNode(&child, negamaxSearchBufferLength);                    //  Encode this new node at the end of the stack.
    saveNegamaxSearchBufferLength(negamaxSearchBufferLength + 1);   //  Increment the length of the stack.

    return;                                                         //  End heartbeat.
  }

/* HEARTBEAT NEGAMAX: _PHASE_AFTER_CHILD
   Incorporate the node's data into its parent. */
void afterChild_step(unsigned int gsIndex, NegamaxNode* node)
  {
    NegamaxNode parent;
    unsigned int parentIndex;
    NegamaxMove move;
    unsigned int moveIndex;
    unsigned int negamaxSearchBufferLength;
    unsigned char toMove;
    float score;
    unsigned int i;

    parentIndex = node->parent;                                     //  Retrieve the index of the parent of the given node.
    restoreNode(parentIndex, &parent);                              //  Restore the parent of the given node.

    if(parentIndex == gsIndex)                                      //  Make sure this isn't the root node.
      {
                                                                    //  Indicate error if it is.
        inputParametersBuffer[PARAM_BUFFER_STATUS_OFFSET] = STATUS_ERROR;
        return;
      }

    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                       //  Copy "parent"s "gs" to "queryGameStateBuffer" for sideToMove().
      queryGameStateBuffer[i] = parent.gs[i];
    copyQuery2EvalGSInput();                                        //  Copy Negamax Module's "queryGameStateBuffer" to Evaluation Module's "inputBuffer".
    toMove = sideToMove();                                          //  (Ask the Evaluation Module) Which side is to move?

    moveIndex = parent.moveOffset + parent.moveNextPtr - 1;         //  Retrieve the index of the move the parent made to reach this node.
    restoreMove(moveIndex, &move);                                  //  Restore the move.

    score = -node->value;

    if(score > parent.value)                                        //  Update parent's value.
      {
        parent.value = score;
        for(i = 0; i < _MOVE_BYTE_SIZE; i++)                        //  Save this move as the parent's best move.
          parent.bestMove[i] = node->parentMove[i];
      }
    if(score > parent.alpha)                                        //  Update parent's alpha.
      parent.alpha = score;
    if(parent.alpha >= parent.beta)                                 //  Cutoff.
      {
        if(move.quietMove == MOVEFLAG_QUIET)                        //  The move is "quiet": it is not a capture, not a promotion.
          {
            killerAdd(parent.ply, move.moveByteArray);              //  This is a KILLER MOVE!
            historyUpdate(toMove, parent.ply, move.moveByteArray);  //  Update the HISTORY HEURISTIC.
          }

        parent.phase = _PHASE_FINISH_NODE;                          //  Parent's work is done.
      }
    else                                                            //  Not a cutoff.
      {
        if(parent.moveNextPtr >= parent.moveCount)
          parent.phase = _PHASE_FINISH_NODE;
        else
          parent.phase = _PHASE_NEXT_MOVE;
      }

    saveNode(&parent, parentIndex);                                 //  Save the parent.

    negamaxSearchBufferLength = restoreNegamaxSearchBufferLength(); //  Recover the length of the node stack.
    saveNegamaxSearchBufferLength(negamaxSearchBufferLength - 1);   //  Shorten the length of the node stack; effectively "popping" this node.

    return;
  }

/* HEARTBEAT NEGAMAX: _PHASE_FINISH_NODE
   Write to the Transpo Table. */
void finishNode_step(unsigned int gsIndex, NegamaxNode* node)
  {
    TranspoRecord ttEntry;
    unsigned char currGen;
    unsigned char oldness;

    float v = node->value;
    float a0 = node->originalAlpha;
    float b = node->beta;
    unsigned char ttType;
    unsigned char depthStore;
    unsigned int i;

    if(v <= a0)
      ttType = NODE_TYPE_ALL;                                       //  Upper bound (fail-low).
    else if(v >= b)
      ttType = NODE_TYPE_CUT;                                       //  Lower bound (fail-high).
    else
      ttType = NODE_TYPE_PV;                                        //  Exact.

    depthStore = (node->depth > 0) ? (unsigned char)node->depth : 0;//  For storing in the transposition table; clamp to zero.
    currGen = getGeneration();                                      //  Get the current transposition-table generation.
                                                                    //  Recover whatever's at this address.
    deserializeTranspoRecord(transpositionTableBuffer + 1 + node->hIndex * _TRANSPO_RECORD_BYTE_SIZE, &ttEntry);
    oldness = ((int)currGen - (int)ttEntry.age) & 0xFF;             //  Force modulo arithmetic.
                                                                    //  Either:
                                                                    //    This slot was free. Write.
                                                                    //  Or:
                                                                    //    This slot was occupied but too old to be useful anymore. Overwrite.
                                                                    //  Or:
                                                                    //    This slot was occupied but our information now comes from a deeper depth. Overwrite.
    if(ttEntry.age == 0 || oldness >= _TRANSPO_AGE_THRESHOLD || ttEntry.depth <= depthStore)
      {
        ttEntry.lock = node->zhash;                                 //  Lock = Zobrist key.
        for(i = 0; i < _MOVE_BYTE_SIZE; i++)                        //  Copy the best move found for this state.
          ttEntry.bestMove[i] = node->bestMove[i];
        ttEntry.depth = depthStore;                                 //  Save depth.
        ttEntry.score = node->value;                                //  Save value.
        ttEntry.type = ttType;                                      //  Save the type.
        ttEntry.age = currGen;                                      //  Set the age.
      }
                                                                    //  Write this record to this sub-array.
    serializeTranspoRecord(&ttEntry, transpositionTableBuffer + 1 + node->hIndex * _TRANSPO_RECORD_BYTE_SIZE);

    if(node->moveCount > 0)                                         //  Only if this node generated moves at all (rather than early exiting).
      saveNegamaxMoveBufferLength(node->moveOffset);                //  Roll back the moves arena.

    if(node->parent == gsIndex)                                     //  Only the ROOT NODE has itself as its own parent.
      node->phase = _PHASE_COMPLETE;                                //  Prepare to write final output and report completion.
    else                                                            //  Else: set node phase to _PHASE_AFTER_CHILD so that this child updates its parent
      node->phase = _PHASE_AFTER_CHILD;                             //  and then pops itself.

    saveNode(node, gsIndex);
    return;
  }

/**************************************************************************************************
 Quicksort  */

void quicksort(bool desc, signed int* scores, NegamaxMove* moves, unsigned int lo, unsigned int hi)
  {
    unsigned int p;

    if(lo < hi)
      {
        p = partition(desc, scores, moves, lo, hi);

        if(p > 0)                                                   //  PREVENT ROLL-OVER TO 0xFFFF.
          quicksort(desc, scores, moves, lo, p - 1);                //  Left side: start quicksort.
        if(p < 65535)                                               //  PREVENT ROLL-OVER TO 0x0000.
          quicksort(desc, scores, moves, p + 1, hi);                //  Right side: start quicksort.
      }

    return;
  }

unsigned int partition(bool desc, signed int* scores, NegamaxMove* moves, unsigned int lo, unsigned int hi)
  {
    signed int pivot = scores[hi];
    unsigned int i = lo;
    unsigned int j;
    unsigned char k;
    signed int tmpSignedInt;
    NegamaxMove tmpMove;
    bool trigger;

    for(j = lo; j < hi; j++)
      {
        if(desc)
          trigger = (scores[j] > pivot);                            //  SORT DESCENDING.
        else
          trigger = (scores[j] < pivot);                            //  SORT ASCENDING.

        if(trigger)
          {
            tmpSignedInt = scores[i];                               //  Swap scores[i] with scores[j].
            scores[i]    = scores[j];
            scores[j]    = tmpSignedInt;

            for(k = 0; k < _MOVE_BYTE_SIZE; k++)                    //  tmp gets [i].
              tmpMove.moveByteArray[k] = moves[i].moveByteArray[k];
            tmpMove.quietMove = moves[i].quietMove;

            for(k = 0; k < _MOVE_BYTE_SIZE; k++)                    //  [i] gets [j].
              moves[i].moveByteArray[k] = moves[j].moveByteArray[k];
            moves[i].quietMove = moves[j].quietMove;

            for(k = 0; k < _MOVE_BYTE_SIZE; k++)                    //  [j] gets tmp.
              moves[j].moveByteArray[k] = tmpMove.moveByteArray[k];
            moves[j].quietMove = tmpMove.quietMove;

            i++;
          }
      }

    tmpSignedInt = scores[i];                                       //  Swap scores[i] with scores[hi].
    scores[i]    = scores[hi];
    scores[hi]   = tmpSignedInt;

    for(k = 0; k < _MOVE_BYTE_SIZE; k++)                            //  tmp gets [i].
      tmpMove.moveByteArray[k] = moves[i].moveByteArray[k];
    tmpMove.quietMove = moves[i].quietMove;

    for(k = 0; k < _MOVE_BYTE_SIZE; k++)                            //  [i] gets [hi].
      moves[i].moveByteArray[k] = moves[hi].moveByteArray[k];
    moves[i].quietMove = moves[j].quietMove;

    for(k = 0; k < _MOVE_BYTE_SIZE; k++)                            //  [hi] gets tmp.
      moves[hi].moveByteArray[k] = tmpMove.moveByteArray[k];
    moves[j].quietMove = tmpMove.quietMove;

    return i;
  }

/**************************************************************************************************
 Search-buffer functions  */

/* Read the first 4 bytes from "negamaxSearchBuffer" and return the unsigned int they describe. */
unsigned int restoreNegamaxSearchBufferLength(void)
  {
    unsigned char buffer4[4];
    unsigned int ui4;
    unsigned char i;

    for(i = 0; i < 4; i++)                                          //  Copy the first 4 bytes from the global buffer.
      buffer4[i] = negamaxSearchBuffer[i];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    return ui4;                                                     //  Return the unsigned int.
  }

/* Read the first 4 bytes from "negamaxMovesBuffer" and return the unsigned int they describe. */
unsigned int restoreNegamaxMoveBufferLength(void)
  {
    unsigned char buffer4[4];
    unsigned int ui4;
    unsigned char i;

    for(i = 0; i < 4; i++)                                          //  Copy the first 4 bytes from the global buffer.
      buffer4[i] = negamaxMovesBuffer[i];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    return ui4;                                                     //  Return the unsigned int.
  }

/* Write the given "len" to the first 4 bytes of "negamaxSearchBuffer". */
void saveNegamaxSearchBufferLength(unsigned int len)
  {
    unsigned char buffer4[4];
    unsigned char i;

    memcpy(buffer4, (unsigned char*)(&len), 4);                     //  Force unsigned int into 4-byte unsigned char buffer.
    for(i = 0; i < 4; i++)                                          //  Write 4-byte buffer to "negamaxSearchBuffer".
      negamaxSearchBuffer[i] = buffer4[i];

    return;
  }

/* Write the given "len" to the first 4 bytes of "negamaxMovesBuffer". */
void saveNegamaxMoveBufferLength(unsigned int len)
  {
    unsigned char buffer4[4];
    unsigned char i;

    memcpy(buffer4, (unsigned char*)(&len), 4);                     //  Force unsigned int into 4-byte unsigned char buffer.
    for(i = 0; i < 4; i++)                                          //  Write 4-byte buffer to "negamaxMovesBuffer".
      negamaxMovesBuffer[i] = buffer4[i];

    return;
  }

/* In "negamaxSearchBuffer", at an offset of "index" * sizeof(NegamaxNode), read the bytes there into the given struct, "node". */
void restoreNode(unsigned int index, NegamaxNode* node)
  {
    unsigned char buffer[_NEGAMAX_NODE_BYTE_SIZE];
    unsigned char buffer4[4];
    unsigned char buffer8[8];
    unsigned int ui4;
    float f4;
    unsigned long long ull8;

    unsigned int i, j;

    for(i = 0; i < _NEGAMAX_NODE_BYTE_SIZE; i++)                    //  Copy a slice of "negamaxSearchBuffer" to the local "buffer".
      buffer[i] = negamaxSearchBuffer[index * _NEGAMAX_NODE_BYTE_SIZE + 4 + i];

    i = 0;                                                          //  Reset "i" to iterate through local buffer.
    for(j = 0; j < _GAMESTATE_BYTE_SIZE; j++)                       //  Recover node's gamestate byte array from local buffer.
      node->gs[j] = buffer[i++];                                    //  NODE.GS

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    node->parent = ui4;                                             //  NODE.PARENT

    for(j = 0; j < _MOVE_BYTE_SIZE; j++)                            //  Recover node's parent-move from local buffer.
      node->parentMove[j] = buffer[i++];                            //  NODE.PARENTMOVE

    for(j = 0; j < _MOVE_BYTE_SIZE; j++)                            //  Recover node's best-move from local buffer.
      node->bestMove[j] = buffer[i++];                              //  NODE.BESTMOVE

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    node->moveOffset = ui4;                                         //  NODE.MOVEOFFSET

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    node->moveCount = ui4;                                          //  NODE.MOVECOUNT

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    node->moveNextPtr = ui4;                                        //  NODE.MOVENEXTPTR

    node->depth = (signed char)buffer[i++];                         //  NODE.DEPTH
    node->ply = (unsigned char)buffer[i++];                         //  NODE.PLY

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&f4, buffer4, 4);                                        //  Force the 4-byte buffer into a float.
    node->originalAlpha = f4;                                       //  NODE.ORIGINALALPHA

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&f4, buffer4, 4);                                        //  Force the 4-byte buffer into a float.
    node->alpha = f4;                                               //  NODE.ALPHA

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&f4, buffer4, 4);                                        //  Force the 4-byte buffer into a float.
    node->beta = f4;                                                //  NODE.BETA

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&f4, buffer4, 4);                                        //  Force the 4-byte buffer into a float.
    node->value = f4;                                               //  NODE.VALUE

    for(j = 0; j < 8; j++)                                          //  Copy 8 bytes from the local buffer.
      buffer8[j] = buffer[i++];
    memcpy(&ull8, buffer8, 8);                                      //  Force the 8-byte buffer into an unsigned long long.
    node->zhash = ull8;                                             //  NODE.ZHASH

    for(j = 0; j < 4; j++)                                          //  Copy 4 bytes from the local buffer.
      buffer4[j] = buffer[i++];
    memcpy(&ui4, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.
    node->hIndex = ui4;                                             //  NODE.HINDEX

    node->phase = buffer[i++];                                      //  NODE.PHASE
    node->flags = buffer[i++];                                      //  NODE.FLAGS

    return;
  }

/* Write bytes for the given struct "node" to "negamaxSearchBuffer" at an offset of "index" * sizeof(NegamaxNode). */
void saveNode(NegamaxNode* node, unsigned int index)
  {
    unsigned char buffer[_NEGAMAX_NODE_BYTE_SIZE];
    unsigned char buffer4[4];
    unsigned char buffer8[8];
    unsigned int ui4;
    float f4;
    unsigned long long ull8;

    unsigned int i, j;

    i = 0;
    for(j = 0; j < _GAMESTATE_BYTE_SIZE; j++)                       //  Copy node's gamestate byte array to buffer.
      buffer[i++] = node->gs[j];                                    //  NODE.GS

    ui4 = node->parent;                                             //  Copy node's parent index to buffer.
    memcpy(buffer4, (unsigned char*)(&ui4), 4);                     //  Force unsigned int into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.PARENT

    for(j = 0; j < _MOVE_BYTE_SIZE; j++)                            //  Copy node's parent-move to buffer.
      buffer[i++] = node->parentMove[j];                            //  NODE.PARENTMOVE

    for(j = 0; j < _MOVE_BYTE_SIZE; j++)                            //  Copy node's best-move to buffer.
      buffer[i++] = node->bestMove[j];                              //  NODE.BESTMOVE

    ui4 = node->moveOffset;                                         //  Copy the node's move-offset to the buffer.
    memcpy(buffer4, (unsigned char*)(&ui4), 4);                     //  Force unsigned int into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.MOVEOFFSET

    ui4 = node->moveCount;                                          //  Copy the node's move-count to the buffer.
    memcpy(buffer4, (unsigned char*)(&ui4), 4);                     //  Force unsigned int into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.MOVECOUNT

    ui4 = node->moveNextPtr;                                        //  Copy the node's move-to-try-next to the buffer.
    memcpy(buffer4, (unsigned char*)(&ui4), 4);                     //  Force unsigned int into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.MOVENEXTPTR

    buffer[i++] = (unsigned char)node->depth;                       //  NODE.DEPTH
    buffer[i++] = (unsigned char)node->ply;                         //  NODE.PLY

    f4 = node->originalAlpha;                                       //  Copy the node's originalAlpha to the buffer.
    memcpy(buffer4, (unsigned char*)(&f4), 4);                      //  Force float into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.ORIGINALALPHA

    f4 = node->alpha;                                               //  Copy the node's alpha to the buffer.
    memcpy(buffer4, (unsigned char*)(&f4), 4);                      //  Force float into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.ALPHA

    f4 = node->beta;                                                //  Copy the node's beta to the buffer.
    memcpy(buffer4, (unsigned char*)(&f4), 4);                      //  Force float into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.BETA

    f4 = node->value;                                               //  Copy the node's value to the buffer.
    memcpy(buffer4, (unsigned char*)(&f4), 4);                      //  Force float into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.VALUE

    ull8 = node->zhash;                                             //  Copy the node's zhash to the buffer.
    memcpy(buffer8, (unsigned char*)(&ull8), 8);                    //  Force unsigned long long into 8-byte unsigned char buffer.
    for(j = 0; j < 8; j++)                                          //  Append 8-byte buffer to buffer.
      buffer[i++] = buffer8[j];                                     //  NODE.ZHASH

    ui4 = node->hIndex;                                             //  Copy the node's hIndex to the buffer.
    memcpy(buffer4, (unsigned char*)(&ui4), 4);                     //  Force unsigned int into 4-byte unsigned char buffer.
    for(j = 0; j < 4; j++)                                          //  Append 4-byte buffer to buffer.
      buffer[i++] = buffer4[j];                                     //  NODE.HINDEX

    buffer[i++] = node->phase;                                      //  NODE.PHASE
    buffer[i++] = node->flags;                                      //  NODE.FLAGS

    for(i = 0; i < _NEGAMAX_NODE_BYTE_SIZE; i++)                    //  Now write "buffer" to an offset location in "negamaxSearchBuffer".
      negamaxSearchBuffer[index * _NEGAMAX_NODE_BYTE_SIZE + 4 + i] = buffer[i];

    return;
  }

/* In "negamaxMovesBuffer", at an offset of "index" * sizeof(NegamaxMove), read the bytes there into the given struct, "moveData". */
void restoreMove(unsigned int index, NegamaxMove* moveData)
  {
    unsigned char buffer[_NEGAMAX_MOVE_BYTE_SIZE];
    unsigned int i, j;

    for(i = 0; i < _NEGAMAX_MOVE_BYTE_SIZE; i++)                    //  Copy a slice of "negamaxMovesBuffer" to the local "buffer".
      buffer[i] = negamaxMovesBuffer[4 + index * _NEGAMAX_MOVE_BYTE_SIZE + i];

    i = 0;
    for(j = 0; j < _MOVE_BYTE_SIZE; j++)
      moveData->moveByteArray[j] = buffer[i++];                     //  MOVE.MOVEBYTEARRAY

    moveData->quietMove = buffer[i++];                              //  MOVE.QUIETMOVE

    return;
  }

/* Write bytes for the given Negamax move (byte array) to "negamaxMovesBuffer" at an offset of "index" * _MOVE_BYTE_SIZE. */
void saveMove(NegamaxMove* moveData, unsigned int index)
  {
    unsigned char buffer[_NEGAMAX_MOVE_BYTE_SIZE];
    unsigned int i, j;

    i = 0;
    for(j = 0; j < _MOVE_BYTE_SIZE; j++)
      buffer[i++] = moveData->moveByteArray[j];                     //  MOVE.MOVEBYTEARRAY

    buffer[i++] = moveData->quietMove;                              //  MOVE.QUIETMOVE

    for(i = 0; i < _NEGAMAX_MOVE_BYTE_SIZE; i++)                    //  Now write "buffer" to an offset location in "negamaxMovesBuffer".
      negamaxMovesBuffer[4 + index * _NEGAMAX_MOVE_BYTE_SIZE + i] = buffer[i];

    return;
  }

/**************************************************************************************************
 Zobrist hashing  */

/* Hash the given byte array "hashInputBuffer". */
unsigned long long hash(unsigned char* hashInputBuffer)
  {
    unsigned long long h = 0L;
    unsigned int index;
    unsigned int i;
    unsigned char j, k, l;
    unsigned char mask;
    unsigned char buffer8[8];                                       //  Byte array to hold byte array version of unsigned long long.
    unsigned long long ull8;                                        //  The unsigned long long we will actually use to hash.

    if((hashInputBuffer[0] & 128) == 128)                           //  Hash the side to move.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[W_TO_MOVE * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    if((hashInputBuffer[0] & 64) == 64)                             //  Hash white's castling data.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[W_KINGSIDE_CASTLE * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    if((hashInputBuffer[0] & 32) == 32)
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[W_QUEENSIDE_CASTLE * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    if((hashInputBuffer[0] & 16) == 16)
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[W_CASTLED * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    if((hashInputBuffer[0] & 8) == 8)                               //  Hash black's castling data.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[B_KINGSIDE_CASTLE * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    if((hashInputBuffer[0] & 4) == 4)
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[B_QUEENSIDE_CASTLE * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    if((hashInputBuffer[0] & 2) == 2)
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[B_CASTLED * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }

    if(hashInputBuffer[1] == 1)                                     //  Hash whether a pawn's doulbe move previously occurred in column A.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_A * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 2)                                //  Hash whether a pawn's doulbe move previously occurred in column B.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_B * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 3)                                //  Hash whether a pawn's doulbe move previously occurred in column C.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_C * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 4)                                //  Hash whether a pawn's doulbe move previously occurred in column D.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_D * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 5)                                //  Hash whether a pawn's doulbe move previously occurred in column E.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_E * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 6)                                //  Hash whether a pawn's doulbe move previously occurred in column F.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_F * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 7)                                //  Hash whether a pawn's doulbe move previously occurred in column G.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_G * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 8)                                //  Hash whether a pawn's doulbe move previously occurred in column H.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_H * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 9)                                //  Hash whether a pawn's doulbe move previously occurred in column I.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_I * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }
    else if(hashInputBuffer[1] == 10)                               //  Hash whether a pawn's doulbe move previously occurred in column J.
      {
        for(l = 0; l < 8; l++)                                      //  Copy 8 bytes from the serial buffer.
          buffer8[l] = zobristHashBuffer[PREV_DOUBLE_COL_J * 8 + l];
        memcpy(&ull8, buffer8, 8);                                  //  Force the 8-byte buffer into an unsigned long long.
        h ^= ull8;
      }

    i = 2;
    for(index = 0; index < _NONE; index++)
      {
                                                                    //  There can be no pawns on row 1 or row 8.
        if(hashInputBuffer[i] == _WHITE_PAWN && index >= 10 && index < 70)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WP_A2 + index - 8) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _WHITE_KNIGHT)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WN_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _WHITE_BISHOP)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WB_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _WHITE_ROOK)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WR_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _WHITE_ARCHBISHOP)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WA_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _WHITE_CHANCELLOR)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WC_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _WHITE_QUEEN)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WQ_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _WHITE_KING)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(WK_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
                                                                    //  There can be no pawns on row 1 or row 8.
        else if(hashInputBuffer[i] == _BLACK_PAWN && index >= 10 && index < 70)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BP_A2 + index - 8) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _BLACK_KNIGHT)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BN_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _BLACK_BISHOP)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BB_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _BLACK_ROOK)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BR_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _BLACK_ARCHBISHOP)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BA_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _BLACK_CHANCELLOR)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BC_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _BLACK_QUEEN)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BQ_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }
        else if(hashInputBuffer[i] == _BLACK_KING)
          {
            for(l = 0; l < 8; l++)                                  //  Copy 8 bytes from the serial buffer.
              buffer8[l] = zobristHashBuffer[(BK_A1 + index) * 8 + l];
            memcpy(&ull8, buffer8, 8);                              //  Force the 8-byte buffer into an unsigned long long.
            h ^= ull8;
          }

        i++;
      }

    return h;
  }

/**************************************************************************************************
 Killer-move functions  */

/* Look up the given move, at the given ply.
   If this is the first (freshest) killer-move listed, then return 1.
   If this is the second (less fresh) killer-move listed, then return 2.
   If the given move, at the given ply is not found, return 0. */
unsigned char killerLookup(unsigned char ply, unsigned char* moveByteArray)
  {
    unsigned int offset;

    offset = ply * _KILLER_MOVE_PER_PLY * 2;
    if(killerMovesTableBuffer[offset] == moveByteArray[0] && killerMovesTableBuffer[offset + 1] == moveByteArray[1])
      return KILLER_FOUND_FIRST;

    offset += 2;
    if(killerMovesTableBuffer[offset] == moveByteArray[0] && killerMovesTableBuffer[offset + 1] == moveByteArray[1])
      return KILLER_FOUND_SECOND;

    return KILLER_NOT_FOUND;
  }

/* Add the given killer move at the given ply.
   Demote whatever was there before. */
void killerAdd(unsigned char ply, unsigned char* moveByteArray)
  {
    unsigned int offset = ply * _KILLER_MOVE_PER_PLY * 2;
    unsigned char tmpFrom, tmpTo;
                                                                    //  If the given move is already equal to the first preferred killer,
                                                                    //  then do nothing.
    if(killerMovesTableBuffer[offset] == moveByteArray[0] && killerMovesTableBuffer[offset + 1] == moveByteArray[1])
      return;
                                                                    //  If the given move is already equal to the second preferred killer,
                                                                    //  then promote number 2 to number 1 and demote number 1 to number 2.
    if(killerMovesTableBuffer[offset + 2] == moveByteArray[0] && killerMovesTableBuffer[offset + 3] == moveByteArray[1])
      {
        tmpFrom = killerMovesTableBuffer[offset + 2];               //  Killer #2 --> tmp
        tmpTo   = killerMovesTableBuffer[offset + 3];
                                                                    //  Killer #1 --> Killer #2
        killerMovesTableBuffer[offset + 2] = killerMovesTableBuffer[offset    ];
        killerMovesTableBuffer[offset + 3] = killerMovesTableBuffer[offset + 1];

        killerMovesTableBuffer[offset    ] = tmpFrom;               //  tmp --> Killer #1
        killerMovesTableBuffer[offset + 1] = tmpTo;

        return;
      }
                                                                    //  If the given move is new,
                                                                    //  then drop number 2, demote number 1 to number 2, and the given move the new number 1.
    killerMovesTableBuffer[offset + 2] = killerMovesTableBuffer[offset    ];
    killerMovesTableBuffer[offset + 3] = killerMovesTableBuffer[offset + 1];

    killerMovesTableBuffer[offset    ] = moveByteArray[0];
    killerMovesTableBuffer[offset + 1] = moveByteArray[1];

    return;
  }

/**************************************************************************************************
 History-heuristic functions  */

/* Look up the history-heuristic score for the given side to move, the given move (sans promotion). */
unsigned int historyLookup(unsigned char sideToMove, unsigned char* moveByteArray)
  {
    unsigned int offset = sideToMove * _NONE * _NONE;

    offset += moveByteArray[0] * _NONE + moveByteArray[1];

    return (unsigned int)historyTableBuffer[offset];
  }

/* Increment the history-heuristic score for the given side to move, the given move (sans promotion). */
void historyUpdate(unsigned char sideToMove, unsigned char ply, unsigned char* moveByteArray)
  {
    unsigned int inc = ply * ply;
    unsigned int value;
    unsigned int offset = sideToMove * _NONE * _NONE;

    offset += moveByteArray[0] * _NONE + moveByteArray[1];
    value = historyTableBuffer[offset] + inc;

    historyTableBuffer[offset] = (value > 255) ? 255 : (unsigned char)value;

    return;
  }

/**************************************************************************************************
 Node Ctr++  */

void incrementNodeCtr(void)
  {
    unsigned int ctr;
    unsigned char buffer4[4];
    unsigned char i;

    for(i = 0; i < 4; i++)
      buffer4[i] = inputParametersBuffer[PARAM_BUFFER_NODESSEARCHED_OFFSET + i];

    memcpy(&ctr, buffer4, 4);                                       //  Force the 4-byte buffer into an unsigned int.

    ctr++;

    memcpy(buffer4, (unsigned char*)(&ctr), 4);                     //  Force the unsigned int into a 4-byte temp buffer.
    for(i = 0; i < 4; i++)                                          //  Copy bytes to parameters buffer.
      inputParametersBuffer[PARAM_BUFFER_NODESSEARCHED_OFFSET + i] = buffer4[i];

    return;
  }