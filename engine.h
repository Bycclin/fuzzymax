#ifndef ENGINE_H
#define ENGINE_H

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Global variable to store position hashes for threefold repetition.
extern std::vector<uint64_t> gameHashes;

// -----------------------------------------------------------------------------
// CHESS DATA STRUCTURES
// -----------------------------------------------------------------------------

struct Move {
    int from;
    int to;
    int promotion; // -1 = none, 1=n,2=b,3=r,4=q

    Move(int f, int t, int p = -1) : from(f), to(t), promotion(p) {}
    Move() : from(-1), to(-1), promotion(-1) {}
};

class Position {
public:
    using Bitboard = uint64_t;
    enum Piece { P, N, B, R, Q, K, p, n, b, r, q, k };

    std::array<Bitboard, 12> pieces{};
    Bitboard wOcc = 0;
    Bitboard bOcc = 0;
    Bitboard allOcc = 0;
    int side = 0; // 0 for white, 1 for black

    // Constructors and basic methods.
    Position();
    static Position create_start_position();
    Position rotate() const;
    static Bitboard flip(Bitboard bb);

    // Move generation and game state methods.
    std::vector<Move> genMoves() const;
    void generateSlidingMoves(std::vector<Move>& moves, int pieceIdx, const int dir[][2], int numDirections) const;
    void generateKnightMoves(std::vector<Move>& moves) const;
    void generateKingMoves(std::vector<Move>& moves) const;
    void generatePawnMoves(std::vector<Move>& moves) const;
    Position makeMove(const Move &m) const;
    std::string to_string() const;

    // Game state checks.
    bool is_checkmate() const;
    bool is_stalemate() const;
    bool is_threefold_repetition() const;
    bool isInsufficientMaterial() const;
    std::string current_turn() const;
    bool is_in_check() const;

    // Zobrist hash for the position.
    uint64_t getZobristHash() const;

    // Parse a FEN string and return the corresponding Position.
    static Position fromFEN(const std::string &fen);

    // Directional arrays.
    static const int bishopDir[4][2];
    static const int rookDir[4][2];
    static const int queenDir[8][2];
};

// -----------------------------------------------------------------------------
// Utility: move conversion
// -----------------------------------------------------------------------------

inline std::string move_to_uci(const Move &m) {
    if (m.from < 0 || m.to < 0) return "0000";

    auto sq_to_str = [](int sq) -> std::string {
        char file = static_cast<char>('a' + (sq % 8));
        char rank = static_cast<char>('1' + (sq / 8));
        return std::string{file, rank};
    };

    std::string uci = sq_to_str(m.from) + sq_to_str(m.to);

    if (m.promotion != -1) {
        char prom = 'q';
        switch (m.promotion) {
            case 1: prom = 'n'; break;
            case 2: prom = 'b'; break;
            case 3: prom = 'r'; break;
            case 4: prom = 'q'; break;
            default: prom = 'q'; break;
        }
        uci.push_back(prom);
    }

    return uci;
}

#endif // ENGINE_H
