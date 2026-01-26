/*

Game logic module for the human player.

sudo docker run --rm -v $(pwd):/src -u $(id -u):$(id -g) --mount type=bind,source=$(pwd),target=/home/src emscripten-c emcc -Os -s STANDALONE_WASM -s EXPORTED_FUNCTIONS="['_getCurrentState','_getMovesBuffer','_sideToMove_client','_setup_client','_isWhite_client','_isBlack_client','_isEmpty_client','_isPawn_client','_isKnight_client','_isBishop_client','_isRook_client','_isArchbishop_client','_isChancellor_client','_isQueen_client','_isKing_client','_whiteKingsidePrivilege_client','_whiteQueensidePrivilege_client','_whiteCastled_client','_blackKingsidePrivilege_client','_blackQueensidePrivilege_client','_blackCastled_client','_isCastle_client','_getMovesIndex_client','_makeMove_client','_isTerminal_client','_isWin_client','_draw']" -Wl,--no-entry "gamelogic.c" -o "gamelogic.wasm"

*/

#include "gamestate.h"

/**************************************************************************************************
 Typedefs  */


/**************************************************************************************************
 Prototypes  */

__attribute__((import_module("env"), import_name("_printRow"))) void printRow(char a, char b, char c, char d, char e, char f, char g, char h, char i, char j);
__attribute__((import_module("env"), import_name("_printGameStateData"))) void printGameStateData(bool wToMove,
                                                                                                  bool wKingside, bool wQueenside, bool wCastled,
                                                                                                  bool bKingside, bool bQueenside, bool bCastled,
                                                                                                  unsigned char previousDoubleMoveColumn, unsigned char moveCtr);
unsigned char* getCurrentState(void);
unsigned char* getMovesBuffer(void);
void serialize(GameState*);
void deserialize(GameState*);

unsigned char sideToMove_client(void);
unsigned char setup_client(void);
bool isWhite_client(unsigned char);
bool isBlack_client(unsigned char);
bool isEmpty_client(unsigned char);
bool isPawn_client(unsigned char);
bool isKnight_client(unsigned char);
bool isBishop_client(unsigned char);
bool isRook_client(unsigned char);
bool isArchbishop_client(unsigned char);
bool isChancellor_client(unsigned char);
bool isQueen_client(unsigned char);
bool isKing_client(unsigned char);

bool whiteKingsidePrivilege_client(void);
bool whiteQueensidePrivilege_client(void);
bool whiteCastled_client(void);

bool blackKingsidePrivilege_client(void);
bool blackQueensidePrivilege_client(void);
bool blackCastled_client(void);

bool isCastle_client(unsigned char, unsigned char, unsigned char);

unsigned int getMovesIndex_client(unsigned char);
void makeMove_client(unsigned char, unsigned char, unsigned char);
bool isTerminal_client(void);
unsigned char isWin_client(void);

void draw(void);

/**************************************************************************************************
 Globals  */

unsigned char currentState[_GAMESTATE_BYTE_SIZE];                   //  Global array containing the serialized game state.
unsigned char movesBuffer[_MAX_NUM_TARGETS];                        //  Global array containing the unique destination-indices
                                                                    //  (not necessarily the number of unique moves) available.

/**************************************************************************************************
 Functions  */

/* Expose the global array declared here to JavaScript.  */
unsigned char* getCurrentState(void)
  {
    return &currentState[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getMovesBuffer(void)
  {
    return &movesBuffer[0];
  }

/* Pack a GameState into the unsigned-char buffer "currentState". */
void serialize(GameState* gs)
  {
    unsigned char i = 0, j;
    unsigned char ch;

    //////////////////////////////////////////////////////////////////  (1 byte) Encode side to move and castling data.
    ch = 0;
    if(gs->whiteToMove)                                             //  Set bit: white to move.
      ch |= 128;
    if(gs->whiteKingsidePrivilege)                                  //  Set bit: white's kingside castling privilege.
      ch |= 64;
    if(gs->whiteQueensidePrivilege)                                 //  Set bit: white's queenside castling privilege.
      ch |= 32;
    if(gs->whiteCastled)                                            //  Set bit: white has castled.
      ch |= 16;
    if(gs->blackKingsidePrivilege)                                  //  Set bit: black's kingside castling privilege.
      ch |= 8;
    if(gs->blackQueensidePrivilege)                                 //  Set bit: black's queenside castling privilege.
      ch |= 4;
    if(gs->blackCastled)                                            //  Set bit: black has castled.
      ch |= 2;

    currentState[i++] = ch;                                         //  Write byte.

    //////////////////////////////////////////////////////////////////  (1 byte) Encode previous pawn double move data.
    ch = gs->previousDoublePawnMove;
    currentState[i++] = ch;                                         //  Write byte.

    //////////////////////////////////////////////////////////////////  (80 bytes) Encode the board.
    for(j = 0; j < _NONE; j++)
      {
        ch = 0;
        if(isWhite(j, gs))
          {
            if(isPawn(j, gs))
              ch = _WHITE_PAWN;
            else if(isKnight(j, gs))
              ch = _WHITE_KNIGHT;
            else if(isBishop(j, gs))
              ch = _WHITE_BISHOP;
            else if(isRook(j, gs))
              ch = _WHITE_ROOK;
            else if(isArchbishop(j, gs))
              ch = _WHITE_ARCHBISHOP;
            else if(isChancellor(j, gs))
              ch = _WHITE_CHANCELLOR;
            else if(isQueen(j, gs))
              ch = _WHITE_QUEEN;
            else
              ch = _WHITE_KING;
          }
        else if(isBlack(j, gs))
          {
            if(isPawn(j, gs))
              ch = _BLACK_PAWN;
            else if(isKnight(j, gs))
              ch = _BLACK_KNIGHT;
            else if(isBishop(j, gs))
              ch = _BLACK_BISHOP;
            else if(isRook(j, gs))
              ch = _BLACK_ROOK;
            else if(isArchbishop(j, gs))
              ch = _BLACK_ARCHBISHOP;
            else if(isChancellor(j, gs))
              ch = _BLACK_CHANCELLOR;
            else if(isQueen(j, gs))
              ch = _BLACK_QUEEN;
            else
              ch = _BLACK_KING;
          }

        currentState[i++] = ch;                                     //  Write byte.
      }

    //////////////////////////////////////////////////////////////////  (1 byte) Encode the setup.
    currentState[i++] = gs->setup;

    //////////////////////////////////////////////////////////////////  (1 byte) Encode the move counter.
    currentState[i++] = gs->moveCtr;

    return;                                                         //  TOTAL: 84 bytes.
  }

/* Recover a GameState from the unsigned-char buffer "currentState". */
void deserialize(GameState* gs)
  {
    unsigned char i, j;

    for(i = 0; i < _NONE; i++)                                      //  Fill-in/blank-out.
      gs->board[i] = _EMPTY;
    gs->previousDoublePawnMove = 0;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode side to move and castling data.
    gs->whiteToMove = ((currentState[0] & 128) == 128);             //  Recover side to move.

    gs->whiteKingsidePrivilege = ((currentState[0] & 64) == 64);    //  Recover white's castling data.
    gs->whiteQueensidePrivilege = ((currentState[0] & 32) == 32);
    gs->whiteCastled = ((currentState[0] & 16) == 16);

    gs->blackKingsidePrivilege = ((currentState[0] & 8) == 8);      //  Recover black's castling data.
    gs->blackQueensidePrivilege = ((currentState[0] & 4) == 4);
    gs->blackCastled = ((currentState[0] & 2) == 2);

    //////////////////////////////////////////////////////////////////  (1 byte) Decode en-passant data.
    gs->previousDoublePawnMove = currentState[1];

    if(gs->previousDoublePawnMove > 10)
      gs->previousDoublePawnMove = 0;                               //  "There can be only one!"

    //////////////////////////////////////////////////////////////////  (80 bytes) Decode the board.
    i = 2;
    for(j = 0; j < _NONE; j++)
      {
        if(currentState[i] == _WHITE_PAWN)
          gs->board[j] = _WHITE_PAWN;
        else if(currentState[i] == _WHITE_KNIGHT)
          gs->board[j] = _WHITE_KNIGHT;
        else if(currentState[i] == _WHITE_BISHOP)
          gs->board[j] = _WHITE_BISHOP;
        else if(currentState[i] == _WHITE_ROOK)
          gs->board[j] = _WHITE_ROOK;
        else if(currentState[i] == _WHITE_ARCHBISHOP)
          gs->board[j] = _WHITE_ARCHBISHOP;
        else if(currentState[i] == _WHITE_CHANCELLOR)
          gs->board[j] = _WHITE_CHANCELLOR;
        else if(currentState[i] == _WHITE_QUEEN)
          gs->board[j] = _WHITE_QUEEN;
        else if(currentState[i] == _WHITE_KING)
          gs->board[j] = _WHITE_KING;
        else if(currentState[i] == _BLACK_PAWN)
          gs->board[j] = _BLACK_PAWN;
        else if(currentState[i] == _BLACK_KNIGHT)
          gs->board[j] = _BLACK_KNIGHT;
        else if(currentState[i] == _BLACK_BISHOP)
          gs->board[j] = _BLACK_BISHOP;
        else if(currentState[i] == _BLACK_ROOK)
          gs->board[j] = _BLACK_ROOK;
        else if(currentState[i] == _BLACK_ARCHBISHOP)
          gs->board[j] = _BLACK_ARCHBISHOP;
        else if(currentState[i] == _BLACK_CHANCELLOR)
          gs->board[j] = _BLACK_CHANCELLOR;
        else if(currentState[i] == _BLACK_QUEEN)
          gs->board[j] = _BLACK_QUEEN;
        else if(currentState[i] == _BLACK_KING)
          gs->board[j] = _BLACK_KING;

        i++;
      }

    //////////////////////////////////////////////////////////////////  (1 byte) Decode the setup.
    gs->setup = currentState[i++];

    //////////////////////////////////////////////////////////////////  (1 byte) Decode the move counter.
    gs->moveCtr = currentState[i++];

    return;                                                         //  TOTAL: 84 bytes.
  }

/* Answer the client-side question, Whose turn is it? */
unsigned char sideToMove_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return (gs.whiteToMove) ? _WHITE_TO_MOVE : _BLACK_TO_MOVE;
  }

unsigned char setup_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return gs.setup;
  }

bool isWhite_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isWhite(index, &gs);
  }

bool isBlack_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isBlack(index, &gs);
  }

bool isEmpty_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isEmpty(index, &gs);
  }

bool isPawn_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isPawn(index, &gs);
  }

bool isKnight_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isKnight(index, &gs);
  }

bool isBishop_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isBishop(index, &gs);
  }

bool isRook_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isRook(index, &gs);
  }

bool isArchbishop_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isArchbishop(index, &gs);
  }

bool isChancellor_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isChancellor(index, &gs);
  }

bool isQueen_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isQueen(index, &gs);
  }

bool isKing_client(unsigned char index)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isKing(index, &gs);
  }

bool whiteKingsidePrivilege_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return gs.whiteKingsidePrivilege;
  }

bool whiteQueensidePrivilege_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return gs.whiteQueensidePrivilege;
  }

bool whiteCastled_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return gs.whiteCastled;
  }

bool blackKingsidePrivilege_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return gs.blackKingsidePrivilege;
  }

bool blackQueensidePrivilege_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return gs.blackQueensidePrivilege;
  }

bool blackCastled_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return gs.blackCastled;
  }

bool isCastle_client(unsigned char from, unsigned char to, unsigned char promo)
  {
    GameState gs;
    Move move;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    move.from = from;
    move.to = to;
    move.promo = promo;
    return isCastle(&move, &gs);
  }

bool isTerminal_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return terminal(&gs);
  }

/* Returns unsigned char in {GAME_ONGOING         = 0,
                             GAME_OVER_WHITE_WINS = 1,
                             GAME_OVER_BLACK_WINS = 2,
                             GAME_OVER_STALEMATE  = 3}. */
unsigned char isWin_client(void)
  {
    GameState gs;
    deserialize(&gs);                                               //  Recover GameState from buffer.
    return isWin(&gs);
  }

/* Given an index, recover the game state from the global buffer "currentState", and compute the moves available to the piece at "index."
   The number of moves is returned, and that many bytes in "movesBuffer" will contain a destinations.
   This function is intended to answer queries from the human player. */
unsigned int getMovesIndex_client(unsigned char index)
  {
    GameState gs;
    Move moves[_MAX_NUM_TARGETS];                                   //  Generous upper bound assumes that a single piece could reach half of all squares.
    unsigned int len, i = 0, j;
    unsigned int ctr;
    unsigned char indices[_MAX_NUM_TARGETS];

    deserialize(&gs);                                               //  Recover GameState from buffer.
    len = getMovesIndex(index, &gs, moves);

    ctr = 0;
    for(i = 0; i < len; i++)                                        //  Iterate through moves for index and identify unique destination indices.
      {
        j = 0;
        while(j < ctr && indices[j] != moves[i].to)
          j++;
        if(j == ctr)
          indices[ctr++] = moves[i].to;
      }

    i = 0;                                                          //  Reset. 'i' now iterates into 'movesBuffer'.
    for(len = 0; len < ctr; len++)
      movesBuffer[i++] = indices[len];

    return ctr;
  }

/* Update "currentState" according to the given move data (if those data are indeed valid!) */
void makeMove_client(unsigned char from, unsigned char to, unsigned char promo)
  {
    GameState gs;
    Move moves[_NONE];                                              //  Generous assumption that every square is reachable.
    Move move;
    unsigned int len, i;

    deserialize(&gs);                                               //  Recover GameState from buffer.
    len = getMovesIndex(from, &gs, moves);                          //  Make sure that this move is legal.
    i = 0;                                                          //  Otherwise, ignore it. Cheaters lose their turns!
    while(i < len && !(moves[i].from == from && moves[i].to == to && moves[i].promo == promo))
      i++;
    if(i < len)
      {
        move.from = from;
        move.to = to;
        move.promo = promo;
        makeMove(&move, &gs);
      }

    serialize(&gs);                                                 //  Write updated GameState back to buffer.

    return;
  }

/* Draw the board to the JavaScript console.
   r n a b q k b c n r
   p p p p p p p p p p
   . . . . . . . . . .
   . . . . . . . . . .
   . . . . . . . . . .
   . . . . . . . . . .
   P P P P P P P P P P
   R N A B Q K B C N R */
void draw(void)
  {
    GameState gs;
    signed char y;

    deserialize(&gs);                                               //  Recover GameState from buffer.

    for(y = 7; y >= 0; y--)
      printRow(gs.board[y * 10], gs.board[y * 10 + 1], gs.board[y * 10 + 2], gs.board[y * 10 + 3], gs.board[y * 10 + 4], gs.board[y * 10 + 5], gs.board[y * 10 + 6], gs.board[y * 10 + 7], gs.board[y * 10 + 8], gs.board[y * 10 + 9]);

    printGameStateData(gs.whiteToMove, gs.whiteKingsidePrivilege, gs.whiteQueensidePrivilege, gs.whiteCastled,
                                       gs.blackKingsidePrivilege, gs.blackQueensidePrivilege, gs.blackCastled,
                       gs.previousDoublePawnMove, gs.moveCtr);
    return;
  }
