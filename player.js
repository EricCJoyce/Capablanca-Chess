const STATUS_UNSEARCHED = 0;                                        //  Branch object has not been examined at all.
const STATUS_BOOK_LOOKUP = 1;                                       //  Call to back-end book-lookup is currently out.
const STATUS_SEARCHING = 2;                                         //  This branch has been pushed/initialized as root, and negamax is in some state.
const STATUS_DONE_SEARCH = 3;                                       //  This branch has been searched as deeply as we want.

const NEGAMAX_STATUS_IDLE              = 0x00;                      //  (See C++ code) No search running. Awaiting instructions.
const NEGAMAX_STATUS_RUNNING           = 0x01;                      //  (See C++ code) Search running.
const NEGAMAX_STATUS_DONE              = 0x02;                      //  (See C++ code) Search complete.
const NEGAMAX_STATUS_STOP_REQUESTED    = 0x03;                      //  (See C++ code) Will halt the present search at the next safe point.
const NEGAMAX_STATUS_STOP_TIME         = 0x04;                      //  (See C++ code) Will halt the present search at the next safe point, owing to time constraints.
const NEGAMAX_STATUS_ABORTED           = 0x05;                      //  (See C++ code) Search was hard-killed: be wary of partial results.
const NEGAMAX_STATUS_ERROR             = 0xFF;                      //  (See C++ code) An error has occurred.

const NEGAMAX_CTRL_STOP_REQUESTED      = 0x01;                      //  (See C++ code) Set this byte in commandFlags to request that the present search stop.
const NEGAMAX_CTRL_HARD_ABORT          = 0x02;                      //  (See C++ code) Set this byte in commandFlags to request that the present search abort.
const NEGAMAX_CTRL_TIME_ENABLED        = 0x04;                      //  (See C++ code) Set this byte in commandFlags to indicate that search is timed.
const NEGAMAX_CTRL_PONDERING           = 0x08;                      //  (See C++ code) Set this byte in commandFlags to indicate that search occurs during opponent's turn.

/* A.I. Player class for CAPABLANCA CHESS. */
class Player
  {
    constructor()
      {
        this.team = 'Black';                                        //  In {'White', 'Black'}. Default to Black.
        this.currentPly = 1;                                        //  The depth to which the A.I. should search ON THE CURRENT ITERATION.
        this.maxPly = 1;                                            //  Maximum depth to which this A.I. should search.

        this.searchId = 0;                                          //  Each negamax run gets a unique ID.
        this.nodeCtr = 0;                                           //  Reset after every turn.

        this.branches = [];                                         //  Array of Objects, each {ByteArray(GameState), uchar(depth), ByteArray(Move)}.
        this.branchIterator = 0;                                    //                          The state in which    Depth to      The agent's reply.
                                                                    //                          the agent may act.    which agent   (Having searched.)
                                                                    //                                                has searched.
        //////////////////////////////////////////////////////////////  The evaluation engine.
        this.evaluationEngine = null;                               //  WebAssembly Module containing evaluation functions.

        this.evaluationInputGameStateOffset = null;                 //  (Offset into Module memory.)
        this.evaluationInputGameStateBuffer = null;                 //  ByteArray: Input buffer for an encoded query-gamestate.

        this.evaluationInputMoveOffset = null;                      //  (Offset into Module memory.)
        this.evaluationInputMoveBuffer = null;                      //  ByteArray: Input buffer for an encoded query-move.

        this.evaluationOutputGameStateOffset = null;                //  (Offset into Module memory.)
        this.evaluationOutputGameStateBuffer = null;                //  ByteArray: Output buffer for an encoded answer-gamestate.
                                                                    //  (You would obtain a single gamestate, for instance, after calling makeMove().)

        this.evaluationOutputMovesOffset = null;                    //  (Offset into Module memory.)
        this.evaluationOutputMovesBuffer = null;                    //  ByteArray: Output buffer for encoded answer-moves.
                                                                    //  (You would obtain several moves, for instance, after calling getMoves().)

        //////////////////////////////////////////////////////////////  The negamax engine.
        this.negamaxEngine = null;                                  //  WebAssembly Module containing the negamax search engine.

        this.negamaxInputOffset = null;                             //  (Offset into Module memory.)
        this.negamaxInputBuffer = null;                             //  ByteArray: Input buffer for tree search. Encode a query-gamestate here.
                                                                    //             When Player asks negamax something.
        this.negamaxParamsOffset = null;                            //  (Offset into Module memory.)
        this.negamaxParamsBuffer = null;                            //  ByteArray: Input buffer for search parameters.

        this.negamaxOutputOffset = null;                            //  (Offset into Module memory.)
        this.negamaxOutputBuffer = null;                            //  ByteArray: Output buffer for tree search. Decode an answer from here.
                                                                    //             When negamax answers Player.
        this.negamaxQueryGameStateOffset = null;                    //  (Offset into Module memory.)
        this.negamaxQueryGameStateBuffer = null;                    //  ByteArray: Used by the negamax module to pass encoded game states to the evaluation module.

        this.negamaxQueryMoveOffset = null;                         //  (Offset into Module memory.)
        this.negamaxQueryMoveBuffer = null;                         //  ByteArray: Used by the negamax module to pass encoded moves to the evaluation module.

        this.negamaxAnswerGameStateOffset = null;                   //  (Offset into Module memory.)
        this.negamaxAnswerGameStateBuffer = null;                   //  ByteArray: Used by the negamax module to receive encoded game states from the evaluation module.

        this.negamaxAnswerMovesOffset = null;                       //  (Offset into Module memory.)
        this.negamaxAnswerMovesBuffer = null;                       //  ByteArray: Used by the negamax module to receive encoded moves from the evaluation module.

        this.ZobristHashOffset = null;                              //  (Offset into Module memory.)
        this.ZobristHashBuffer = null;                              //  ByteArray representation of the Zobrist hasher.

        this.TranspositionTableOffset = null;                       //  (Offset into Module memory.)
        this.TranspositionTableBuffer = null;                       //  ByteArray representation of the transposition table.

        this.negamaxSearchOffset = null;                            //  (Offset into Module memory.)
        this.negamaxSearchBuffer = null;                            //  ByteArray: Working node buffer for tree-search.

        this.negamaxMovesOffset = null;                             //  (Offset into Module memory.)
        this.negamaxMovesBuffer = null;                             //  ByteArray: Working move buffer for tree-search.

        this.negamaxKillerMovesOffset = null;                       //  (Offset into Module memory.)
        this.negamaxKillerMovesBuffer = null;                       //  ByteArray representation of the killer-moves table.

        this.negamaxHistoryHeuristicOffset = null;                  //  (Offset into Module memory.)
        this.negamaxHistoryHeuristicBuffer = null;                  //  ByteArray representation of the history-heuristic table.

        //////////////////////////////////////////////////////////////  Book lookup.
        this.bookLookup = new XMLHttpRequest();                     //  IE 7+, Firefox, Chrome, Opera, Safari.
        this.bookLookup.Parent = this;                              //  Add reference to the Player object containing this XMLHttpRequest.
        this.callOut = false;                                       //  True while AJAX call has yet to return.

        //////////////////////////////////////////////////////////////  Fetch, instantiate, and connect the Evaluation Module.
        fetch('obj/wasm/eval.wasm', {headers: {'Content-Type': 'application/wasm'} })
        .then(response => response.arrayBuffer())
        .then(bytes =>
          {
            WebAssembly.instantiate(bytes,
              {
                env: {
                       memoryBase: 0,
                       tableBase: 0,
                                                                    //  Malloc 2 pages for 71.8 KB file.
                       memory: new WebAssembly.Memory({initial: 2, maximum: 2}),
                       table: new WebAssembly.Table({initial: 0, element: 'anyfunc'})
                     }
              })
            .then(instance =>
              {
                this.evaluationEngine = instance;
                                                                    //  Assign offset to input-gamestate buffer.
                this.evaluationInputGameStateOffset = this.evaluationEngine.instance.exports.getInputGameStateBuffer();
                this.evaluationInputGameStateBuffer = new Uint8Array(this.evaluationEngine.instance.exports.memory.buffer, this.evaluationInputGameStateOffset, _GAMESTATE_BYTE_SIZE);
                                                                    //  Assign offset to input-move buffer.
                this.evaluationInputMoveOffset = this.evaluationEngine.instance.exports.getInputMoveBuffer();
                this.evaluationInputMoveBuffer = new Uint8Array(this.evaluationEngine.instance.exports.memory.buffer, this.evaluationInputMoveOffset, _MOVE_BYTE_SIZE);
                                                                    //  Assign offset to output-gamestate buffer.
                this.evaluationOutputGameStateOffset = this.evaluationEngine.instance.exports.getOutputGameStateBuffer();
                this.evaluationOutputGameStateBuffer = new Uint8Array(this.evaluationEngine.instance.exports.memory.buffer, this.evaluationOutputGameStateOffset, _GAMESTATE_BYTE_SIZE);
                                                                    //  Assign offset to output-moves buffer.
                this.evaluationOutputMovesOffset = this.evaluationEngine.instance.exports.getOutputMovesBuffer();
                this.evaluationOutputMovesBuffer = new Uint8Array(this.evaluationEngine.instance.exports.memory.buffer, this.evaluationOutputMovesOffset, _MAX_MOVES * (_MOVE_BYTE_SIZE + 5));

                elementsLoaded++;                                   //  Check evaluationEngine off our list.
                                                                    //  Load the tree-search module AFTER the evaluation module is complete.
                                                                    //  Fetch, instantiate, and connect the Negamax Module.
                fetch('obj/wasm/negamax.wasm', {headers: {'Content-Type': 'application/wasm'} })
                .then(response => response.arrayBuffer())
                .then(bytes =>
                  {
                    WebAssembly.instantiate(bytes,
                      {
                        env: {
                               memoryBase: 0,
                               tableBase: 0,
                                                                    //  Compiled with -s INITIAL_MEMORY=19005440 = 290 pages.
                               memory: new WebAssembly.Memory({initial: 290}),
                               table: new WebAssembly.Table({initial: 1, element: 'anyfunc'}),
                               _copyQuery2EvalGSInput: function()
                                 {
                                   var i;                           //  Copy negamaxEngine's query buffer contents
                                                                    //  to evaluationEngine's input-gamestate buffer.
                                   for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)
                                     this.evaluationInputGameStateBuffer[i] = this.negamaxQueryGameStateBuffer[i];
                                   return;
                                 }.bind(this),
                               _copyQuery2EvalMoveInput: function()
                                 {
                                   var i;                           //  Copy negamaxEngine's query buffer contents
                                                                    //  to evaluationEngine's input-move buffer.
                                   for(i = 0; i < _MOVE_BYTE_SIZE; i++)
                                     this.evaluationInputMoveBuffer[i] = this.negamaxQueryMoveBuffer[i];
                                   return;
                                 }.bind(this),
                               _copyEvalOutput2AnswerGSBuffer: function()
                                 {
                                   var i;                           //  Copy evaluationEngine's output buffer contents
                                                                    //  to negamaxEngine's answer-gamestate buffer.
                                   for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)
                                     this.negamaxAnswerGameStateBuffer[i] = this.evaluationOutputGameStateBuffer[i];
                                   return;
                                 }.bind(this),
                               _copyEvalOutput2AnswerMovesBuffer: function(len)
                                 {
                                   var i;
                                   const size = _MOVE_BYTE_SIZE + 5;//  Copy evaluationEngine's output buffer contents
                                   for(i = 0; i < len * size; i++)  //  to negamaxEngine's answer-moves buffer.
                                     this.negamaxAnswerMovesBuffer[i] = this.evaluationOutputMovesBuffer[i];
                                   return;
                                 }.bind(this),
                               _sideToMove: function()              //  Returns unsigned char.
                                 {
                                   return this.evaluationEngine.instance.exports.sideToMove_eval();
                                 }.bind(this),
                               _isTerminal: function()              //  Returns bool.
                                 {
                                   return this.evaluationEngine.instance.exports.isTerminal_eval();
                                 }.bind(this),
                               _isSideToMoveInCheck: function()     //  Returns bool.
                                 {
                                   return this.evaluationEngine.instance.exports.isSideToMoveInCheck_eval();
                                 }.bind(this),
                               _nonPawnMaterial: function()         //  Returns unsigned char.
                                 {
                                   return this.evaluationEngine.instance.exports.nonPawnMaterial_eval();
                                 }.bind(this),
                               _makeMove: function()
                                 {
                                   this.evaluationEngine.instance.exports.makeMove_eval();
                                   return;
                                 }.bind(this),
                               _makeNullMove: function()
                                 {
                                   this.evaluationEngine.instance.exports.makeNullMove_eval();
                                   return;
                                 }.bind(this),
                               _evaluate: function()                //  Returns float.
                                 {
                                   return this.evaluationEngine.instance.exports.evaluate_eval();
                                 }.bind(this),
                               _getMoves: function()                //  Returns unsigned int.
                                 {
                                   return this.evaluationEngine.instance.exports.getMoves_eval();
                                 }.bind(this)
                             }
                      })
                    .then(instance =>
                      {
                        var i;

                        this.negamaxEngine = instance;
                                                                    //  Assign offset to input buffer. This receives a game state as a byte array.
                        this.negamaxInputOffset = this.negamaxEngine.instance.exports.getInputBuffer();
                        this.negamaxInputBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxInputOffset, _GAMESTATE_BYTE_SIZE);
                                                                    //  Assign offset to parameters buffer.
                        this.negamaxParamsOffset = this.negamaxEngine.instance.exports.getParametersBuffer();
                        this.negamaxParamsBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxParamsOffset, _PARAMETER_ARRAY_SIZE);
                                                                    //  Assign offset to output buffer.
                                                                    //  This receives a game state, a 1-byte uchar (depth achieved), a move, a 4-byte float (score).
                        this.negamaxOutputOffset = this.negamaxEngine.instance.exports.getOutputBuffer();
                        this.negamaxOutputBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxOutputOffset, _GAMESTATE_BYTE_SIZE + 1 + _MOVE_BYTE_SIZE + 4);
                                                                    //  Assign offset to input buffer. This receives a game state as a byte array.
                        this.negamaxQueryGameStateOffset = this.negamaxEngine.instance.exports.getQueryGameStateBuffer();
                        this.negamaxQueryGameStateBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxQueryGameStateOffset, _GAMESTATE_BYTE_SIZE);
                                                                    //  Assign offset to input buffer. This receives a game state as a byte array.
                        this.negamaxQueryMoveOffset = this.negamaxEngine.instance.exports.getQueryMoveBuffer();
                        this.negamaxQueryMoveBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxQueryMoveOffset, _MOVE_BYTE_SIZE);
                                                                    //  Assign offset to auxiliary buffer.
                                                                    //  This is a receiving buffer, temporarily holding output from the evaluation module before converting these data to negamax nodes.
                        this.negamaxAnswerGameStateOffset = this.negamaxEngine.instance.exports.getAnswerGameStateBuffer();
                        this.negamaxAnswerGameStateBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxAnswerGameStateOffset, _GAMESTATE_BYTE_SIZE);
                                                                    //  Assign offset to auxiliary buffer.
                                                                    //  This is a receiving buffer, temporarily holding output from the evaluation module before converting these data to negamax nodes.
                        this.negamaxAnswerMovesOffset = this.negamaxEngine.instance.exports.getAnswerMovesBuffer();
                        this.negamaxAnswerMovesBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxAnswerMovesOffset, _MAX_MOVES * (_MOVE_BYTE_SIZE + 5));
                                                                    //  Assign offset to Zobrist hash buffer.
                                                                    //  This receives 8 * _ZHASH_TABLE_SIZE bytes. Sections of 8 bytes treated as unsigned long longs.
                        this.ZobristHashOffset = this.negamaxEngine.instance.exports.getZobristHashBuffer();
                        this.ZobristHashBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.ZobristHashOffset, _HASH_VALUE_BYTE_SIZE * _ZHASH_TABLE_SIZE);
                                                                    //  Assign offset to Transposition Table buffer.
                        this.TranspositionTableOffset = this.negamaxEngine.instance.exports.getTranspositionTableBuffer();
                        this.TranspositionTableBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.TranspositionTableOffset, 1 + _TRANSPO_TABLE_SIZE * _TRANSPO_RECORD_BYTE_SIZE);
                                                                    //  Assign offset to tree-search buffer.
                                                                    //  This is a working buffer that receives bytes from the evaluation engine.
                        this.negamaxSearchOffset = this.negamaxEngine.instance.exports.getNegamaxSearchBuffer();
                        this.negamaxSearchBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxSearchOffset, 4 + _TREE_SEARCH_ARRAY_SIZE * _NEGAMAX_NODE_BYTE_SIZE);
                                                                    //  Assign offset to tree-search buffer.
                                                                    //  This is a working buffer that receives bytes from the evaluation engine.
                        this.negamaxMovesOffset = this.negamaxEngine.instance.exports.getNegamaxMovesBuffer();
                        this.negamaxMovesBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxMovesOffset, 4 + _TREE_SEARCH_ARRAY_SIZE * _NEGAMAX_MOVE_BYTE_SIZE);
                                                                    //  Assign offset to auxiliary buffer.
                                                                    //  This is a working buffer that holds the killer-moves table.
                        this.negamaxKillerMovesOffset = this.negamaxEngine.instance.exports.getKillerMovesBuffer();
                        this.negamaxKillerMovesBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxKillerMovesOffset, _KILLER_MOVE_PER_PLY * 2 * _KILLER_MOVE_MAX_DEPTH);
                                                                    //  Assign offset to auxiliary buffer.
                                                                    //  This is a working buffer that holds the history-heuristic table.
                        this.negamaxHistoryHeuristicOffset = this.negamaxEngine.instance.exports.getHistoryTableBuffer();
                        this.negamaxHistoryHeuristicBuffer = new Uint8Array(this.negamaxEngine.instance.exports.memory.buffer, this.negamaxHistoryHeuristicOffset, 2 * (_NOTHING + _NOTHING));

                        this.TranspositionTableBuffer[0] = 1;       //  Set "generation byte" to 1.

                        elementsLoaded++;                           //  Check negaMaxEngine off our list.
                                                                    //  Load a Zobrist Hasher.
                        var ReqXML = new XMLHttpRequest();          //  IE 7+, Firefox, Chrome, Opera, Safari.
                        var params = 'sendRequest=eggsnhash';
                        ReqXML.parent = this;                       //  Add a reference to the Player object inside this callback function.

                        ReqXML.open("POST", 'obj/sess/reqzhash.php', true);
                        ReqXML.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
                        ReqXML.onreadystatechange = function()
                          {
                            if(ReqXML.readyState == 4 && ReqXML.status == 200)
                              {
                                if(ReqXML.responseText == "")       //  Null return: unknown error.
                                  {
                                    switch(currentLang)
                                      {
                                        case "Spanish": alert(alertStringScrub("Error"));  break;
                                        case "German": alert(alertStringScrub("Fehler"));  break;
                                        case "Polish": alert(alertStringScrub("B&#322;&#261;d na stronie"));  break;
                                        default: alert(alertStringScrub("Error"));
                                      }
                                  }
                                else
                                  {
                                    var parse = ReqXML.responseText.split('|');
                                    var arr;
                                    var i;
                                    if(parse[0] == 'capablancachess' && parse[1] == 'ok')
                                      {
                                        parse = parse[2].split(',');//  Repurpose "parse".
                                        console.log(parse[0] + ' Zobrist keys.');
                                                                    //  Load into Zobrist-hasher's buffer.
                                        for(i = 1; i < parse.length; i++)
                                          {
                                            arr = new Uint8Array(1);//  Force byte type.
                                            arr[0] = parseInt(parse[i]);
                                            this.parent.ZobristHashBuffer[i] = arr[0];
                                          }

                                        elementsLoaded++;           //  Check Zobrist hasher off our list.
                                        loadTotalReached();         //  Check the total.
                                      }
                                    else                            //  Error-label or garbage.
                                      {
                                        switch(currentLang)
                                          {
                                            case "Spanish": alert(alertStringScrub("Error"));  break;
                                            case "German": alert(alertStringScrub("Fehler"));  break;
                                            case "Polish": alert(alertStringScrub("B&#322;&#261;d na stronie"));  break;
                                            default: alert(alertStringScrub("Error"));
                                          }
                                      }
                                  }
                              }
                          };
                        ReqXML.send(params);
                      });
                  });
              });
          });
      }

    /* Called on every frame, except while animating, as long as the game is not over. */
    step()
      {
        var status, nodesSearched, depthAchieved, score;
        var elementToKeep;
        var i;

        //////////////////////////////////////////////////////////////  This A.I.'s turn.
        if(CurrentTurn == this.team)
          {
                                                                    //  Is there only one, current and correct branch?
            //if(this.branches.length == 1 && this.searchId == this.branches[ this.branchIterator ].id)
            if(this.branches.length == 1 && this.byteArrCmp(gameStateBuffer, this.branches[ this.branchIterator ].gamestate))
              {
                switch(this.branches[ this.branchIterator ].status)
                  {
                    case STATUS_UNSEARCHED:                         //  Nothing has been done with this branch yet.
                      this.lookup();                                //  First attempt a book lookup.
                      break;

                    case STATUS_BOOK_LOOKUP:                        //  A call is out.
                      break;

                    case STATUS_SEARCHING:                          //  Negamax pulse-search is underway.
                                                                    //  Is search necessary?
                      if(this.branches[ this.branchIterator ].depth < this.currentPly)
                        {
                                                                    //  Advance the algorithm.
                          this.negamaxEngine.instance.exports.negamax();
                                                                    //  Check the negamax engine's status.
                          status = this.negamaxEngine.instance.exports.getStatus();
                          if(status == NEGAMAX_STATUS_DONE)         //  Has pulse-negamax completed?
                            {
                              nodesSearched = this.negamaxEngine.instance.exports.getNodesSearched();
                              this.nodeCtr += nodesSearched;
                              updateNodeCounter(this.nodeCtr);
                              depthAchieved = this.negamaxEngine.instance.exports.finalDepthAchieved();
                              score = this.negamaxEngine.instance.exports.finalScore();
                              if(depthAchieved >= this.currentPly)
                                {
                                                                    //  Save the best move.
                                  for(i = 0; i < _MOVE_BYTE_SIZE; i++)
                                    this.branches[ this.branchIterator ].bestMove[i] = this.negamaxOutputBuffer[_GAMESTATE_BYTE_SIZE + 1 + i];
                                                                    //  Save the depth achieved.
                                  this.branches[ this.branchIterator ].depth = depthAchieved;
                                                                    //  Save the score.
                                  this.branches[ this.branchIterator ].score = score;
                                                                    //  Update the node count.
                                  this.branches[ this.branchIterator ].nodeCtr += nodesSearched;
                                                                    //  Reset the negamax module's node counter.
                                  this.negamaxEngine.instance.exports.resetNodesSearched();
                                                                    //  Update this branch's status.
                                  this.branches[ this.branchIterator ].status = STATUS_DONE_SEARCH;
                                }
                            }
                        }
                      else                                          //  Search at this.currentPly is not necessary.
                        this.branches[ this.branchIterator ].status = STATUS_DONE_SEARCH;

                      break;

                    case STATUS_DONE_SEARCH:                        //  Search for this branch at currentPly is done.
                      if(this.currentPly < this.maxPly && !this.branches[ this.branchIterator ].bookHit)
                        {                                           //  If we have not yet searched as deeply as we intend to search,
                          this.currentPly++;                        //  then increment the iterative depth.
                          this.branches[ this.branchIterator ].status = STATUS_SEARCHING;
                          this.initalizeSearch();                   //  Load the game state to which "branchIterator" currently points into Negamax Module.
                        }
                      else
                        {
                          Select_A = this.branches[ this.branchIterator ].bestMove[0];
                          Select_B = this.branches[ this.branchIterator ].bestMove[1];
                          PromotionTarget = this.branches[ this.branchIterator ].bestMove[2];

                          if(this.team == 'Black')                  //  A.I. moving, as black.
                            {
                                                                    //  Capture.
                              if(gameEngine.instance.exports.isWhite_client(Select_B))
                                animationInstruction = {a:Select_A, b:Select_B, promo:PromotionTarget, action:'die'};
                                                                    //  Castling.
                              else if(gameEngine.instance.exports.isCastle_client(Select_A, Select_B, PromotionTarget))
                                animationInstruction = {a:Select_A, b:Select_B, c:gameEngine.instance.exports.getRookSrcCastle_client(Select_A, Select_B), d:gameEngine.instance.exports.getRookDstCastle_client(Select_A, Select_B), action:'castle'};
                                                                    //  En passant capture.
                              else if(isEnPassantCapture(Select_A, Select_B))
                                animationInstruction = {a:Select_A, b:Select_B, action:'dieEnPassant'};
                                                                    //  Move.
                              else
                                animationInstruction = {a:Select_A, b:Select_B, promo:PromotionTarget, action:'move'};
                            }
                          else                                      //  A.I. moving, as white.
                            {
                                                                    //  Capture.
                              if(gameEngine.instance.exports.isBlack_client(Select_B))
                                animationInstruction = {a:Select_A, b:Select_B, promo:PromotionTarget, action:'die'};
                                                                    //  Castling.
                              else if(gameEngine.instance.exports.isCastle_client(Select_A, Select_B, PromotionTarget))
                                animationInstruction = {a:Select_A, b:Select_B, c:gameEngine.instance.exports.getRookSrcCastle_client(Select_A, Select_B), d:gameEngine.instance.exports.getRookDstCastle_client(Select_A, Select_B), action:'castle'};
                                                                    //  En passant capture.
                              else if(isEnPassantCapture(Select_A, Select_B))
                                animationInstruction = {a:Select_A, b:Select_B, action:'dieEnPassant'};
                                                                    //  Move.
                              else
                                animationInstruction = {a:Select_A, b:Select_B, promo:PromotionTarget, action:'move'};
                            }

                          this.branches = [];                       //  Empty the array.
                          animate();                                //  Cue the animation.
                        }
                      break;
                  }
              }
            else
              {
                if(this.branches.length == 0)                       //  No game state.
                  {
                    this.branches = [ {gamestate: new Uint8Array(_GAMESTATE_BYTE_SIZE),
                                       id:        this.searchId,
                                       bestMove:  [_NOTHING, _NOTHING, _NO_PROMO],
                                       depth:     0,
                                       status:    STATUS_UNSEARCHED,
                                       nodeCtr:   0,
                                       bookHit:   false,
                                       score:     0.0
                                      } ];
                    for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)
                      this.branches[ 0 ].gamestate[i] = gameStateBuffer[i];

                    this.branchIterator = 0;                        //  Point to the single branch.
                                                                    //  Rescind the stop order, if there was one.
                    this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_STOP_REQUESTED);
                                                                    //  Indicate that we are NOT "pondering"--we are SEARCHING.
                    this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_PONDERING);
                    this.initalizeSearch();                         //  Load this state into the search engine.
                  }
                else if(this.branches.length == 1)                  //  One WRONG game state.
                  {
                    console.log('It is the AI\'s turn, and we have a single WRONG game state.');
                    //  What's the negamax engine doing? Is it idle? Is something running? If something is running, send a halt signal.
                    //  get negamax engine status
                  }
                else                                                //  Several game states.
                  {
                    //console.log('It is the AI\'s turn, and we have SEVERAL game states.');
                                                                    //  What's the negamax engine doing? Is something running?
                                                                    //  If something is running, send a halt signal.
                    status = this.negamaxEngine.instance.exports.getStatus();
                    switch(status)
                      {
                        case NEGAMAX_STATUS_IDLE:                   //  Engine is idle.
                          i = 0;                                    //  Scan branches to see if the current state is in the list.
                          while(i < this.branches.length && !this.byteArrCmp(gameStateBuffer, this.branches[ i ].gamestate))
                            i++;
                          if(i < this.branches.length)              //  Cut array down to the current state, retain any accomplished planning.
                            {
                              console.log('It is the AI\'s turn, and we have FOUND the now-current game state among branches.');
                              elementToKeep = this.branches[i];
                              this.branches.splice(0, this.branches.length, elementToKeep);
                            }
                          else                                      //  Create a pseudo-branch for the current state. (Should never happen??)
                            {
                              console.log('It is the AI\'s turn, and we have NOT FOUND the now-current game state among branches. We made a new one.');
                              this.branches = [ {gamestate: new Uint8Array(_GAMESTATE_BYTE_SIZE),
                                                 id:        this.searchId,
                                                 bestMove:  [_NOTHING, _NOTHING, _NO_PROMO],
                                                 depth:     0,
                                                 status:    STATUS_UNSEARCHED,
                                                 nodeCtr:   0,
                                                 bookHit:   false,
                                                 score:     0.0
                                                } ];
                              for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)
                                this.branches[ 0 ].gamestate[i] = gameStateBuffer[i];
                            }
                          this.branchIterator = 0;                  //  Point to the single branch.
                                                                    //  Rescind the stop order, if there was one.
                          this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_STOP_REQUESTED);
                                                                    //  Indicate that we are NOT "pondering"--we are SEARCHING.
                          this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_PONDERING);
                          this.initalizeSearch();                   //  Load this state into the search engine.
                          break;

                        case NEGAMAX_STATUS_DONE:                   //  Engine is done.
                          //  Save work.
                          //  LEFT OFF HERE !!! *** SHOULD BE JUST LIKE BELOW

                          i = 0;                                    //  Scan branches to see if the current state is in the list.
                          while(i < this.branches.length && !this.byteArrCmp(gameStateBuffer, this.branches[ i ].gamestate))
                            i++;
                          if(i < this.branches.length)              //  Cut array down to the current state, retain any accomplished planning.
                            {
                              console.log('It is the AI\'s turn, and we have FOUND the now-current game state among branches.');
                              elementToKeep = this.branches[i];
                              this.branches.splice(0, this.branches.length, elementToKeep);
                            }
                          else                                      //  Create a pseudo-branch for the current state. (Should never happen??)
                            {
                              console.log('It is the AI\'s turn, and we have NOT FOUND the now-current game state among branches. We made a new one.');
                              this.branches = [ {gamestate: new Uint8Array(_GAMESTATE_BYTE_SIZE),
                                                 id:        this.searchId,
                                                 bestMove:  [_NOTHING, _NOTHING, _NO_PROMO],
                                                 depth:     0,
                                                 status:    STATUS_UNSEARCHED,
                                                 nodeCtr:   0,
                                                 bookHit:   false,
                                                 score:     0.0
                                                } ];
                              for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)
                                this.branches[ 0 ].gamestate[i] = gameStateBuffer[i];
                            }
                          this.branchIterator = 0;                  //  Point to the single branch.
                                                                    //  Rescind the stop order, if there was one.
                          this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_STOP_REQUESTED);
                                                                    //  Indicate that we are NOT "pondering"--we are SEARCHING.
                          this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_PONDERING);
                          this.initalizeSearch();                   //  Load this state into the search engine.
                          break;

                        case NEGAMAX_STATUS_RUNNING:                //  Engine is running.
                          console.log('It is the AI\'s turn, but the negamax engine is still running. Issue a halt signal.');
                                                                    //  Send a stop request.
                          this.negamaxEngine.instance.exports.setControlFlag(NEGAMAX_CTRL_STOP_REQUESTED);
                                                                    //  Continue pulsing until the engine can halt comfortably.
                          this.negamaxEngine.instance.exports.negamax();
                          break;

                        case NEGAMAX_STATUS_STOP_REQUESTED:         //  Engine has received a stop signal.
                        case NEGAMAX_STATUS_STOP_TIME:              //  Engine has received a stop signal owing to time.
                                                                    //  Continue pulsing until the engine can halt comfortably.
                          console.log('It is the AI\'s turn, and the negamax engine has received a halt signal.');
                          this.negamaxEngine.instance.exports.negamax();
                          break;

                        case NEGAMAX_STATUS_ABORTED:                //  Engine has aborted.
                        case NEGAMAX_STATUS_ERROR:                  //  Engine has encountered an error.
                          //  Save SOME OF the work.
                          //  LEFT OFF HERE !!! *** WHAT TO SAVE?????
                          //nodesSearched = this.negamaxEngine.instance.exports.getNodesSearched();
                          //this.nodeCtr += nodesSearched;
                          //this.branches[ this.branchIterator ].nodeCtr += nodesSearched;

                          console.log('It is the AI\'s turn, and the negamax engine has halted.');

                          i = 0;                                    //  Scan branches to see if the current state is in the list.
                          while(i < this.branches.length && !this.byteArrCmp(gameStateBuffer, this.branches[ i ].gamestate))
                            i++;
                          if(i < this.branches.length)              //  Cut array down to the current state, retain any accomplished planning.
                            {
                              console.log('It is the AI\'s turn, and we have FOUND the now-current game state among branches.');
                              elementToKeep = this.branches[i];
                              this.branches.splice(0, this.branches.length, elementToKeep);
                            }
                          else                                      //  Create a pseudo-branch for the current state. (Should never happen??)
                            {
                              console.log('It is the AI\'s turn, and we have NOT FOUND the now-current game state among branches. We made a new one.');
                              this.branches = [ {gamestate: new Uint8Array(_GAMESTATE_BYTE_SIZE),
                                                 id:        this.searchId,
                                                 bestMove:  [_NOTHING, _NOTHING, _NO_PROMO],
                                                 depth:     0,
                                                 status:    STATUS_UNSEARCHED,
                                                 nodeCtr:   0,
                                                 bookHit:   false,
                                                 score:     0.0
                                                } ];
                              for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)
                                this.branches[ 0 ].gamestate[i] = gameStateBuffer[i];
                            }
                          this.branchIterator = 0;                  //  Point to the single branch.
                                                                    //  Rescind the stop order, if there was one.
                          this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_STOP_REQUESTED);
                                                                    //  Indicate that we are NOT "pondering"--we are SEARCHING.
                          this.negamaxEngine.instance.exports.unsetControlFlag(NEGAMAX_CTRL_PONDERING);
                          this.initalizeSearch();                   //  Load this state into the search engine.
                          break;
                      }
                  }
              }
          }
        //////////////////////////////////////////////////////////////  Opponent's turn (this A.I. "ponders").
        else
          {
            if(this.branches.length == 0)                           //  Do branches exist?
              this.branch();                                        //  Fan out branches. Point this.branchIterator at 0-th element.
            else
              {
                switch(this.branches[this.branchIterator].status)
                  {
                    case STATUS_UNSEARCHED:                         //  Nothing has been done with this branch yet.
                      this.lookup();                                //  First attempt a book lookup.
                      break;

                    case STATUS_BOOK_LOOKUP:                        //  A call is out.
                      break;

                    case STATUS_SEARCHING:                          //  Negamax pulse-search is underway.
                                                                    //  Is search necessary?
                      if(this.branches[ this.branchIterator ].depth < this.currentPly)
                        {
                                                                    //  Advance the algorithm.
                          this.negamaxEngine.instance.exports.negamax();
                                                                    //  Check the negamax engine's status.
                          status = this.negamaxEngine.instance.exports.getStatus();

                          if(status == NEGAMAX_STATUS_DONE)         //  Has pulse-negamax completed?
                            {
                              nodesSearched = this.negamaxEngine.instance.exports.getNodesSearched();
                              this.nodeCtr += nodesSearched;
                              updateNodeCounter(this.nodeCtr);
                              depthAchieved = this.negamaxEngine.instance.exports.finalDepthAchieved();
                              score = this.negamaxEngine.instance.exports.finalScore();
                              if(depthAchieved >= this.currentPly)
                                {
                                                                    //  Save the best move.
                                  for(i = 0; i < _MOVE_BYTE_SIZE; i++)
                                    this.branches[ this.branchIterator ].bestMove[i] = this.negamaxOutputBuffer[_GAMESTATE_BYTE_SIZE + 1 + i];
                                                                    //  Save the depth achieved.
                                  this.branches[ this.branchIterator ].depth = depthAchieved;
                                                                    //  Save the score.
                                  this.branches[ this.branchIterator ].score = score;
                                                                    //  Update the node count.
                                  this.branches[ this.branchIterator ].nodeCtr += nodesSearched;
                                                                    //  Reset the negamax module's node counter.
                                  this.negamaxEngine.instance.exports.resetNodesSearched();
                                                                    //  Update this branch's status.
                                  this.branches[ this.branchIterator ].status = STATUS_DONE_SEARCH;
                                }
                            }
                        }
                      else                                          //  Search at this.currentPly is not necessary.
                        this.branches[ this.branchIterator ].status = STATUS_DONE_SEARCH;

                      break;

                    case STATUS_DONE_SEARCH:                        //  Search for this branch at currentPly is done.
                      this.branchIterator++;                        //  Iterate (or loop around) to the next.
                      if(this.branchIterator == this.branches.length)
                        {
                          this.branchIterator = 0;                  //  Loop around to the beginning of this.branches.

                          if(this.currentPly < this.maxPly)         //  If we have not yet searched as deeply as we intend to search,
                            {                                       //  then increment the iterative depth.
                              this.currentPly++;
                                                                    //  Set everything that was not a book hit and which is less than the new
                                                                    //  target depth to be searched again.
                              for(i = 0; i < this.branches.length; i++)
                                {
                                  if(this.branches[ i ].bookHit == false && this.branches[ i ].depth < this.currentPly)
                                    this.branches[ i ].status = STATUS_SEARCHING;
                                }
                            }
                        }

                      this.initalizeSearch();                       //  Load the game state to which "branchIterator" currently points into Negamax Module.

                      break;
                  }
              }
          }
      }

    /* Collect all moves the opponent might make, and prepare for each, an Object for the negamax search routine. */
    branch()
      {
        var i, j, len;
        var moves;
        var buffer4 = new Uint8Array(4);

        for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                   //  Copy the current game-state byte-array to the player's evaluation engine's input buffer.
          this.evaluationInputGameStateBuffer[i] = gameStateBuffer[i];
                                                                    //  Tell the evaluation engine to get a list of possible moves.
        len = this.evaluationEngine.instance.exports.getMoves_eval();

        moves = [];                                                 //  Parse encoded moves and metadata.
        for(i = 0; i < len; i++)
          {
            moves.push( {move: new Uint8Array(_MOVE_BYTE_SIZE), score: 0} );
            for(j = 0; j < _MOVE_BYTE_SIZE; j++)
              moves[moves.length - 1].move[j] = this.evaluationOutputMovesBuffer[i * (_MOVE_BYTE_SIZE + 5) + j];
            for(j = 0; j < 4; j++)
              buffer4[j] = this.evaluationOutputMovesBuffer[i * (_MOVE_BYTE_SIZE + 5) + _MOVE_BYTE_SIZE + j];
            const dv = new DataView(buffer4.buffer, buffer4.byteOffset, 4);
            moves[moves.length - 1].score = dv.getUint32(0, true);  //  Little-endian.
          }

        moves.sort((a, b) => b.score - a.score);                    //  Sort descending by score.

        this.branches = [];                                         //  Reset the array.
        for(i = 0; i < len; i++)                                    //  Transfer results from the evaluation engine to the array of objects.
          {
                                                                    //  Game state.
                                                                    //  Best move found for this state.
                                                                    //  Depth searched/achieved.
                                                                    //  Whether we already tried the DB lookup.
            this.branches.push( {gamestate: new Uint8Array(_GAMESTATE_BYTE_SIZE),
                                 id:        this.searchId,
                                 bestMove:  [_NOTHING, _NOTHING, _NO_PROMO],
                                 depth:     0,
                                 status:    STATUS_UNSEARCHED,
                                 nodeCtr:   0,
                                 bookHit:   false,
                                 score:     0.0} );

            for(j = 0; j < _MOVE_BYTE_SIZE; j++)                    //  Copy the candidate move to the evaluation module's move-input.
              this.evaluationInputMoveBuffer[j] = moves[i].move[j];

            this.evaluationEngine.instance.exports.makeMove_eval(); //  Apply the candidate move to the game state still in Evaluation's input buffer.

            for(j = 0; j < _GAMESTATE_BYTE_SIZE; j++)               //  Copy the encoded, resultant game state from Evaluation's output to the branch object.
              this.branches[this.branches.length - 1].gamestate[j] = this.evaluationOutputGameStateBuffer[j];

            this.searchId++;                                        //  Increment ID.
          }

        this.branchIterator = 0;                                    //  Reset the branch iterator, point to the first (assumed best) move.
        this.currentPly = 1;                                        //  Reset iterative deepening to 1.
        this.negamaxEngine.instance.exports.resetNodesSearched();   //  Reset the number of nodes counted.
                                                                    //  Indicate that we are "pondering".
        this.negamaxEngine.instance.exports.setControlFlag(NEGAMAX_CTRL_PONDERING);

        return;
      }

    /* Attempt to look up an advantageous move to make for the game state currently held as a byte array
       in "this.branches[ this.branchIterator ].gamestate". */
    lookup()
      {
        if(!this.callOut)
          {
            var params = 'sendRequest=pluto';
            var i;
            for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)
              params += '&b'+i+'='+this.branches[ this.branchIterator ].gamestate[i];

            this.bookLookup.open("POST", 'obj/sess/lookup.php', true);
            this.bookLookup.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
            this.bookLookup.setRequestHeader("Cache-Control", "no-cache");
            this.bookLookup.onreadystatechange = function()
              {
                var parse;
                var i;

                if(this.readyState == 4 && this.status == 200)
                  {
                    if(this.responseText == "")                     //  Null return: unknown error. Whatever; proceed with search.
                      {
                        this.Parent.initalizeSearch();              //  Set this branch as root for the Negamax Module.
                        this.Parent.branches[ this.branchIterator ].status = STATUS_SEARCHING;
                      }
                    else
                      {
                        parse = this.responseText.split('|');

                        if(parse[0] == 'capablancachess')           //  Contact!
                          {
                            if(parse[1] == 'found')                 //  Found!
                              {
                                parse = parse[2].split(',');        //  Repurpose "parse".
                                for(i = 0; i < parse.length; i++)   //  Load lookup result.
                                  this.Parent.branches[ this.Parent.branchIterator ].bestMove[i] = parseInt(parse[i]);
                                                                    //  Book move was found.
                                this.Parent.branches[ this.Parent.branchIterator ].bookHit = true;
                                this.Parent.branches[ this.Parent.branchIterator ].status = STATUS_DONE_SEARCH;
                              }
                            else                                    //  "failed", "notfound", or "error". Whatever; proceed with search.
                              {
                                this.Parent.initalizeSearch();      //  Set this branch as root for the Negamax Module.
                                this.Parent.branches[ this.Parent.branchIterator ].status = STATUS_SEARCHING;
                              }
                          }
                        else                                        //  Contact fail or garbage. Whatever; proceed with search.
                          {
                            this.Parent.initalizeSearch();          //  Set this branch as root for the Negamax Module.
                            this.Parent.branches[ this.Parent.branchIterator ].status = STATUS_SEARCHING;
                          }
                      }

                    this.Parent.callOut = false;
                  }
              };
                                                                    //  Set the call-is-out flag.
            this.callOut = true;                                    //  Set status of this branch to indicate that lookup is underway.
            this.branches[ this.branchIterator ].status = STATUS_BOOK_LOOKUP;
            this.bookLookup.send(params);                           //  Send the request.
          }

        return;
      }

    /* Set up the search routine for the game state in this.branches[ this.branchIterator ]. */
    initalizeSearch()
      {
        var i;
                                                                    //  First, copy the byte array from this.branches[ this.branchIterator ]
        for(i = 0; i < _GAMESTATE_BYTE_SIZE; i++)                   //  to "this.negamaxInputBuffer".
          this.negamaxInputBuffer[i] = this.branches[ this.branchIterator ].gamestate[i];
                                                                    //  Set this search's ID.
        this.negamaxEngine.instance.exports.setSearchId( this.branches[ this.branchIterator ].id );
        this.negamaxEngine.instance.exports.setControlFlag(0);      //  Blank out control flags.
                                                                    //  Set the target depth.
        this.negamaxEngine.instance.exports.setTargetDepth( this.currentPly );
        this.negamaxEngine.instance.exports.initSearch();

        return;
      }

    /* Compare two byte arrays and determine whether they are equal, byte for byte. */
    byteArrCmp(a, b)
      {
        var i;

        i = 0;
        while(i < _GAMESTATE_BYTE_SIZE && a[i] == b[i])
          i++;

        return (i == _GAMESTATE_BYTE_SIZE);
      }

    /*
    someOtherFunctionOfThisClass()
      {
      }
    */
  }