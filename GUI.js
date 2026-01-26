var currentLang = "English";

var lang_en = document.getElementById("lang_en"); ////////////////////  Language selection buttons
var lang_pl = document.getElementById("lang_pl");
var lang_es = document.getElementById("lang_es");
var lang_de = document.getElementById("lang_de");

var techDetails_en; //////////////////////////////////////////////////  Load from file: tech notes about this page
var techDetails_pl;
var techDetails_es;
var techDetails_de;

var gui_en; //////////////////////////////////////////////////////////  Load from file: GUI interface and default settings
var gui_pl;
var gui_es;
var gui_de;

//////////////////////////////////////////////////////////////////////  Interface controls
var NodeCtr = 0;                                                    //  Nodes searched in the previous script call
var thoughtStaged = false;                                          //  Whether or not the Artwork For Thinking SHOULD be visible
var countStaged = false;                                            //  Whether or not the Node Counter SHOULD be visible

var angle = 36;                                                     //  Current camera angle (36 looks nice, let's start with that)
var fullscreenAvailable = THREEx.FullScreen.available();            //  Whether fullscreen is available on this device
var fullscreenActive = false;                                       //  Whether fullscreen is currently active
var showParticles = true;                                           //  Toggle the particle effect

var clockgame = false;                                              //  Whether to use the game clock
var whiteTimeMin = 60;                                              //  Time in minutes to each side
var blackTimeMin = 60;
var whiteTimeSec = 0;                                               //  Seconds remaining to each side
var blackTimeSec = 0;

var panelOpen = false;                                              //  Whether the central panel is open

//////////////////////////////////////////////////////////////////////  HUD / DOCKED-HUD
var hud;                                                            //  CSS, created and destroyed
var dockedhud = document.getElementById('dockedhud');               //  HTML, hidden and revealed
const _NO_PROMO = 0;
const _PROMO_KNIGHT = 1;
const _PROMO_BISHOP = 2;
const _PROMO_ROOK = 3;
const _PROMO_ARCHBISHOP = 4;
const _PROMO_CHANCELLOR = 5;
const _PROMO_QUEEN = 6;

//////////////////////////////////////////////////////////////////////
//   I N I T s
function initGUI()
  {
    lang_en.addEventListener('click', function() { setlang('English') } );
    lang_pl.addEventListener('click', function() { setlang('Polish') } );
    lang_es.addEventListener('click', function() { setlang('Spanish') } );
    lang_de.addEventListener('click', function() { setlang('German') } );

    loadTechDetails();                                              //  Load tech-details

    loadGUIXML();                                                   //  Load GUI

    loadWebASM();                                                   //  Load engines
  }

//  Create the floating, dragable element
function initHUD()
  {
    hud = document.createElement('hud');
    hud.setAttribute('draggable', 'true');
    hud.setAttribute('id', 'draghud');
  }

//  Add HUD to the document body.
//  If we are in mobile view, then this element has display property = hidden,
//  and the dockedhud contains the actual buttons. In either case, leave that to CSS.
//  The function updateHUD() handles both the actual HUD and the dockedHUD content.
function addHUD()
  {
    document.body.appendChild(hud);
    updateHUD();

    hud.addEventListener('dragstart', dragStart, false);            //  If we're on a small screen,
    hud.addEventListener('touchmove', touchDrag, false);            //  then allow this anyway: it
    document.body.addEventListener('dragover', dragOver, false);    //  simply won't appear.
    document.body.addEventListener('drop', dragStop, false);
                                                                    //  Small screen: pop panel for promo.
    if(getComputedStyle(document.getElementById('dockedhud'), null).display != "none")
      popPanel();
  }

//  Load all techDetails varialbes
function loadTechDetails()
  {
    var TechXML_en = new XMLHttpRequest();                          //  IE 7+, Firefox, Chrome, Opera, Safari
    TechXML_en.open("GET", 'obj/xml/about_en.xml', true);
    TechXML_en.onreadystatechange = function()
      {
        if(TechXML_en.readyState == 4 && TechXML_en.status == 200)
          {
            techDetails_en = TechXML_en.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    TechXML_en.send();

    var TechXML_pl = new XMLHttpRequest();                          //  IE 7+, Firefox, Chrome, Opera, Safari
    TechXML_pl.open("GET", 'obj/xml/about_pl.xml', true);
    TechXML_pl.onreadystatechange = function()
      {
        if(TechXML_pl.readyState == 4 && TechXML_pl.status == 200)
          {
            techDetails_pl = TechXML_pl.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    TechXML_pl.send();

    var TechXML_es = new XMLHttpRequest();                          //  IE 7+, Firefox, Chrome, Opera, Safari
    TechXML_es.open("GET", 'obj/xml/about_es.xml', true);
    TechXML_es.onreadystatechange = function()
      {
        if(TechXML_es.readyState == 4 && TechXML_es.status == 200)
          {
            techDetails_es = TechXML_es.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    TechXML_es.send();

    var TechXML_de = new XMLHttpRequest();                          //  IE 7+, Firefox, Chrome, Opera, Safari
    TechXML_de.open("GET", 'obj/xml/about_de.xml', true);
    TechXML_de.onreadystatechange = function()
      {
        if(TechXML_de.readyState == 4 && TechXML_de.status == 200)
          {
            techDetails_de = TechXML_de.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    TechXML_de.send();
  }

//  Load all control panels
function loadGUIXML()
  {
    var GUIXML_en = new XMLHttpRequest();                           //  IE 7+, Firefox, Chrome, Opera, Safari
    GUIXML_en.open("GET", 'obj/xml/gui_en.xml', true);
    GUIXML_en.onreadystatechange = function()
      {
        if(GUIXML_en.readyState == 4 && GUIXML_en.status == 200)
          {
            gui_en = GUIXML_en.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    GUIXML_en.send();

    var GUIXML_pl = new XMLHttpRequest();                           //  IE 7+, Firefox, Chrome, Opera, Safari
    GUIXML_pl.open("GET", 'obj/xml/gui_pl.xml', true);
    GUIXML_pl.onreadystatechange = function()
      {
        if(GUIXML_pl.readyState == 4 && GUIXML_pl.status == 200)
          {
            gui_pl = GUIXML_pl.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    GUIXML_pl.send();

    var GUIXML_es = new XMLHttpRequest();                           //  IE 7+, Firefox, Chrome, Opera, Safari
    GUIXML_es.open("GET", 'obj/xml/gui_es.xml', true);
    GUIXML_es.onreadystatechange = function()
      {
        if(GUIXML_es.readyState == 4 && GUIXML_es.status == 200)
          {
            gui_es = GUIXML_es.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    GUIXML_es.send();

    var GUIXML_de = new XMLHttpRequest();                           //  IE 7+, Firefox, Chrome, Opera, Safari
    GUIXML_de.open("GET", 'obj/xml/gui_de.xml', true);
    GUIXML_de.onreadystatechange = function()
      {
        if(GUIXML_de.readyState == 4 && GUIXML_de.status == 200)
          {
            gui_de = GUIXML_de.responseText;
            elementsLoaded++;
            loadTotalReached();
          }
      };
    GUIXML_de.send();
  }

//  Load compiled programs.
function loadWebASM()
  {
                                                                    //  Load GameLogic.
    fetch('obj/wasm/gamelogic.wasm', {headers: {'Content-Type': 'application/wasm'} })
    .then(response => response.arrayBuffer())
    .then(bytes =>
      {
        WebAssembly.instantiate(bytes,
          {
            env: {
                   memoryBase: 0,
                   tableBase: 0,
                                                                    //  Malloc 1 page for 28.3 KB file.
                   memory: new WebAssembly.Memory({initial: 1, maximum: 1}),
                   table: new WebAssembly.Table({initial: 0, element: 'anyfunc'}),
                   _printRow: function(a, b, c, d, e, f, g, h, i, j)//  (Fantastically stupid.)
                     {
                       const _EMPTY            = 0x00;              //  (See C code.)
                       const _WHITE_PAWN       = 0x01;
                       const _WHITE_KNIGHT     = 0x02;
                       const _WHITE_BISHOP     = 0x03;
                       const _WHITE_ROOK       = 0x04;
                       const _WHITE_ARCHBISHOP = 0x05;
                       const _WHITE_CHANCELLOR = 0x06;
                       const _WHITE_QUEEN      = 0x07;
                       const _WHITE_KING       = 0x08;

                       const _BLACK_PAWN       = 0x09;
                       const _BLACK_KNIGHT     = 0x0A;
                       const _BLACK_BISHOP     = 0x0B;
                       const _BLACK_ROOK       = 0x0C;
                       const _BLACK_ARCHBISHOP = 0x0D;
                       const _BLACK_CHANCELLOR = 0x0E;
                       const _BLACK_QUEEN      = 0x0F;
                       const _BLACK_KING       = 0x10;
                       var consoleStr = '';

                       switch(a)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(b)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(c)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(d)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(e)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(f)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(g)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(h)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(i)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }
                       switch(j)
                         {
                           case _EMPTY:            consoleStr += '. ';  break;
                           case _WHITE_PAWN:       consoleStr += 'P ';  break;
                           case _WHITE_KNIGHT:     consoleStr += 'N ';  break;
                           case _WHITE_BISHOP:     consoleStr += 'B ';  break;
                           case _WHITE_ROOK:       consoleStr += 'R ';  break;
                           case _WHITE_ARCHBISHIP: consoleStr += 'A ';  break;
                           case _WHITE_CHANCELLOR: consoleStr += 'C ';  break;
                           case _WHITE_QUEEN:      consoleStr += 'Q ';  break;
                           case _WHITE_KING:       consoleStr += 'K ';  break;
                           case _BLACK_PAWN:       consoleStr += 'p ';  break;
                           case _BLACK_KNIGHT:     consoleStr += 'n ';  break;
                           case _BLACK_BISHOP:     consoleStr += 'b ';  break;
                           case _BLACK_ROOK:       consoleStr += 'r ';  break;
                           case _BLACK_ARCHBISHOP: consoleStr += 'a ';  break;
                           case _BLACK_CHANCELLOR: consoleStr += 'c ';  break;
                           case _BLACK_QUEEN:      consoleStr += 'q ';  break;
                           case _BLACK_KING:       consoleStr += 'k ';  break;
                         }

                       console.log(consoleStr);
                     },
                   _printGameStateData: function(wToMove, wKingside, wQueenside, wCastled,
                                                          bKingside, bQueenside, bCastled,
                                                 previousDoubleMoveColumn, moveCtr)
                     {
                       var castlingString;

                       if(wToMove)
                         console.log('White to move.');
                       else
                         console.log('Black to move.');

                       if(wCastled)
                         console.log('White has castled.');
                       else
                         {
                           castlingString = 'White has ';
                           if(wKingside && wQueenside)
                             castlingString += 'kingside and queenside';
                           else if(wKingside && !wQueenside)
                             castlingString += 'kingside';
                           else if(!wKingside && wQueenside)
                             castlingString += 'queenside';
                           else
                             castlingString += 'no';
                           castlingString += ' castling privleges.';
                           console.log(castlingString);
                         }

                       if(bCastled)
                         console.log('Black has castled.');
                       else
                         {
                           castlingString = 'Black has ';
                           if(bKingside && bQueenside)
                             castlingString += 'kingside and queenside';
                           else if(bKingside && !bQueenside)
                             castlingString += 'kingside';
                           else if(!bKingside && bQueenside)
                             castlingString += 'queenside';
                           else
                             castlingString += 'no';
                           castlingString += ' castling privleges.';
                           console.log(castlingString);
                         }

                       if(previousDoubleMoveColumn > 0)
                         console.log('Previous pawn double-move on column ' + previousDoubleMoveColumn);

                       console.log('Move counter: ' + moveCtr);
                     }
                 }
          })
        .then(instance =>
          {
            gameEngine = instance;
                                                                    //  Assign offset.
            gameStateOffset = gameEngine.instance.exports.getCurrentState();
            gameStateBuffer = new Uint8Array(gameEngine.instance.exports.memory.buffer, gameStateOffset, _GAMESTATE_BYTE_SIZE);
                                                                    //  Assign offset.
            gameOutputOffset = gameEngine.instance.exports.getMovesBuffer();
            gameOutputBuffer = new Uint8Array(gameEngine.instance.exports.memory.buffer, gameOutputOffset, _MOVEBUFFER_BYTE_SIZE);

            elementsLoaded++;                                       //  Check this load off our list.
            loadTotalReached();                                     //  Check the total.
          });
      });
  }

//////////////////////////////////////////////////////////////////////
//   H U D    C O N T E N T
//  Adjust content of floating, dragable element according to game states and variables
function updateHUD()
  {
    var hudstr = '<table style="table-layout: auto;">';
    var dockedstr = '<p>';

    if(pluto.team == 'Black')
      {
        hudstr += allPromotablePiecesHUD('white');
        dockedstr += allPromotablePiecesDockedHUD('white');
      }
    else
      {
        hudstr += allPromotablePiecesHUD('black');
        dockedstr += allPromotablePiecesDockedHUD('black');
      }

    hudstr += '</table>';
    dockedstr += '</p>';

    hud.innerHTML = hudstr;
    dockedhud.innerHTML = dockedstr
  }

//  In Chess, a pawn can promote to any piece of the same color:
//  so it is legal for either side to have two queens, more than two rooks,
//  three bishops, etc.
function allPromotablePiecesHUD(team)
  {
    var str;
    str  = '<tr><td><a href="javascript:;" onClick="choosePromo(_PROMO_KNIGHT);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/knight.jpg"/></a></td></tr>';
    str += '<tr><td><a href="javascript:;" onClick="choosePromo(_PROMO_BISHOP);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/bishop.jpg"/></a></td></tr>';
    str += '<tr><td><a href="javascript:;" onClick="choosePromo(_PROMO_ROOK);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/rook.jpg"/></a></td></tr>';
    str += '<tr><td><a href="javascript:;" onClick="choosePromo(_PROMO_ARCHBISHOP);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/archbishop.jpg"/></a></td></tr>';
    str += '<tr><td><a href="javascript:;" onClick="choosePromo(_PROMO_CHANCELOR);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/chancellor.jpg"/></a></td></tr>';
    str += '<tr><td><a href="javascript:;" onClick="choosePromo(_PROMO_QUEEN);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/queen.jpg"/></a></td></tr>';
    return str;
  }

//  Same thing, but the docked version.
function allPromotablePiecesDockedHUD(team)
  {
    var str;
    str  = '<a href="javascript:;" onClick="hidePanel(); choosePromo(_PROMO_KNIGHT);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/knight.jpg"/></a>';
    str += '<a href="javascript:;" onClick="hidePanel(); choosePromo(_PROMO_BISHOP);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/bishop.jpg"/></a>';
    str += '<a href="javascript:;" onClick="hidePanel(); choosePromo(_PROMO_ROOK);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/rook.jpg"/></a>';
    str += '<a href="javascript:;" onClick="hidePanel(); choosePromo(_PROMO_ARCHBISHOP);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/archbishop.jpg"/></a>';
    str += '<a href="javascript:;" onClick="hidePanel(); choosePromo(_PROMO_CHANCELLOR);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/chancellor.jpg"/></a>';
    str += '<a href="javascript:;" onClick="hidePanel(); choosePromo(_PROMO_QUEEN);"><img class="gamesettingbutton" src="https://www.ericjoycefilm.com/wastesoftime/boardgames/capablancachess/obj/img/hud/' + team + '/queen.jpg"/></a>';
    return str;
  }

function choosePromo(p)
  {
    promote_mp3.play();                                             //  Play the sound.
    PromotionTarget = p;
    removeHUD();                                                    //  Remove the floating HUD.
    blankOutDockedHUD();                                            //  Blank out the Docked HUD.
    promoteHuman(Select_A, Select_B, p);                            //  Perform the promotion animation.
  }

//////////////////////////////////////////////////////////////////////
//   H U D    E V E N T S
function dragStart(e)
  {
    var style = window.getComputedStyle(e.target, null);
    e.dataTransfer.setData("text/plain", (parseInt(style.getPropertyValue("left"), 10) - e.clientX)
                                 + ',' + (parseInt(style.getPropertyValue("top"), 10) - e.clientY));
  }

function dragOver(e)
  {
    e.preventDefault();
    return false;
  }

function dragStop(e)
  {
    var offset = e.dataTransfer.getData("text/plain").split(',');
    var dragable = document.getElementById('draghud');
    dragable.style.left = (e.clientX + parseInt(offset[0], 10)) + 'px';
    dragable.style.top  = (e.clientY + parseInt(offset[1], 10)) + 'px';
    e.preventDefault();
    return false;
  }

function touchDrag(e)
  {
    var touch = e.targetTouches[0];
    var dragable = document.getElementById('draghud');
    draggable.style.left = touch.pageX - 25 + 'px';
    draggable.style.top = touch.pageY - 25 + 'px';
    e.preventDefault();
  }

function removeHUD()
  {
    hud.removeEventListener('dragstart', dragStart);
    document.body.removeEventListener('dragover', dragOver);
    document.body.removeEventListener('drop', dragStop);
    document.body.removeChild(hud);
  }

function blankOutDockedHUD()
  {
    dockedhud.innerHTML = '';
  }

//////////////////////////////////////////////////////////////////////
//   B U T T O N    E V E N T S

//  Change camera position and rotation according to this angle.
function updateAngle()
  {
    var x = parseInt(document.getElementById('angle-slider').value);

    document.getElementById('angle-num').innerHTML = x;
    resetCameraPositionAngle(x);
  }

//  Toggle fullscreen
function updateFullscreenToggle()
  {
    fullscreenActive = !fullscreenActive;
    toggleFullscreen(fullscreenActive);
  }

//  Toggle particles
function updateParticleToggle()
  {
    showParticles = !showParticles;
    toggleParticleEffect(showParticles);
  }

//  Update search depth
function updatePlies()
  {
    var x = parseInt(document.getElementById('plies-slider').value);
    document.getElementById('plies-num').innerHTML = x;

    pluto.maxPly = x;                                               //  Change the number of ply.
  }

//  Toggle A.I. side to play
function updateAIPlaysBlack()
  {
    var i;

    if(!gameStarted)
      {
        switch_mp3.play();                                          //  Play the sound effect.

        if(pluto.team == 'Black')
          {
            pluto.team = 'White';
            MasterControl = false;
            HumansTurn = false;

            directionalLight1.position.set(-1, 1, 1).normalize();
            directionalLight2.position.set(-1, -1, -1).normalize();

            camera.position.set(CAMERA_X, CAMERA_Y, -CAMERA_Z);
            camera.rotation.set(0, 0, Math.PI);

            for(i = 0; i < gamePieces.length; i++)
              {
                if(gamePieces[i].chessrank == "Bishop")
                  gamePieces[i].rotation.y = Math.PI;
              }
          }
        /*
        else
          {
            pluto.team = 'Black';

            directionalLight1.position.set(1, -1, 1).normalize();
            directionalLight2.position.set(1, 1, -1).normalize();

            camera.position.set(CAMERA_X, CAMERA_Y, CAMERA_Z);
            camera.rotation.set(0, 0, 0);

            for(i = 0; i < gamePieces.length; i++)
              {
                if(gamePieces[i].chessrank == "Bishop")
                  gamePieces[i].rotation.y = 0;
              }
          }
        */
        resetCameraPositionAngle(angle);                            //  Force redraw

        pullGUIComponents();                                        //  Cue the A.I. to make the first move
                                                                    //  It now becomes the A.I.'s turn!
        artworkForThinking(true);                                   //  Show the "thinking" artwork.
        updateNodeCounter(pluto.nodeCtr);                           //  Show the A.I.'s node count.
        nodeCounter(true);                                          //  Show the node counter.
      }
  }

//  Enable/Disable time control.
function updateTimeControl()
  {
    var whiteClock = document.getElementById('white-clock');
    var blackClock = document.getElementById('black-clock');
    var whiteClockDiv = document.getElementById('white-clock-div');
    var blackClockDiv = document.getElementById('black-clock-div');

    if(!gameStarted)
      {
        clockgame = !clockgame;

        if(clockgame)
          {
            whiteClock.style.visibility = 'visible';
            blackClock.style.visibility = 'visible';
            if(!isNaN(whiteTimeMin))
              whiteClockDiv.innerHTML = whiteTimeMin + ':00';
            if(!isNaN(blackTimeMin))
              blackClockDiv.innerHTML = blackTimeMin + ':00';
            whiteClockDiv.style.visibility = 'visible';
            blackClockDiv.style.visibility = 'visible';
          }
        else
          {
            whiteClock.style.visibility = 'hidden';
            blackClock.style.visibility = 'hidden';
            whiteClockDiv.innerHTML = '';
            blackClockDiv.innerHTML = '';
            whiteClockDiv.style.visibility = 'hidden';
            blackClockDiv.style.visibility = 'hidden';
          }
      }
  }

//  Update minutes allotted to each side.
function updateClockTimeAllotted()
  {
    var whiteClock = document.getElementById('white-clock-div');
    var blackClock = document.getElementById('black-clock-div');
    var x = parseInt(document.getElementById('minutesallocated-input').value);

    if(!gameStarted)
      {
        if(!isNaN(x))
          {
            whiteTimeMin = blackTimeMin = x;

            whiteClock.innerHTML = whiteTimeMin + ':00';
            blackClock.innerHTML = blackTimeMin + ':00';
          }
      }
  }

//  Commit to some settings and pull them from the control panel
function pullGUIComponents()
  {
    gameStarted = true;                                             //  The game has officially begun.
                                                                    //  Commit to the choices made for play and pull their controls from the control panel.
    document.getElementById('switch-sides-tr').style.display = 'none';
    document.getElementById('AIblack').removeAttribute('onclick');

    document.getElementById('timecontrol-label').style.display = 'none';
    document.getElementById('useclock-label').style.display = 'none';
    document.getElementById('timecontrol').style.display = 'none';
    document.getElementById('timecontrol').removeAttribute('onclick');

    document.getElementById('minutesallocated-label').style.display = 'none';
    document.getElementById('minutesallocated-input').style.display = 'none';
    document.getElementById('minutesallocated-input').removeAttribute('oninput');
  }

//  Open the central panel.
function popPanel()
  {
    document.getElementById('panel-content').classList.remove('collapsed-panel');
    document.getElementById('centralpanel').classList.add('popped-panel');
    setPanelToggleReveal(false);
    panelOpen = true;
  }

//  Close the central panel.
function hidePanel()
  {
    document.getElementById('centralpanel').classList.remove('popped-panel');
    document.getElementById('panel-content').classList.add('collapsed-panel');
    setPanelToggleReveal(true);
    panelOpen = false;
  }

//  Call setPanelToggleReveal(true) to make the button reveal the panel.
//  Call setPanelToggleReveal(false) to make the button collapse the panel.
function setPanelToggleReveal(b)
  {
    var t = document.getElementById('panel-toggle');

    if(b)                                                           //  Arrow pointing up is &#9650;
      t.innerHTML = '<a href="javascript:void(0);" onclick="popPanel();">&#9650;</a>';
    else                                                            //  Arrow pointing down is &#9660;
      t.innerHTML = '<a href="javascript:void(0);" onclick="hidePanel();">&#9660;</a>';
  }

function setlang(lang)
  {
    switch(lang)
      {
        case 'Polish':
             currentLang = "Polish";
             break;
        case 'Spanish':
             currentLang = "Spanish";
             break;
        case 'German':
             currentLang = "German";
             break;
        default:
             currentLang = "English";
             break;
      }
    updateGUIlabels();
  }

function updateGUIlabels()
  {
    switch(currentLang)
      {
        case 'Polish':  document.getElementById('project-title').innerHTML = 'Szachy Capablanki';
                        document.getElementById("node-counter-label").innerHTML = 'W&#281;z&#322;y rozwi&#261;zywane:';

                        document.getElementById('view-label').innerHTML = 'Widzenie';
                        document.getElementById('angle-label').innerHTML = 'K&#261;t';
                        document.getElementById('fullscreen-label').innerHTML = 'Ca&#322;y ekran';
                        document.getElementById('particles-label').innerHTML = 'Li&#347;cie';
                        document.getElementById('ai-label').innerHTML = 'A.I.';
                        document.getElementById('plies-label').innerHTML = 'Poziomy';
                        document.getElementById('aiblack-label').innerHTML = 'A.I. gra czarnymi';
                        document.getElementById('timecontrol-label').innerHTML = 'Zegar szachowy';
                        document.getElementById('useclock-label').innerHTML = 'U&#380;ywaj zegara';
                        document.getElementById('minutesallocated-label').innerHTML = 'Limit minut';

                        document.getElementById('tech-details').innerHTML = techDetails_pl;
                        break;
        case 'Spanish': document.getElementById('project-title').innerHTML = 'Ajedrez de Capablanca';
                        document.getElementById("node-counter-label").innerHTML = 'Nodos evaluados:';

                        document.getElementById('view-label').innerHTML = 'Visto';
                        document.getElementById('angle-label').innerHTML = '&#193;ngulo';
                        document.getElementById('fullscreen-label').innerHTML = 'Pantalla completa';
                        document.getElementById('particles-label').innerHTML = 'Hojas';
                        document.getElementById('ai-label').innerHTML = 'A.I.';
                        document.getElementById('plies-label').innerHTML = 'Niveles';
                        document.getElementById('aiblack-label').innerHTML = 'A.I. juega las piezas negras';
                        document.getElementById('timecontrol-label').innerHTML = 'Reloj de ajedrez';
                        document.getElementById('useclock-label').innerHTML = 'Usa el reloj';
                        document.getElementById('minutesallocated-label').innerHTML = 'Minutos';

                        document.getElementById('tech-details').innerHTML = techDetails_es;
                        break;
        case 'German':  document.getElementById('project-title').innerHTML = 'Capablancas Schach';
                        document.getElementById("node-counter-label").innerHTML = 'Knoten untersucht:';

                        document.getElementById('view-label').innerHTML = 'Sicht';
                        document.getElementById('angle-label').innerHTML = 'Blickwinkel';
                        document.getElementById('fullscreen-label').innerHTML = 'Vollbildansicht';
                        document.getElementById('particles-label').innerHTML = 'Bl&#228;tter';
                        document.getElementById('ai-label').innerHTML = 'A.I.';
                        document.getElementById('plies-label').innerHTML = 'Halbz&#252;ge voraus';
                        document.getElementById('aiblack-label').innerHTML = 'A.I. spielt Schwarz';
                        document.getElementById('timecontrol-label').innerHTML = 'Bedenkzeit';
                        document.getElementById('useclock-label').innerHTML = 'Schachuhr';
                        document.getElementById('minutesallocated-label').innerHTML = 'Minuten';

                        document.getElementById('tech-details').innerHTML = techDetails_de;
                        break;
        default:        document.getElementById('project-title').innerHTML = 'Capablanca Chess';
                        document.getElementById("node-counter-label").innerHTML = 'Nodes searched:';

                        document.getElementById('view-label').innerHTML = 'View';
                        document.getElementById('angle-label').innerHTML = 'Angle';
                        document.getElementById('fullscreen-label').innerHTML = 'Fullscreen';
                        document.getElementById('particles-label').innerHTML = 'Leaves';
                        document.getElementById('ai-label').innerHTML = 'A.I.';
                        document.getElementById('plies-label').innerHTML = 'Plies';
                        document.getElementById('aiblack-label').innerHTML = 'A.I. plays black';
                        document.getElementById('timecontrol-label').innerHTML = 'Time control';
                        document.getElementById('useclock-label').innerHTML = 'Timed game';
                        document.getElementById('minutesallocated-label').innerHTML = 'Minutes allocated';

                        document.getElementById('tech-details').innerHTML = techDetails_en;
      }
  }

function toggleParticleEffect(b)
  {
    var flowfield = document.getElementById('flowfield');
    if(b)
      flowfield.style.visibility = 'visible';
    else
      flowfield.style.visibility = 'hidden';
  }

//////////////////////////////////////////////////////////////////////////////////////
//   D I S P L A Y    A C T I O N S
function artworkForThinking(b)
  {
    if(b)
      document.getElementById("thinking-icon").className = "thinking";
    else
      document.getElementById("thinking-icon").className = "not-thinking";

    thoughtStaged = b;
  }

function nodeCounter(b)
  {
    if(b)
      {
        document.getElementById("node-counter-label").className = "show-node";
        document.getElementById("node-counter").className = "show-node";
      }
    else
      {
        document.getElementById("node-counter-label").className = "hide-node";
        document.getElementById("node-counter").className = "hide-node";
      }

    countStaged = b;
  }

function updateNodeCounter(ctr)
  {
    document.getElementById("node-counter").innerHTML = ctr;
  }

initGUI();
initHUD();
