/*

sudo docker run --rm -v $(pwd):/src -u $(id -u):$(id -g) --mount type=bind,source=$(pwd),target=/home/src emscripten-c emcc -Os -s STANDALONE_WASM -s EXPORTED_FUNCTIONS="['_getInputGameStateBuffer','_getInputMoveBuffer','_getOutputGameStateBuffer','_getOutputMovesBuffer','_sideToMove_eval','_isTerminal_eval','_isSideToMoveInCheck_eval','_nonPawnMaterial_eval','_makeMove_eval','_makeNullMove_eval','_evaluate_eval','_getMoves_eval']" -Wl,--no-entry "pluto.c" -o "eval.wasm"

*/

#include "gamestate.h"
#include "pluto.h"

#define SEE_SCORE_PAWN               10                             /* Static Exchange Evaluation, rough pawn score. */
#define SEE_SCORE_KNIGHT             30                             /* Static Exchange Evaluation, rough knight score. */
#define SEE_SCORE_BISHOP             33                             /* Static Exchange Evaluation, rough bishop score. */
#define SEE_SCORE_ROOK               50                             /* Static Exchange Evaluation, rough rook score. */
#define SEE_SCORE_ARCHBISHOP         83                             /* Static Exchange Evaluation, rough archbishop score. */
#define SEE_SCORE_CHANCELLOR         80                             /* Static Exchange Evaluation, rough chancellor score. */
#define SEE_SCORE_QUEEN              90                             /* Static Exchange Evaluation, rough queen score. */
#define SEE_SCORE_KING             1000                             /* Static Exchange Evaluation, rough king score. */
#define MOVE_SORTING_PROMO_BONUS    800                             /* Static Exchange Evaluation, rough promotion bonus. */
#define MOVE_SORTING_CHECK_BONUS     50                             /* Static Exchange Evaluation, rough putting-opponent-in-check bonus. */

/**************************************************************************************************
 Typedefs  */


/**************************************************************************************************
 Prototypes  */

unsigned char* getInputGameStateBuffer(void);
unsigned char* getInputMoveBuffer(void);
unsigned char* getOutputGameStateBuffer(void);
unsigned char* getOutputMovesBuffer(void);

void serializeGameStateToBuffer(GameState*, unsigned char*);
void serializeMoveToBuffer(Move*, unsigned char*);
void deserializeGameState(GameState*);
void deserializeMove(Move*);

unsigned char sideToMove_eval(void);
bool isTerminal_eval(void);
bool isSideToMoveInCheck_eval(void);
unsigned char nonPawnMaterial_eval(void);
void makeMove_eval(void);
void makeNullMove_eval(void);
float evaluate_eval(void);
unsigned int getMoves_eval(void);
signed int SEE(Move*, GameState*);
signed int SEE_lookup(char);

/**************************************************************************************************
 Globals  */

unsigned char inputGameStateBuffer[_GAMESTATE_BYTE_SIZE];           //  Global array containing the serialized INPUT game state.

unsigned char inputMoveBuffer[_MOVE_BYTE_SIZE];                     //  Global array containing the serialized INPUT move.

unsigned char outputGameStateBuffer[_GAMESTATE_BYTE_SIZE];          //  Global array containing the serialized OUTPUT game state.

                                                                    //  Global array containing up to _MAX_MOVES moves.
                                                                    //  Rather than encode the number of moves in the array itself, we return an integer.
                                                                    //  Each move is represented as a byte sub-array encoding:
                                                                    //    _MOVE_BYTE_SIZE  :  bytes encoding a single move,
                                                                    //    4                :  bytes for signed integer, which is rough score.
unsigned char outputMovesBuffer[_MAX_MOVES * (_MOVE_BYTE_SIZE + 5)];//    1                :  byte (should be Boolean) indicating whether move is "quiet".

/**************************************************************************************************
 Functions  */

/* Expose the global array declared here to JavaScript.  */
unsigned char* getInputGameStateBuffer(void)
  {
    return &inputGameStateBuffer[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getInputMoveBuffer(void)
  {
    return &inputMoveBuffer[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getOutputGameStateBuffer(void)
  {
    return &outputGameStateBuffer[0];
  }

/* Expose the global array declared here to JavaScript.  */
unsigned char* getOutputMovesBuffer(void)
  {
    return &outputMovesBuffer[0];
  }

/* Write the given game state to the given buffer. */
void serializeGameStateToBuffer(GameState* gs, unsigned char* buffer)
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

    buffer[i++] = ch;                                               //  Write byte.

    //////////////////////////////////////////////////////////////////  (1 byte) Encode previous pawn double move data.
    ch = gs->previousDoublePawnMove;
    buffer[i++] = ch;                                               //  Write byte.

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

        buffer[i++] = ch;                                           //  Write byte.
      }

    //////////////////////////////////////////////////////////////////  (1 byte) Encode the setup.
    buffer[i++] = gs->setup;

    //////////////////////////////////////////////////////////////////  (1 byte) Encode the move counter.
    buffer[i++] = gs->moveCtr;

    return;                                                         //  TOTAL: 84 bytes.
  }

/* Write the given move to the given buffer. */
void serializeMoveToBuffer(Move* move, unsigned char* buffer)
  {
    unsigned char i = 0;

    buffer[i++] = move->from;
    buffer[i++] = move->to;
    buffer[i++] = move->promo;

    return;
  }

/* Recover a GameState from the unsigned-char buffer "inputGameStateBuffer". */
void deserializeGameState(GameState* gs)
  {
    unsigned char i, j;

    for(i = 0; i < _NONE; i++)                                      //  Fill-in/blank-out.
      gs->board[i] = _EMPTY;
    gs->previousDoublePawnMove = 0;

    //////////////////////////////////////////////////////////////////  (1 byte) Decode side to move and castling data.
    gs->whiteToMove = ((inputGameStateBuffer[0] & 128) == 128);     //  Recover side to move.
                                                                    //  Recover white's castling data.
    gs->whiteKingsidePrivilege = ((inputGameStateBuffer[0] & 64) == 64);
    gs->whiteQueensidePrivilege = ((inputGameStateBuffer[0] & 32) == 32);
    gs->whiteCastled = ((inputGameStateBuffer[0] & 16) == 16);
                                                                    //  Recover black's castling data.
    gs->blackKingsidePrivilege = ((inputGameStateBuffer[0] & 8) == 8);
    gs->blackQueensidePrivilege = ((inputGameStateBuffer[0] & 4) == 4);
    gs->blackCastled = ((inputGameStateBuffer[0] & 2) == 2);

    //////////////////////////////////////////////////////////////////  (1 byte) Decode en-passant data.
    gs->previousDoublePawnMove = inputGameStateBuffer[1];

    if(gs->previousDoublePawnMove > 10)
      gs->previousDoublePawnMove = 0;                               //  "There can be only one!"

    //////////////////////////////////////////////////////////////////  (80 bytes) Decode the board.
    i = 2;
    for(j = 0; j < _NONE; j++)
      {
        if(inputGameStateBuffer[i] == _WHITE_PAWN)
          gs->board[j] = _WHITE_PAWN;
        else if(inputGameStateBuffer[i] == _WHITE_KNIGHT)
          gs->board[j] = _WHITE_KNIGHT;
        else if(inputGameStateBuffer[i] == _WHITE_BISHOP)
          gs->board[j] = _WHITE_BISHOP;
        else if(inputGameStateBuffer[i] == _WHITE_ROOK)
          gs->board[j] = _WHITE_ROOK;
        else if(inputGameStateBuffer[i] == _WHITE_ARCHBISHOP)
          gs->board[j] = _WHITE_ARCHBISHOP;
        else if(inputGameStateBuffer[i] == _WHITE_CHANCELLOR)
          gs->board[j] = _WHITE_CHANCELLOR;
        else if(inputGameStateBuffer[i] == _WHITE_QUEEN)
          gs->board[j] = _WHITE_QUEEN;
        else if(inputGameStateBuffer[i] == _WHITE_KING)
          gs->board[j] = _WHITE_KING;
        else if(inputGameStateBuffer[i] == _BLACK_PAWN)
          gs->board[j] = _BLACK_PAWN;
        else if(inputGameStateBuffer[i] == _BLACK_KNIGHT)
          gs->board[j] = _BLACK_KNIGHT;
        else if(inputGameStateBuffer[i] == _BLACK_BISHOP)
          gs->board[j] = _BLACK_BISHOP;
        else if(inputGameStateBuffer[i] == _BLACK_ROOK)
          gs->board[j] = _BLACK_ROOK;
        else if(inputGameStateBuffer[i] == _BLACK_ARCHBISHOP)
          gs->board[j] = _BLACK_ARCHBISHOP;
        else if(inputGameStateBuffer[i] == _BLACK_CHANCELLOR)
          gs->board[j] = _BLACK_CHANCELLOR;
        else if(inputGameStateBuffer[i] == _BLACK_QUEEN)
          gs->board[j] = _BLACK_QUEEN;
        else if(inputGameStateBuffer[i] == _BLACK_KING)
          gs->board[j] = _BLACK_KING;

        i++;
      }

    //////////////////////////////////////////////////////////////////  (1 byte) Decode the setup.
    gs->setup = inputGameStateBuffer[i++];

    //////////////////////////////////////////////////////////////////  (1 byte) Decode the move counter.
    gs->moveCtr = inputGameStateBuffer[i++];

    return;                                                         //  TOTAL: 67 bytes.
  }

/* Recover a Move from the unsigned-char buffer "inputMoveBuffer". */
void deserializeMove(Move* move)
  {
    move->from = inputMoveBuffer[0];
    move->to = inputMoveBuffer[1];
    move->promo = inputMoveBuffer[2];
    return;
  }

/* Answer the Negamax Module's query, "Which side is to move in the GameState in the query buffer?"
   Return an unsigned char in {_WHITE_TO_MOVE, _BLACK_TO_MOVE}. */
unsigned char sideToMove_eval(void)
  {
    GameState gs;
    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    return gs.whiteToMove ? _WHITE_TO_MOVE : _BLACK_TO_MOVE;
  }

/* Answer the Negamax Module's query, "Is the GameState in the query buffer terminal?" */
bool isTerminal_eval(void)
  {
    GameState gs;
    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    return terminal(&gs);
  }

/* Answer the Negamax Module's query, "Is the side to move in the GameState in the query buffer in check?" */
bool isSideToMoveInCheck_eval(void)
  {
    GameState gs;
    unsigned char i;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.

    if(gs.whiteToMove)                                              //  White is to move; is white in check?
      {
        i = 0;
        while(i < _NONE && gs.board[i] != _WHITE_KING)
          i++;
        return inCheckBy(i, 'b', &gs);
      }
                                                                    //  Black is to move; is black in check?
    i = 0;
    while(i < _NONE && gs.board[i] != _BLACK_KING)
      i++;
    return inCheckBy(i, 'w', &gs);
  }

/* Answer the Negamax Module's query, "How much non-pawn material does the side to move have in the GameState in the query buffer?" */
unsigned char nonPawnMaterial_eval(void)
  {
    GameState gs;
    unsigned char knights = 0;
    unsigned char bishops = 0;
    unsigned char rooks = 0;
    unsigned char archbishops = 0;
    unsigned char chancellors = 0;
    unsigned char queens = 0;
    unsigned char i;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.

    if(gs.whiteToMove)
      {
        for(i = 0; i < _NONE; i++)
          {
            if(isWhite(i, &gs))
              {
                if(isKnight(i, &gs))
                  knights++;
                else if(isBishop(i, &gs))
                  bishops++;
                else if(isRook(i, &gs))
                  rooks++;
                else if(isArchbishop(i, &gs))
                  archbishops++;
                else if(isChancellor(i, &gs))
                  chancellors++;
                else if(isQueen(i, &gs))
                  queens++;
              }
          }
      }
    else
      {
        for(i = 0; i < _NONE; i++)
          {
            if(isBlack(i, &gs))
              {
                if(isKnight(i, &gs))
                  knights++;
                else if(isBishop(i, &gs))
                  bishops++;
                else if(isRook(i, &gs))
                  rooks++;
                else if(isArchbishop(i, &gs))
                  archbishops++;
                else if(isChancellor(i, &gs))
                  chancellors++;
                else if(isQueen(i, &gs))
                  queens++;
              }
          }
      }

    return 3 * (knights + bishops) + 5 * rooks + 8 * archbishops + 8 * chancellors + 9 * queens;
  }

/* Answer the Negamax Module's query, "What GameState results from making the move in the input-move buffer in the game state in the input-gamestate buffer?"
   Writes to "outputGameStateBuffer". */
void makeMove_eval(void)
  {
    GameState gs;
    Move move;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    deserializeMove(&move);                                         //  Recover Move from buffer.

    makeMove(&move, &gs);                                           //  Make the move.

    serializeGameStateToBuffer(&gs, outputGameStateBuffer);         //  Write updated GameState to output-gamestate buffer.

    return;
  }

/* For use by null-move pruning in tree-search.
   Answer the Negamax Module's query, "What GameState results from a null-move in the game state in the input-gamestate buffer?"
   Writes to "outputGameStateBuffer". */
void makeNullMove_eval(void)
  {
    GameState gs;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.

    makeNullMove(&gs);                                              //  Make the move.

    serializeGameStateToBuffer(&gs, outputGameStateBuffer);         //  Write updated GameState to output-gamestate buffer.

    return;
  }

/* Answer the Negamax Module's query, "What is the evaluation of the GameState in the input-gamestate buffer?" */
float evaluate_eval(void)
  {
    GameState gs;
    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    return score(&gs);                                              //  Negamax rule: always evaluate for the side that is now to move.
  }

/* Answer the Negamax Module's query, "What are all the moves that can be made from the GameState in the input-gamestate buffer?"
   Writes to "outputMovesBuffer":
     [_MOVE_BYTE_SIZE bytes of move, 4 bytes of a signed int, 1 byte indicating whether the move is "quiet"],
     [_MOVE_BYTE_SIZE bytes of move, 4 bytes of a signed int, 1 byte indicating whether the move is "quiet"],
                                                         . . .
     [_MOVE_BYTE_SIZE bytes of move, 4 bytes of a signed int, 1 byte indicating whether the move is "quiet"] */
unsigned int getMoves_eval()
  {
    GameState gs, child;
    Move moves[_MAX_MOVES];
    unsigned int movesLen = 0;
    signed int score, scores[_MAX_MOVES];                           //  Use fast, cheap heuristics like SEE.

    unsigned char buffer4[4];                                       //  Byte array to hold leading int = number of moves in output buffer.
    unsigned int i, j, k;
    unsigned char whiteKingIndex, blackKingIndex;

    deserializeGameState(&gs);                                      //  Recover GameState from buffer.
    movesLen = getMoves(&gs, moves);                                //  Get moves.

    for(i = 0; i < movesLen; i++)                                   //  Compute a fast-n-cheap score to help the Negamax Module sort its nodes.
      {
        scores[i] = 0;                                              //  Initialize every move to zero.

        if(isCapture(moves + i, &gs))                               //  Move is a capture.
          scores[i] += SEE(moves + i, &gs);                         //  Static Exchange Evaluation can reveal good, equal, or bad captures.
        if((moves + i)->promo != _NO_PROMO)                         //  Move is a promotion.
          scores[i] += MOVE_SORTING_PROMO_BONUS;

        copyGameState(&gs, &child);                                 //  Clone the source state.
        makeMove(moves + i, &child);                                //  Apply the candidate move.
        whiteKingIndex = 0;                                         //  Locate the white king.
        while(whiteKingIndex < _NONE && child.board[whiteKingIndex] != _WHITE_KING)
          whiteKingIndex++;
        blackKingIndex = 0;                                         //  Locate the black king.
        while(blackKingIndex < _NONE && child.board[blackKingIndex] != _BLACK_KING)
          blackKingIndex++;
                                                                    //  Move puts opponent in check?
        if( (!child.whiteToMove && inCheckBy(blackKingIndex, 'w', &child)) || (child.whiteToMove && inCheckBy(whiteKingIndex, 'b', &child)) )
          scores[i] += MOVE_SORTING_CHECK_BONUS;
      }

    i = 0;                                                          //  Point to head of output buffer.
    for(j = 0; j < movesLen; j++)                                   //  Write moves as bytes to output buffer, following uint total number of moves.
      {
        outputMovesBuffer[i++] = moves[j].from;                     //  Copy move to global byte array.
        outputMovesBuffer[i++] = moves[j].to;
        outputMovesBuffer[i++] = moves[j].promo;

        score = scores[j];
        memcpy(buffer4, (unsigned char*)(&score), 4);               //  Force the SIGNED integer into a 4-byte temp buffer.
        for(k = 0; k < 4; k++)                                      //  Copy local SIGNED score to global output byte array.
          outputMovesBuffer[i++] = buffer4[k];
                                                                    //  0: quiet; 1: capture or promotion.
        outputMovesBuffer[i++] = (moves[j].promo == _NO_PROMO || !isCapture(moves + j, &gs)) ? 0 : 1;
      }

    return movesLen;
  }

/* Static Exchange Evaluation */
signed int SEE(Move* move, GameState* src)
  {
    GameState gs;
    signed int gains[_NONE];
    unsigned char gainsLen = 0;
    unsigned char target;
    signed int capturedPieceVal;
    char capturingPiece;
    char team;
    Move buffer[_NONE];
    Move chosenMove;
    unsigned char len, i;
    signed int leastVal, val, victimVal, j;
    signed int stopHere, continueExchange;

    copyGameState(src, &gs);
    target = move->to;                                              //  Identify the target.

    capturedPieceVal = SEE_lookup(gs.board[move->to]);              //  Look up value of the captured piece.
    capturingPiece = gs.board[move->from];
    gains[0] = capturedPieceVal;
    makeMove(move, &gs);                                            //  Apply the move to be evaluated.
    team = gs.whiteToMove ? 'w' : 'b';

    while(true)                                                     //  Follow capture chain to the end.
      {
        len = attackersOfSquare(target, team, &gs, buffer);
        if(len == 0)                                                //  No further captures.
          break;

        leastVal = SEE_SCORE_KING * 2;                              //  Find the least valuable captor.
        for(i = 0; i < len; i++)
          {
            val = SEE_lookup(gs.board[ buffer[i].from ]);
            if(val < leastVal)
              {
                chosenMove.from = buffer[i].from;
                chosenMove.to = buffer[i].to;
                chosenMove.promo = _NO_PROMO;
                leastVal = val;
              }
          }
        victimVal = SEE_lookup(capturingPiece);                     //  Look up value of the victim.
        gainsLen++;
        gains[gainsLen] = victimVal - gains[gainsLen - 1];

        capturingPiece = gs.board[chosenMove.from];                 //  Identify the capturing piece.
        makeMove(&chosenMove, &gs);                                 //  Make the capture.
        team = gs.whiteToMove ? 'w' : 'b';
      }

    if(gainsLen > 0)
      {
        for(j = gainsLen - 1; j >= 0; j--)
          {
            stopHere = -gains[j];                                   //  It is in side-to-move's interest to stop here.
            continueExchange = gains[j + 1];                        //  It is in side-to-move's interest to continue exchanging.
            gains[j] = (stopHere > continueExchange) ? stopHere : continueExchange;
          }
      }

    return gains[0];
  }

/* Look up a rough material score for each piece. */
signed int SEE_lookup(char piece)
  {
    if(piece == _WHITE_PAWN || piece == _BLACK_PAWN)
      return SEE_SCORE_PAWN;
    if(piece == _WHITE_KNIGHT || piece == _BLACK_KNIGHT)
      return SEE_SCORE_KNIGHT;
    if(piece == _WHITE_BISHOP || piece == _BLACK_BISHOP)
      return SEE_SCORE_BISHOP;
    if(piece == _WHITE_ROOK || piece == _BLACK_ROOK)
      return SEE_SCORE_ROOK;
    if(piece == _WHITE_ARCHBISHOP || piece == _BLACK_ARCHBISHOP)
      return SEE_SCORE_ARCHBISHOP;
    if(piece == _WHITE_CHANCELLOR || piece == _BLACK_CHANCELLOR)
      return SEE_SCORE_CHANCELLOR;
    if(piece == _WHITE_QUEEN || piece == _BLACK_QUEEN)
      return SEE_SCORE_QUEEN;
    if(piece == _WHITE_KING || piece == _BLACK_KING)
      return SEE_SCORE_KING;
    return 0;
  }
