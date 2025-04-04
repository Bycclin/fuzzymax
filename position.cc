// position.cpp
#include "engine.h"
#include <iostream>
#include <cstdint>
#include <array>
#include <vector>
#include <cmath>
#include <sstream>
#include <cctype>
#include <random>
#include <chrono>

// -----------------------------------------------------------------------------
// Directional Arrays
// -----------------------------------------------------------------------------
const int Position::bishopDir[4][2] = {
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
};

const int Position::rookDir[4][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}
};

const int Position::queenDir[8][2] = {
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}
};

// -----------------------------------------------------------------------------
// Zobrist Hashing Setup for Threefold Repetition
// -----------------------------------------------------------------------------

static uint64_t ZobristTable[12][64];
static uint64_t ZobristBlack;
static bool zobristInitialized = false;

static void initZobrist() {
    if (zobristInitialized) return;
    std::mt19937_64 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    for (int p = 0; p < 12; p++) {
        for (int sq = 0; sq < 64; sq++) {
            ZobristTable[p][sq] = rng();
        }
    }
    ZobristBlack = rng();
    zobristInitialized = true;
}

uint64_t Position::getZobristHash() const {
    initZobrist();
    uint64_t h = 0;
    for (int pieceType = 0; pieceType < 12; pieceType++) {
        Bitboard bb = pieces[pieceType];
        while (bb) {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            h ^= ZobristTable[pieceType][sq];
        }
    }
    if (side == 1) {
        h ^= ZobristBlack;
    }
    return h;
}

// -----------------------------------------------------------------------------
// Implementation of Position Methods
// -----------------------------------------------------------------------------

// Constructor: Initialize the standard starting position.
Position::Position() {
    pieces = {
        0x000000000000FF00ULL, // White pawns
        0x0000000000000042ULL, // White knights
        0x0000000000000024ULL, // White bishops
        0x0000000000000081ULL, // White rooks
        0x0000000000000008ULL, // White queen
        0x0000000000000010ULL, // White king
        0x00FF000000000000ULL, // Black pawns
        0x4200000000000000ULL, // Black knights
        0x2400000000000000ULL, // Black bishops
        0x8100000000000000ULL, // Black rooks
        0x0800000000000000ULL, // Black queen
        0x1000000000000000ULL  // Black king
    };
    wOcc = pieces[0] | pieces[1] | pieces[2] | pieces[3] | pieces[4] | pieces[5];
    bOcc = pieces[6] | pieces[7] | pieces[8] | pieces[9] | pieces[10] | pieces[11];
    allOcc = wOcc | bOcc;
    side = 0; // White to move by default.
}

// Create and return the starting position.
Position Position::create_start_position() {
    return Position();
}

Position Position::rotate() const {
    return *this;
}

Position::Bitboard Position::flip(Bitboard bb) {
    Bitboard ret = 0;
    for (int i = 0; i < 64; i++) {
        if (bb & (1ULL << i))
            ret |= 1ULL << (63 - i);
    }
    return ret;
}

// -----------------------------------------------------------------------------
// New: Check Detection
// -----------------------------------------------------------------------------
bool Position::is_in_check() const {
    // Find the king square for the side to move.
    int kingIndex = (side == 0 ? 5 : 11);
    if (pieces[kingIndex] == 0) return false; // Should not happen
    int kingSquare = __builtin_ctzll(pieces[kingIndex]);
    int kingRank = kingSquare / 8;
    int kingFile = kingSquare % 8;

    // Pawn attacks:
    if (side == 0) { // White king: enemy black pawn (index 6)
        if (kingRank + 1 < 8) {
            if (kingFile + 1 < 8) {
                int sq = (kingRank + 1) * 8 + (kingFile + 1);
                if (pieces[6] & (1ULL << sq)) return true;
            }
            if (kingFile - 1 >= 0) {
                int sq = (kingRank + 1) * 8 + (kingFile - 1);
                if (pieces[6] & (1ULL << sq)) return true;
            }
        }
    } else { // Black king: enemy white pawn (index 0)
        if (kingRank - 1 >= 0) {
            if (kingFile + 1 < 8) {
                int sq = (kingRank - 1) * 8 + (kingFile + 1);
                if (pieces[0] & (1ULL << sq)) return true;
            }
            if (kingFile - 1 >= 0) {
                int sq = (kingRank - 1) * 8 + (kingFile - 1);
                if (pieces[0] & (1ULL << sq)) return true;
            }
        }
    }

    // Knight attacks.
    int knightMoves[8][2] = {
         {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
         {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
    };
    for (int i = 0; i < 8; i++) {
         int r = kingRank + knightMoves[i][0];
         int f = kingFile + knightMoves[i][1];
         if (r >= 0 && r < 8 && f >= 0 && f < 8) {
             int sq = r * 8 + f;
             if (side == 0) { // White king: enemy knight is index 7
                 if (pieces[7] & (1ULL << sq)) return true;
             } else { // Black king: enemy knight is index 1
                 if (pieces[1] & (1ULL << sq)) return true;
             }
         }
    }

    // Adjacent enemy king.
    for (int dr = -1; dr <= 1; dr++) {
         for (int df = -1; df <= 1; df++) {
             if (dr == 0 && df == 0) continue;
             int r = kingRank + dr, f = kingFile + df;
             if (r >= 0 && r < 8 && f >= 0 && f < 8) {
                 int sq = r * 8 + f;
                 if (side == 0) { // White king: enemy king is index 11
                     if (pieces[11] & (1ULL << sq)) return true;
                 } else { // Black king: enemy king is index 5
                     if (pieces[5] & (1ULL << sq)) return true;
                 }
             }
         }
    }

    // Sliding pieces: Diagonals (enemy bishop or queen).
    int diagDirs[4][2] = { {1,1}, {1,-1}, {-1,1}, {-1,-1} };
    for (int i = 0; i < 4; i++) {
         int dr = diagDirs[i][0], df = diagDirs[i][1];
         int r = kingRank, f = kingFile;
         while (true) {
             r += dr; f += df;
             if (r < 0 || r >= 8 || f < 0 || f >= 8) break;
             int sq = r * 8 + f;
             if ((side == 0 && ((pieces[8] | pieces[10]) & (1ULL << sq))) ||
                 (side == 1 && ((pieces[2] | pieces[4]) & (1ULL << sq)))) {
                 return true;
             }
             if ((pieces[0] | pieces[1] | pieces[2] | pieces[3] |
                  pieces[4] | pieces[5] | pieces[6] | pieces[7] |
                  pieces[8] | pieces[9] | pieces[10] | pieces[11]) & (1ULL << sq)) {
                 break;
             }
         }
    }

    // Sliding pieces: Straights (enemy rook or queen).
    int straightDirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    for (int i = 0; i < 4; i++) {
         int dr = straightDirs[i][0], df = straightDirs[i][1];
         int r = kingRank, f = kingFile;
         while (true) {
             r += dr; f += df;
             if (r < 0 || r >= 8 || f < 0 || f >= 8) break;
             int sq = r * 8 + f;
             if ((side == 0 && ((pieces[9] | pieces[10]) & (1ULL << sq))) ||
                 (side == 1 && ((pieces[3] | pieces[4]) & (1ULL << sq)))) {
                 return true;
             }
             if ((pieces[0] | pieces[1] | pieces[2] | pieces[3] |
                  pieces[4] | pieces[5] | pieces[6] | pieces[7] |
                  pieces[8] | pieces[9] | pieces[10] | pieces[11]) & (1ULL << sq)) {
                 break;
             }
         }
    }
    return false;
}

bool Position::is_checkmate() const { 
    return is_in_check() && genMoves().empty(); 
}

bool Position::is_stalemate() const { 
    return !is_in_check() && genMoves().empty(); 
}

// -----------------------------------------------------------------------------
// Updated: Legal Move Generation Filtering Out Moves that Leave King in Check
// -----------------------------------------------------------------------------
std::vector<Move> Position::genMoves() const {
    std::vector<Move> pseudoLegalMoves;
    // Generate pseudo–legal moves.
    generateSlidingMoves(pseudoLegalMoves, 2, bishopDir, 4);
    generateSlidingMoves(pseudoLegalMoves, 3, rookDir, 4);
    generateSlidingMoves(pseudoLegalMoves, 4, queenDir, 8);
    generateKnightMoves(pseudoLegalMoves);
    generateKingMoves(pseudoLegalMoves);
    generatePawnMoves(pseudoLegalMoves);
    
    // Filter out moves that leave the king in check.
    std::vector<Move> legalMoves;
    for (const Move &m : pseudoLegalMoves) {
         Position newPos = makeMove(m);
         if (!newPos.is_in_check()) {
             legalMoves.push_back(m);
         }
    }
    return legalMoves;
}

// -----------------------------------------------------------------------------
// Modified generateSlidingMoves: Now with explicit occupancy checks to ensure
// that sliding pieces (such as bishops) stop at the first friendly piece encountered.
// -----------------------------------------------------------------------------
void Position::generateSlidingMoves(std::vector<Move>& moves, int pieceType, const int dir[][2], int numDirections) const {
    int offset = (side == 0 ? 0 : 6);
    Bitboard pieceBB = pieces[offset + pieceType];
    Bitboard friendly = (side == 0 ? wOcc : bOcc);
    Bitboard enemy = (side == 0 ? bOcc : wOcc);
    while (pieceBB) {
        int from = __builtin_ctzll(pieceBB);
        pieceBB &= pieceBB - 1;
        int fromRank = from / 8;
        int fromFile = from % 8;
        for (int d = 0; d < numDirections; d++) {
            int dr = dir[d][0], df = dir[d][1];
            int r = fromRank, f = fromFile;
            while (true) {
                r += dr;
                f += df;
                if (r < 0 || r >= 8 || f < 0 || f >= 8)
                    break;
                int to = r * 8 + f;
                Bitboard toMask = 1ULL << to;
                if ((friendly & toMask) != 0ULL)
                    break;
                moves.push_back(Move{from, to});
                if ((enemy & toMask) != 0ULL)
                    break;
            }
        }
    }
}

void Position::generateKnightMoves(std::vector<Move>& moves) const {
    int offset = (side == 0 ? 0 : 6);
    Bitboard knights = pieces[offset + 1];
    Bitboard friendly = (side == 0 ? wOcc : bOcc);
    while (knights) {
        int from = __builtin_ctzll(knights);
        knights &= knights - 1;
        int r = from / 8, f = from % 8;
        static const int knightMoves[8][2] = {
            {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
            {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
        };
        for (int i = 0; i < 8; i++) {
            int nr = r + knightMoves[i][0], nf = f + knightMoves[i][1];
            if (nr < 0 || nr >= 8 || nf < 0 || nf >= 8)
                continue;
            int to = nr * 8 + nf;
            Bitboard toMask = 1ULL << to;
            if (friendly & toMask)
                continue;
            moves.push_back(Move{from, to});
        }
    }
}

void Position::generateKingMoves(std::vector<Move>& moves) const {
    int offset = (side == 0 ? 0 : 6);
    Bitboard king = pieces[offset + 5];
    Bitboard friendly = (side == 0 ? wOcc : bOcc);
    if (!king)
        return;
    int from = __builtin_ctzll(king);
    int r = from / 8, f = from % 8;
    static const int kingMoves[8][2] = {
        {1,0}, {1,1}, {0,1}, {-1,1},
        {-1,0}, {-1,-1}, {0,-1}, {1,-1}
    };
    for (int i = 0; i < 8; i++) {
        int nr = r + kingMoves[i][0], nf = f + kingMoves[i][1];
        if (nr < 0 || nr >= 8 || nf < 0 || nf >= 8)
            continue;
        int to = nr * 8 + nf;
        Bitboard toMask = 1ULL << to;
        if (friendly & toMask)
            continue;
        moves.push_back(Move{from, to});
    }
}

void Position::generatePawnMoves(std::vector<Move>& moves) const {
    int offset = (side == 0 ? 0 : 6);
    Bitboard pawns = pieces[offset + 0];
    Bitboard enemy = (side == 0 ? bOcc : wOcc);
    while (pawns) {
        int from = __builtin_ctzll(pawns);
        pawns &= pawns - 1;
        int r = from / 8, f = from % 8;
        int nr = (side == 0 ? r + 1 : r - 1);
        if (nr < 0 || nr >= 8)
            continue;
        int to = nr * 8 + f;
        Bitboard toMask = 1ULL << to;
        if (!(allOcc & toMask))
            moves.push_back(Move{from, to});
        if (f - 1 >= 0) {
            int toCap = nr * 8 + (f - 1);
            Bitboard capMask = 1ULL << toCap;
            if (enemy & capMask)
                moves.push_back(Move{from, toCap});
        }
        if (f + 1 < 8) {
            int toCap = nr * 8 + (f + 1);
            Bitboard capMask = 1ULL << toCap;
            if (enemy & capMask)
                moves.push_back(Move{from, toCap});
        }
    }
}

Position Position::makeMove(const Move &m) const {
    Position newPos = *this;
    int offset = (side == 0 ? 0 : 6);
    int enemyOffset = (side == 0 ? 6 : 0);
    int movingPiece = -1;
    for (int i = 0; i < 6; i++) {
        if (newPos.pieces[offset + i] & (1ULL << m.from)) {
            movingPiece = offset + i;
            break;
        }
    }
    if (movingPiece == -1)
        return newPos;
    newPos.pieces[movingPiece] &= ~(1ULL << m.from);
    for (int i = 0; i < 6; i++) {
        if (newPos.pieces[enemyOffset + i] & (1ULL << m.to)) {
            newPos.pieces[enemyOffset + i] &= ~(1ULL << m.to);
            break;
        }
    }
    newPos.pieces[movingPiece] |= (1ULL << m.to);
    newPos.wOcc = newPos.pieces[0] | newPos.pieces[1] | newPos.pieces[2] |
                  newPos.pieces[3] | newPos.pieces[4] | newPos.pieces[5];
    newPos.bOcc = newPos.pieces[6] | newPos.pieces[7] | newPos.pieces[8] |
                  newPos.pieces[9] | newPos.pieces[10] | newPos.pieces[11];
    newPos.allOcc = newPos.wOcc | newPos.bOcc;
    newPos.side = 1 - side;
    return newPos;
}

std::string Position::to_string() const {
    std::string board(64, '.');
    const char* symbols = "PNBRQKpnbrqk";
    for (int i = 0; i < 12; i++) {
        Bitboard bb = pieces[i];
        while (bb) {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            board[sq] = symbols[i];
        }
    }
    std::string s;
    for (int rank = 7; rank >= 0; rank--) {
        for (int file = 0; file < 8; file++) {
            s.push_back(board[rank * 8 + file]);
            s.push_back(' ');
        }
        s.push_back('\n');
    }
    return s;
}

std::string Position::current_turn() const { 
    return (side == 0 ? "white" : "black"); 
}

Position Position::fromFEN(const std::string &fen) {
    Position pos;
    pos.pieces.fill(0);
    std::istringstream iss(fen);
    std::string placement, activeColor, castling, enPassant;
    int halfmove, fullmove;
    iss >> placement >> activeColor >> castling >> enPassant >> halfmove >> fullmove;
    
    int rank = 7;
    int file = 0;
    for (char c : placement) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (std::isdigit(c)) {
            file += c - '0';
        } else {
            int square = rank * 8 + file;
            int index = -1;
            switch(c) {
                case 'P': index = 0; break;
                case 'N': index = 1; break;
                case 'B': index = 2; break;
                case 'R': index = 3; break;
                case 'Q': index = 4; break;
                case 'K': index = 5; break;
                case 'p': index = 6; break;
                case 'n': index = 7; break;
                case 'b': index = 8; break;
                case 'r': index = 9; break;
                case 'q': index = 10; break;
                case 'k': index = 11; break;
            }
            if (index != -1)
                pos.pieces[index] |= (1ULL << square);
            file++;
        }
    }
    pos.wOcc = pos.pieces[0] | pos.pieces[1] | pos.pieces[2] |
               pos.pieces[3] | pos.pieces[4] | pos.pieces[5];
    pos.bOcc = pos.pieces[6] | pos.pieces[7] | pos.pieces[8] |
               pos.pieces[9] | pos.pieces[10] | pos.pieces[11];
    pos.allOcc = pos.wOcc | pos.bOcc;
    pos.side = (activeColor == "w" ? 0 : 1);
    return pos;
}

// -----------------------------------------------------------------------------
// New: Definitions for Missing Methods
// -----------------------------------------------------------------------------
bool Position::is_threefold_repetition() const {
    extern std::vector<uint64_t> gameHashes;
    uint64_t h = getZobristHash();
    int count = 0;
    for (auto hash : gameHashes) {
        if (hash == h) count++;
    }
    return count >= 3;
}

bool Position::isInsufficientMaterial() const {
    // Stub: full logic to detect insufficient material can be added.
    return false;
}
