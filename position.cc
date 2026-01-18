#include "engine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

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

    std::mt19937_64 rng(static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

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
    side = 0;
}

Position Position::create_start_position() {
    return Position();
}

Position Position::rotate() const {
    return *this;
}

Position::Bitboard Position::flip(Bitboard bb) {
    Bitboard ret = 0;
    for (int i = 0; i < 64; i++) {
        if (bb & (1ULL << i)) {
            ret |= 1ULL << (63 - i);
        }
    }
    return ret;
}

// -----------------------------------------------------------------------------
// Check Detection (for side-to-move king)
// -----------------------------------------------------------------------------

bool Position::is_in_check() const {
    const int kingIndex = (side == 0 ? 5 : 11);
    if (pieces[kingIndex] == 0) return false;

    const int kingSquare = __builtin_ctzll(pieces[kingIndex]);
    const int kingRank = kingSquare / 8;
    const int kingFile = kingSquare % 8;

    // Pawn attacks
    if (side == 0) {
        // White king, attacked by black pawns (index 6) from rank+1 diagonals
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
    } else {
        // Black king, attacked by white pawns (index 0) from rank-1 diagonals
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

    // Knight attacks
    static const int knightDeltas[8][2] = {
        {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
        {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
    };

    for (const auto &d : knightDeltas) {
        int r = kingRank + d[0];
        int f = kingFile + d[1];
        if (r < 0 || r >= 8 || f < 0 || f >= 8) continue;
        int sq = r * 8 + f;

        if (side == 0) {
            // enemy black knight
            if (pieces[7] & (1ULL << sq)) return true;
        } else {
            // enemy white knight
            if (pieces[1] & (1ULL << sq)) return true;
        }
    }

    // Adjacent enemy king
    for (int dr = -1; dr <= 1; dr++) {
        for (int df = -1; df <= 1; df++) {
            if (dr == 0 && df == 0) continue;
            int r = kingRank + dr;
            int f = kingFile + df;
            if (r < 0 || r >= 8 || f < 0 || f >= 8) continue;
            int sq = r * 8 + f;

            if (side == 0) {
                if (pieces[11] & (1ULL << sq)) return true;
            } else {
                if (pieces[5] & (1ULL << sq)) return true;
            }
        }
    }

    // Sliding attacks: diagonals (bishop/queen)
    static const int diagDirs[4][2] = { {1,1}, {1,-1}, {-1,1}, {-1,-1} };
    for (const auto &d : diagDirs) {
        int r = kingRank;
        int f = kingFile;
        while (true) {
            r += d[0];
            f += d[1];
            if (r < 0 || r >= 8 || f < 0 || f >= 8) break;
            int sq = r * 8 + f;
            Bitboard mask = 1ULL << sq;

            if (side == 0) {
                if ((pieces[8] | pieces[10]) & mask) return true;
            } else {
                if ((pieces[2] | pieces[4]) & mask) return true;
            }

            if (allOcc & mask) break;
        }
    }

    // Sliding attacks: straights (rook/queen)
    static const int straightDirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    for (const auto &d : straightDirs) {
        int r = kingRank;
        int f = kingFile;
        while (true) {
            r += d[0];
            f += d[1];
            if (r < 0 || r >= 8 || f < 0 || f >= 8) break;
            int sq = r * 8 + f;
            Bitboard mask = 1ULL << sq;

            if (side == 0) {
                if ((pieces[9] | pieces[10]) & mask) return true;
            } else {
                if ((pieces[3] | pieces[4]) & mask) return true;
            }

            if (allOcc & mask) break;
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
// Legal Move Generation: generate pseudo-legal, then filter leaving king in check
// -----------------------------------------------------------------------------

std::vector<Move> Position::genMoves() const {
    std::vector<Move> pseudo;
    generateSlidingMoves(pseudo, 2, bishopDir, 4);
    generateSlidingMoves(pseudo, 3, rookDir, 4);
    generateSlidingMoves(pseudo, 4, queenDir, 8);
    generateKnightMoves(pseudo);
    generateKingMoves(pseudo);
    generatePawnMoves(pseudo);

    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    for (const Move &m : pseudo) {
        Position next = makeMove(m);
        if (!next.is_in_check()) {
            legal.push_back(m);
        }
    }
    return legal;
}

void Position::generateSlidingMoves(std::vector<Move>& moves, int pieceType, const int dir[][2], int numDirections) const {
    const int offset = (side == 0 ? 0 : 6);
    Bitboard bb = pieces[offset + pieceType];
    const Bitboard friendly = (side == 0 ? wOcc : bOcc);
    const Bitboard enemy = (side == 0 ? bOcc : wOcc);

    while (bb) {
        int from = __builtin_ctzll(bb);
        bb &= bb - 1;

        int fr = from / 8;
        int ff = from % 8;

        for (int d = 0; d < numDirections; d++) {
            int r = fr;
            int f = ff;
            const int dr = dir[d][0];
            const int df = dir[d][1];

            while (true) {
                r += dr;
                f += df;
                if (r < 0 || r >= 8 || f < 0 || f >= 8) break;

                int to = r * 8 + f;
                Bitboard mask = 1ULL << to;

                if (friendly & mask) break;

                moves.emplace_back(from, to);

                if (enemy & mask) break;
            }
        }
    }
}

void Position::generateKnightMoves(std::vector<Move>& moves) const {
    const int offset = (side == 0 ? 0 : 6);
    Bitboard knights = pieces[offset + 1];
    const Bitboard friendly = (side == 0 ? wOcc : bOcc);

    static const int deltas[8][2] = {
        {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
        {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
    };

    while (knights) {
        int from = __builtin_ctzll(knights);
        knights &= knights - 1;

        int r = from / 8;
        int f = from % 8;

        for (const auto &d : deltas) {
            int nr = r + d[0];
            int nf = f + d[1];
            if (nr < 0 || nr >= 8 || nf < 0 || nf >= 8) continue;

            int to = nr * 8 + nf;
            Bitboard mask = 1ULL << to;
            if (friendly & mask) continue;

            moves.emplace_back(from, to);
        }
    }
}

void Position::generateKingMoves(std::vector<Move>& moves) const {
    const int offset = (side == 0 ? 0 : 6);
    Bitboard king = pieces[offset + 5];
    const Bitboard friendly = (side == 0 ? wOcc : bOcc);

    if (!king) return;

    int from = __builtin_ctzll(king);
    int r = from / 8;
    int f = from % 8;

    static const int deltas[8][2] = {
        {1,0}, {1,1}, {0,1}, {-1,1},
        {-1,0}, {-1,-1}, {0,-1}, {1,-1}
    };

    for (const auto &d : deltas) {
        int nr = r + d[0];
        int nf = f + d[1];
        if (nr < 0 || nr >= 8 || nf < 0 || nf >= 8) continue;

        int to = nr * 8 + nf;
        Bitboard mask = 1ULL << to;
        if (friendly & mask) continue;

        moves.emplace_back(from, to);
    }
}

void Position::generatePawnMoves(std::vector<Move>& moves) const {
    const int offset = (side == 0 ? 0 : 6);
    Bitboard pawns = pieces[offset + 0];
    const Bitboard enemy = (side == 0 ? bOcc : wOcc);

    while (pawns) {
        int from = __builtin_ctzll(pawns);
        pawns &= pawns - 1;

        int r = from / 8;
        int f = from % 8;
        int nr = (side == 0 ? r + 1 : r - 1);
        if (nr < 0 || nr >= 8) continue;

        bool isPromotion = (side == 0 && nr == 7) || (side == 1 && nr == 0);

        // forward one
        int to = nr * 8 + f;
        Bitboard toMask = 1ULL << to;
        if (!(allOcc & toMask)) {
            if (isPromotion) {
                moves.emplace_back(from, to, 1);
                moves.emplace_back(from, to, 2);
                moves.emplace_back(from, to, 3);
                moves.emplace_back(from, to, 4);
            } else {
                moves.emplace_back(from, to);
            }
        }

        // captures
        if (f - 1 >= 0) {
            int cap = nr * 8 + (f - 1);
            Bitboard capMask = 1ULL << cap;
            if (enemy & capMask) {
                if (isPromotion) {
                    moves.emplace_back(from, cap, 1);
                    moves.emplace_back(from, cap, 2);
                    moves.emplace_back(from, cap, 3);
                    moves.emplace_back(from, cap, 4);
                } else {
                    moves.emplace_back(from, cap);
                }
            }
        }

        if (f + 1 < 8) {
            int cap = nr * 8 + (f + 1);
            Bitboard capMask = 1ULL << cap;
            if (enemy & capMask) {
                if (isPromotion) {
                    moves.emplace_back(from, cap, 1);
                    moves.emplace_back(from, cap, 2);
                    moves.emplace_back(from, cap, 3);
                    moves.emplace_back(from, cap, 4);
                } else {
                    moves.emplace_back(from, cap);
                }
            }
        }
    }
}

Position Position::makeMove(const Move &m) const {
    Position next = *this;

    const int offset = (side == 0 ? 0 : 6);
    const int enemyOffset = (side == 0 ? 6 : 0);

    int movingPieceIndex = -1;
    for (int i = 0; i < 6; i++) {
        if (next.pieces[offset + i] & (1ULL << m.from)) {
            movingPieceIndex = offset + i;
            break;
        }
    }

    if (movingPieceIndex == -1) {
        return next;
    }

    // remove moving piece from origin
    next.pieces[movingPieceIndex] &= ~(1ULL << m.from);

    // remove captured enemy
    for (int i = 0; i < 6; i++) {
        if (next.pieces[enemyOffset + i] & (1ULL << m.to)) {
            next.pieces[enemyOffset + i] &= ~(1ULL << m.to);
            break;
        }
    }

    // handle promotion: replace pawn with promoted piece
    if (m.promotion != -1) {
        if (side == 0) {
            int promoted = 4;
            switch (m.promotion) {
                case 1: promoted = 1; break;
                case 2: promoted = 2; break;
                case 3: promoted = 3; break;
                case 4: promoted = 4; break;
                default: promoted = 4; break;
            }
            next.pieces[promoted] |= (1ULL << m.to);
        } else {
            int promoted = 10;
            switch (m.promotion) {
                case 1: promoted = 7; break;
                case 2: promoted = 8; break;
                case 3: promoted = 9; break;
                case 4: promoted = 10; break;
                default: promoted = 10; break;
            }
            next.pieces[promoted] |= (1ULL << m.to);
        }
    } else {
        next.pieces[movingPieceIndex] |= (1ULL << m.to);
    }

    // recompute occupancies
    next.wOcc = next.pieces[0] | next.pieces[1] | next.pieces[2] |
                next.pieces[3] | next.pieces[4] | next.pieces[5];
    next.bOcc = next.pieces[6] | next.pieces[7] | next.pieces[8] |
                next.pieces[9] | next.pieces[10] | next.pieces[11];
    next.allOcc = next.wOcc | next.bOcc;

    // flip side
    next.side = 1 - side;
    return next;
}

std::string Position::to_string() const {
    std::string board(64, '.');
    const char *symbols = "PNBRQKpnbrqk";

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
    std::string placement;
    std::string activeColor;
    std::string castling;
    std::string enPassant;
    int halfmove = 0;
    int fullmove = 1;

    iss >> placement >> activeColor >> castling >> enPassant >> halfmove >> fullmove;

    int rank = 7;
    int file = 0;
    for (char c : placement) {
        if (c == '/') {
            rank--;
            file = 0;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            file += c - '0';
            continue;
        }

        int square = rank * 8 + file;
        int index = -1;
        switch (c) {
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
            default: break;
        }
        if (index != -1) {
            pos.pieces[index] |= (1ULL << square);
        }
        file++;
    }

    pos.wOcc = pos.pieces[0] | pos.pieces[1] | pos.pieces[2] |
               pos.pieces[3] | pos.pieces[4] | pos.pieces[5];
    pos.bOcc = pos.pieces[6] | pos.pieces[7] | pos.pieces[8] |
               pos.pieces[9] | pos.pieces[10] | pos.pieces[11];
    pos.allOcc = pos.wOcc | pos.bOcc;

    pos.side = (activeColor == "w" ? 0 : 1);
    return pos;
}

bool Position::is_threefold_repetition() const {
    const uint64_t h = getZobristHash();
    int count = 0;
    for (uint64_t hh : gameHashes) {
        if (hh == h) count++;
    }
    return count >= 3;
}

bool Position::isInsufficientMaterial() const {
    // A conservative (and common) insufficient material test:
    // - Any pawn, rook, or queen means mate is possible.
    // - Otherwise, check if either side has known mating material.

    auto pop = [](uint64_t bb) -> int { return __builtin_popcountll(bb); };

    const int wP = pop(pieces[0]);
    const int wN = pop(pieces[1]);
    const int wB = pop(pieces[2]);
    const int wR = pop(pieces[3]);
    const int wQ = pop(pieces[4]);

    const int bP = pop(pieces[6]);
    const int bN = pop(pieces[7]);
    const int bB = pop(pieces[8]);
    const int bR = pop(pieces[9]);
    const int bQ = pop(pieces[10]);

    if ((wP + bP) > 0) return false;
    if ((wR + bR) > 0) return false;
    if ((wQ + bQ) > 0) return false;

    auto bishops_have_both_colors = [](uint64_t bishopsBB) -> bool {
        bool hasLight = false;
        bool hasDark = false;
        uint64_t bb = bishopsBB;
        while (bb) {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            int r = sq / 8;
            int f = sq % 8;
            bool isLight = ((r + f) % 2) == 0;
            if (isLight) hasLight = true;
            else hasDark = true;
            if (hasLight && hasDark) return true;
        }
        return false;
    };

    auto has_mating_material = [&](bool white) -> bool {
        const int N = white ? wN : bN;
        const uint64_t bishopsBB = white ? pieces[2] : pieces[8];

        // Bishop + Knight
        if (N >= 1 && pop(bishopsBB) >= 1) return true;

        // Two bishops on opposite colors
        if (pop(bishopsBB) >= 2 && bishops_have_both_colors(bishopsBB)) return true;

        // Three knights can (in principle) deliver mate.
        if (N >= 3) return true;

        return false;
    };

    const bool whiteCanMate = has_mating_material(true);
    const bool blackCanMate = has_mating_material(false);
    return !whiteCanMate && !blackCanMate;
}
