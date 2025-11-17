#include "board.h"
#include "movegen.h"
#include "globals.h"
#include "constants.h"

#include <iostream>
#include <random>

// Piece zobrist table
MultiArray<u64, 2, 6, 64> PIECE_ZTABLE;
// En passant zobrist table
array<u64, 65> EP_ZTABLE;
// Zobrist for stm
u64 STM_ZHASH;
// Zobrist for castling rights
array<u64, 16> CASTLING_ZTABLE;


// Returns the piece on a square as a character
char Board::getPieceAt(int sq) const {
    assert(sq >= 0);
    assert(sq < 64);
    if (getPiece(sq) == NO_PIECE_TYPE)
        return ' ';
    constexpr char whiteSymbols[] = { 'P', 'N', 'B', 'R', 'Q', 'K' };
    constexpr char blackSymbols[] = { 'p', 'n', 'b', 'r', 'q', 'k' };
    if (((1ULL << sq) & byColor[WHITE]) != 0)
        return whiteSymbols[getPiece(sq)];
    return blackSymbols[getPiece(sq)];
}

void Board::placePiece(Color c, PieceType pt, int sq) {
    assert(sq >= 0);
    assert(sq < 64);

    auto& BB = byPieces[pt];

    assert(!readBit(BB, sq));

    zobrist ^= PIECE_ZTABLE[c][pt][sq];

    BB ^= 1ULL << sq;
    byColor[c] ^= 1ULL << sq;

    mailbox[sq] = pt;
}

void Board::removePiece(Color c, PieceType pt, int sq) {
    assert(sq >= 0);
    assert(sq < 64);

    auto& BB = byPieces[pt];

    assert(readBit(BB, sq));

    zobrist ^= PIECE_ZTABLE[c][pt][sq];

    BB ^= 1ULL << sq;
    byColor[c] ^= 1ULL << sq;

    mailbox[sq] = NO_PIECE_TYPE;
}

void Board::removePiece(Color c, int sq) {
    assert(sq >= 0);
    assert(sq < 64);

    auto& BB = byPieces[getPiece(sq)];

    assert(readBit(BB, sq));

    zobrist ^= PIECE_ZTABLE[c][getPiece(sq)][sq];

    BB ^= 1ULL << sq;
    byColor[c] ^= 1ULL << sq;

    mailbox[sq] = NO_PIECE_TYPE;
}

void Board::resetMailbox() {
    mailbox.fill(NO_PIECE_TYPE);
    for (u8 i = 0; i < 64; i++) {
        PieceType& sq   = mailbox[i];
        u64        mask = 1ULL << i;
        if (mask & pieces(PAWN))
            sq = PAWN;
        else if (mask & pieces(KNIGHT))
            sq = KNIGHT;
        else if (mask & pieces(BISHOP))
            sq = BISHOP;
        else if (mask & pieces(ROOK))
            sq = ROOK;
        else if (mask & pieces(QUEEN))
            sq = QUEEN;
        else if (mask & pieces(KING))
            sq = KING;
    }
}

void Board::resetZobrist() {
    zobrist = 0;

    for (PieceType pt = PAWN; pt <= KING; pt = PieceType(static_cast<int>(pt) + 1)) {
        u64 pcs = pieces(WHITE, pt);
        while (pcs) {
            Square sq = popLSB(pcs);
            zobrist ^= PIECE_ZTABLE[WHITE][pt][sq];
        }

        pcs = pieces(BLACK, pt);
        while (pcs) {
            Square sq = popLSB(pcs);
            zobrist ^= PIECE_ZTABLE[BLACK][pt][sq];
        }
    }

    zobrist ^= hashCastling();
    zobrist ^= EP_ZTABLE[epSquare];
}

// Updates checkers and pinners
void Board::updateCheckPinAttack() {
    attacking[stm]  = Movegen::getAttacks(stm, *this);
    attacking[~stm] = Movegen::getAttacks(~stm, *this);

    const u64    kingBB = pieces(stm, KING);
    const Square kingSq = getLSB(kingBB);

    const u64 ourPieces         = pieces(stm);
    const u64 enemyRookQueens   = pieces(~stm, ROOK, QUEEN);
    const u64 enemyBishopQueens = pieces(~stm, BISHOP, QUEEN);

    const u64 occ = pieces();

    // *** BISHOP ROOK QUEEN ATTACKS ***
    const u64 rookChecks   = Movegen::getRookAttacks(Square(kingSq), occ) & enemyRookQueens;
    const u64 bishopChecks = Movegen::getBishopAttacks(Square(kingSq), occ) & enemyBishopQueens;
    u64       checks       = rookChecks | bishopChecks;

    // *** KNIGHT ATTACKS ***
    const u64 knightAttacks = Movegen::KNIGHT_ATTACKS[kingSq] & pieces(~stm, KNIGHT);

    // *** PAWN ATTACKS ***
    u64 checkingPawns = 0;
    if (stm == WHITE) {
        checkingPawns |= shift<NORTH_WEST>(kingBB & ~MASK_FILE[FILE_A]) & pieces(BLACK, PAWN);
        checkingPawns |= shift<NORTH_EAST>(kingBB & ~MASK_FILE[FILE_H]) & pieces(BLACK, PAWN);
    }
    else {
        checkingPawns |= shift<SOUTH_WEST>(kingBB & ~MASK_FILE[FILE_A]) & pieces(WHITE, PAWN);
        checkingPawns |= shift<SOUTH_EAST>(kingBB & ~MASK_FILE[FILE_H]) & pieces(WHITE, PAWN);
    }

    checkers  = knightAttacks | rookChecks | bishopChecks | checkingPawns;
    checkMask = knightAttacks | checkingPawns;

    doubleCheck = popcount(checkers) > 1;

    while (checks)
        checkMask |= LINESEG[kingSq][popLSB(checks)];

    if (checkMask == 0)
        checkMask = ~checkMask;  // If no checks, set to all ones

    // ****** PIN STUFF HERE ******
    const u64 rookXrays   = Movegen::getXrayRookAttacks(Square(kingSq), pieces(), ourPieces) & enemyRookQueens;
    const u64 bishopXrays = Movegen::getXrayBishopAttacks(Square(kingSq), pieces(), ourPieces) & enemyBishopQueens;
    u64       pinners     = rookXrays | bishopXrays;
    pinnersPerC[stm]      = pinners;

    pinned = 0;
    while (pinners)
        pinned |= LINESEG[popLSB(pinners)][kingSq] & ourPieces;
}

void Board::setCastlingRights(Color c, Square sq, bool value) { castling[castleIndex(c, ctzll(pieces(c, KING)) < sq)] = (value == false ? NO_SQUARE : sq); }

void Board::unsetCastlingRights(Color c) { castling[castleIndex(c, true)] = castling[castleIndex(c, false)] = NO_SQUARE; }

u64 Board::hashCastling() const {
    constexpr usize blackQ = 0b1;
    constexpr usize blackK = 0b10;
    constexpr usize whiteQ = 0b100;
    constexpr usize whiteK = 0b1000;

    usize flags = 0;

    if (castling[castleIndex(WHITE, true)])
        flags |= whiteK;
    if (castling[castleIndex(WHITE, false)])
        flags |= whiteQ;
    if (castling[castleIndex(BLACK, true)])
        flags |= blackK;
    if (castling[castleIndex(BLACK, false)])
        flags |= blackQ;

    return CASTLING_ZTABLE[flags];
}

void Board::fillZobristTable() {
    std::random_device rd;
    std::mt19937_64    engine(rd());
    engine.seed(69420);  // Nice
    std::uniform_int_distribution<u64> dist(0, ~0ULL);

    for (auto& stm : PIECE_ZTABLE)
        for (auto& pt : stm)
            for (auto& piece : pt)
                piece = dist(engine);

    for (auto& ep : EP_ZTABLE)
        ep = dist(engine);

    STM_ZHASH = dist(engine);

    for (auto& right : CASTLING_ZTABLE)
        right = dist(engine);

    EP_ZTABLE[NO_SQUARE] = 0;
}

u8 Board::count(PieceType pt) const { return popcount(pieces(pt)); }

u64 Board::pieces() const { return byColor[WHITE] | byColor[BLACK]; }
u64 Board::pieces(Color c) const { return byColor[c]; }
u64 Board::pieces(PieceType pt) const { return byPieces[pt]; }
u64 Board::pieces(Color c, PieceType pt) const { return byPieces[pt] & byColor[c]; }
u64 Board::pieces(PieceType pt1, PieceType pt2) const { return byPieces[pt1] | byPieces[pt2]; }
u64 Board::pieces(Color c, PieceType pt1, PieceType pt2) const { return (byPieces[pt1] | byPieces[pt2]) & byColor[c]; }

u64 Board::attackersTo(Square sq, u64 occ) const {
    return (Movegen::getRookAttacks(sq, occ) & pieces(ROOK, QUEEN)) | (Movegen::getBishopAttacks(sq, occ) & pieces(BISHOP, QUEEN)) | (Movegen::pawnAttackBB(WHITE, sq) & pieces(BLACK, PAWN))
         | (Movegen::pawnAttackBB(BLACK, sq) & pieces(WHITE, PAWN)) | (Movegen::KNIGHT_ATTACKS[sq] & pieces(KNIGHT)) | (Movegen::KING_ATTACKS[sq] & pieces(KING));
}

// Reset the board to startpos
void Board::reset() {
    byPieces[PAWN]   = 0xFF00ULL;
    byPieces[KNIGHT] = 0x42ULL;
    byPieces[BISHOP] = 0x24ULL;
    byPieces[ROOK]   = 0x81ULL;
    byPieces[QUEEN]  = 0x8ULL;
    byPieces[KING]   = 0x10ULL;
    byColor[WHITE]   = byPieces[PAWN] | byPieces[KNIGHT] | byPieces[BISHOP] | byPieces[ROOK] | byPieces[QUEEN] | byPieces[KING];

    byPieces[PAWN] |= 0xFF000000000000ULL;
    byPieces[KNIGHT] |= 0x4200000000000000ULL;
    byPieces[BISHOP] |= 0x2400000000000000ULL;
    byPieces[ROOK] |= 0x8100000000000000ULL;
    byPieces[QUEEN] |= 0x800000000000000ULL;
    byPieces[KING] |= 0x1000000000000000ULL;
    byColor[BLACK] = 0xFF000000000000ULL | 0x4200000000000000ULL | 0x2400000000000000ULL | 0x8100000000000000ULL | 0x800000000000000ULL | 0x1000000000000000ULL;


    stm      = WHITE;
    castling = { a8, h8, a1, h1 };

    epSquare = NO_SQUARE;

    halfMoveClock = 0;
    fullMoveClock = 1;

    resetMailbox();
    resetZobrist();
    updateCheckPinAttack();
}

// Load a board from the FEN
void Board::loadFromFEN(string fen) {
    reset();

    // Clear all squares
    byPieces.fill(0);
    byColor.fill(0);

    std::vector<string> tokens = split(fen, ' ');

    std::vector<string> rankTokens = split(tokens[0], '/');

    int currIdx = 56;

    const char whitePieces[6] = { 'P', 'N', 'B', 'R', 'Q', 'K' };
    const char blackPieces[6] = { 'p', 'n', 'b', 'r', 'q', 'k' };

    for (const string& rank : rankTokens) {
        for (const char c : rank) {
            if (isdigit(c)) {  // Empty squares
                currIdx += c - '0';
                continue;
            }
            for (int i = 0; i < 6; i++) {
                if (c == whitePieces[i]) {
                    setBit<1>(byPieces[i], currIdx);
                    setBit<1>(byColor[WHITE], currIdx);
                    break;
                }
                if (c == blackPieces[i]) {
                    setBit<1>(byPieces[i], currIdx);
                    setBit<1>(byColor[BLACK], currIdx);
                    break;
                }
            }
            currIdx++;
        }
        currIdx -= 16;
    }

    if (tokens[1] == "w")
        stm = WHITE;
    else
        stm = BLACK;

    castling.fill(NO_SQUARE);
    if (tokens[2].find('-') == string::npos) {
        // Standard FEN
        // Standard FEN and maybe XFEN later
        if (tokens[2].find('K') != string::npos)
            castling[castleIndex(WHITE, true)] = h1;
        if (tokens[2].find('Q') != string::npos)
            castling[castleIndex(WHITE, false)] = a1;
        if (tokens[2].find('k') != string::npos)
            castling[castleIndex(BLACK, true)] = h8;
        if (tokens[2].find('q') != string::npos)
            castling[castleIndex(BLACK, false)] = a8;

        // FRC FEN
        if (std::tolower(tokens[2][0]) >= 'a' && std::tolower(tokens[2][0]) <= 'h') {
            chess960 = true;
            for (char token : tokens[2]) {
                File file = static_cast<File>(std::tolower(token) - 'a');

                if (std::isupper(token))
                    setCastlingRights(WHITE, toSquare(RANK1, file), true);
                else
                    setCastlingRights(BLACK, toSquare(RANK8, file), true);
            }
        }
    }

    if (tokens[3] != "-")
        epSquare = parseSquare(tokens[3]);
    else
        epSquare = NO_SQUARE;

    halfMoveClock = tokens.size() > 4 ? (stoi(tokens[4])) : 0;
    fullMoveClock = tokens.size() > 5 ? (stoi(tokens[5])) : 1;

    resetMailbox();
    resetZobrist();
    updateCheckPinAttack();
}

string Board::fen() const {
    std::ostringstream ss;

    // Pieces
    for (i32 rank = 7; rank >= 0; rank--) {
        usize empty = 0;
        for (usize file = 0; file < 8; file++) {
            i32  sq = rank * 8 + file;
            char pc = getPieceAt(sq);
            if (pc == ' ')
                empty++;
            else {
                if (empty) {
                    ss << empty;
                    empty = 0;
                }
                ss << pc;
            }
        }
        if (empty)
            ss << empty;
        if (rank != 0)
            ss << '/';
    }

    // Stm
    ss << ' ' << (stm == WHITE ? 'w' : 'b');

    // Castling
    string castle;
    if (castling[castleIndex(WHITE, true)] != NO_SQUARE)
        castle += 'K';
    if (castling[castleIndex(WHITE, false)] != NO_SQUARE)
        castle += 'Q';
    if (castling[castleIndex(BLACK, true)] != NO_SQUARE)
        castle += 'k';
    if (castling[castleIndex(BLACK, false)] != NO_SQUARE)
        castle += 'q';
    ss << ' ' << (castle.empty() ? "-" : castle);

    // En passant
    if (epSquare != NO_SQUARE)
        ss << ' ' << squareToAlgebraic(epSquare);
    else
        ss << " -";

    // Halfmove
    ss << ' ' << halfMoveClock;

    // Fullmove
    ss << ' ' << fullMoveClock;

    return ss.str();
}

// Return the type of the piece on the square
PieceType Board::getPiece(int sq) const {
    assert(sq >= 0);
    assert(sq < 64);
    return mailbox[sq];
}

// This should return false if
// Move is a capture of any kind
// Move is a queen promotion
// Move is a knight promotion
bool Board::isQuiet(Move m) const { return !isCapture(m) && (m.typeOf() != PROMOTION || m.promo() != QUEEN); }

bool Board::isCapture(Move m) const { return ((1ULL << m.to() & pieces(~stm)) || m.typeOf() == EN_PASSANT); }


// Make a move from a string
void Board::move(string str) { move(Move(str, *this)); }

// Make a move
void Board::move(Move m) {
    zobrist ^= hashCastling();
    zobrist ^= EP_ZTABLE[epSquare];

    epSquare       = NO_SQUARE;
    Square    from = m.from();
    Square    to   = m.to();
    MoveType  mt   = m.typeOf();
    PieceType pt   = getPiece(from);
    PieceType toPT = NO_PIECE_TYPE;

    removePiece(stm, pt, from);
    if (isCapture(m)) {
        toPT          = getPiece(to);
        halfMoveClock = 0;
        if (mt != EN_PASSANT) {
            removePiece(~stm, toPT, to);
        }
    }
    else {
        if (pt == PAWN)
            halfMoveClock = 0;
        else
            halfMoveClock++;
    }

    switch (mt) {
    case STANDARD_MOVE:
        placePiece(stm, pt, to);
        if (pt == PAWN && (to + 16 == from || to - 16 == from)
            && (pieces(~stm, PAWN) & (shift<EAST>((1ULL << to) & ~MASK_FILE[FILE_H]) | shift<WEST>((1ULL << to) & ~MASK_FILE[FILE_A]))))  // Only set EP square if it could be taken
            epSquare = Square(stm == WHITE ? from + NORTH : from + SOUTH);
        break;
    case EN_PASSANT:
        removePiece(~stm, PAWN, to + (stm == WHITE ? SOUTH : NORTH));
        placePiece(stm, pt, to);
        break;
    case CASTLE:
        assert(getPiece(to) == ROOK);
        removePiece(stm, ROOK, to);
        if (stm == WHITE) {
            if (from < to) {
                placePiece(stm, KING, g1);
                placePiece(stm, ROOK, f1);
            }
            else {
                placePiece(stm, KING, c1);
                placePiece(stm, ROOK, d1);
            }
        }
        else {
            if (from < to) {
                placePiece(stm, KING, g8);
                placePiece(stm, ROOK, f8);
            }
            else {
                placePiece(stm, KING, c8);
                placePiece(stm, ROOK, d8);
            }
        }
        break;
    case PROMOTION:
        placePiece(stm, m.promo(), to);
        break;
    }

    assert(popcount(pieces(WHITE, KING)) == 1);
    assert(popcount(pieces(BLACK, KING)) == 1);

    if (pt == ROOK) {
        const Square sq = castleSq(stm, from > ctzll(pieces(stm, KING)));
        if (from == sq)
            setCastlingRights(stm, from, false);
    }
    else if (pt == KING)
        unsetCastlingRights(stm);
    if (toPT == ROOK) {
        const Square sq = castleSq(~stm, to > ctzll(pieces(~stm, KING)));
        if (to == sq)
            setCastlingRights(~stm, to, false);
    }

    stm = ~stm;

    zobrist ^= hashCastling();
    zobrist ^= EP_ZTABLE[epSquare];
    zobrist ^= STM_ZHASH;

    fullMoveClock += stm == WHITE;

    updateCheckPinAttack();
}

bool Board::canCastle(Color c) const { return castleSq(c, true) != NO_SQUARE || castleSq(c, false) != NO_SQUARE; }
bool Board::canCastle(Color c, bool kingside) const { return castleSq(c, kingside) != NO_SQUARE; }

bool Board::inCheck() const { return checkers != 0; }
bool Board::inCheck(Color c) const { return attacking[~c] & pieces(c, KING); }

bool Board::isUnderAttack(Color c, Square square) const { return attacking[~c] & (1ULL << square); }


bool Board::isDraw(const vector<u64>& posHistory) const {
    // 50 move rule
    if (halfMoveClock >= 100)
        return !inCheck();

    // Insufficient material
    if (pieces(PAWN) == 0                                  // No pawns
        && pieces(QUEEN) == 0                              // No queens
        && pieces(ROOK) == 0                               // No rooks
        && ((pieces(BISHOP) & LIGHT_SQ_BB) == 0            // No light sq bishops
            || (pieces(BISHOP) & DARK_SQ_BB) == 0)         // OR no dark sq bishops
        && (pieces(BISHOP) == 0 || pieces(KNIGHT) == 0)    // Not bishop + knight
        && popcount(pieces(KNIGHT)) < 2)                   // Under 2 knights
        return true;

    // Threefold
    if (!posHistory.empty()) {
        usize     reps    = 0;
        const u64 current = posHistory.back();

        for (const u64 hash : posHistory)
            if (hash == current)
                if (++reps == 3)
                    return true;
    }

    return false;
}

bool Board::isGameOver(const vector<u64>& posHistory) const {
    if (isDraw(posHistory))
        return true;

    return Movegen::generateMoves(*this).length == 0;
}

std::string Board::asString(const Move m) const {
    std::ostringstream os;
    const auto printInfo = [&](const usize line) {
        std::ostringstream ss;
        if (line == 1)
            ss << "FEN: " << fen();
        else if (line == 2)
            ss << "Hash: 0x" << std::hex << std::uppercase << zobrist << std::dec;
        else if (line == 3)
            ss << "Side to move: " << (stm == WHITE ? "WHITE" : "BLACK");
        else if (line == 4)
            ss << "En passant: " << (epSquare == NO_SQUARE ? "-" : squareToAlgebraic(epSquare));
        return ss.str();
    };

    os << "\u250c\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510\n";

    const auto from = m.isNull() ? NO_SQUARE : m.from();
    const auto to   = m.isNull() ? NO_SQUARE : m.to();

    const auto fromColor = fmt::color::dim_gray;
    const auto toColor   = isCapture(m) ? fmt::color::dark_red : fmt::color::dim_gray;

    usize line = 1;
    for (i32 rank = (stm == WHITE) * 7; (stm == WHITE) ? rank >= 0 : rank < 8; (stm == WHITE) ? rank-- : rank++) {
        os << "\u2502 ";
        for (usize file = 0; file < 8; file++) {
            const auto sq    = static_cast<Square>(rank * 8 + file);
            const auto fgColor = ((1ULL << sq) & pieces(WHITE)) ? fmt::color::orange : fmt::color::dark_blue;
            const auto bgColor = sq == to ? toColor : fromColor;

            if (from == sq || to == sq)
                os << fmt::format(fmt::fg(fgColor) | fmt::bg(bgColor), "{}", getPieceAt(sq)) << " ";
            else
                os << fmt::format(fmt::fg(fgColor), "{}", getPieceAt(sq)) << " ";
        }
        os << "\u2502 " << rank + 1 << "    " << printInfo(line++) << "\n";
    }
    os << "\u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518\n";
    os << "  a b c d e f g h\n";
    return os.str();
}

// Print the board
std::ostream& operator<<(std::ostream& os, const Board& board) {
    os << board.asString();
    return os;
}