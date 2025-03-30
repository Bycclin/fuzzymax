#ifndef ENGINE_H
#define ENGINE_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <array>

// Global variable to store position hashes for threefold repetition.
extern std::vector<uint64_t> gameHashes;

// -----------------------------------------------------------------------------
// CHESS DATA STRUCTURES
// -----------------------------------------------------------------------------

struct Move {
    int from;
    int to;
    int promotion = -1;
    // Constructor
    Move(int from, int to, int promotion = -1) : from(from), to(to), promotion(promotion) {}
};

class Position {
public:
    using Bitboard = uint64_t;
    enum Piece { P, N, B, R, Q, K, p, n, b, r, q, k };
    
    std::array<Bitboard, 12> pieces;
    Bitboard wOcc, bOcc, allOcc;
    int side; // 0 for white, 1 for black
    
    // Constructors and basic methods.
    Position();
    static Position* create_start_position();
    Position* rotate() const;
    static Bitboard flip(Bitboard bb);
    
    // Move generation and game state methods.
    // (The D module now exports move generation via output parameters.)
    void genMoves(Move** outMoves, size_t* outCount) const;
    void generateSlidingMoves(Move** outMoves, size_t* outCount, int pieceIdx, const int (*dir)[2], int numDirections) const;
    void generateKnightMoves(Move** outMoves, size_t* outCount) const;
    void generateKingMoves(Move** outMoves, size_t* outCount) const;
    void generatePawnMoves(Move** outMoves, size_t* outCount) const;
    
    // Changed to return a pointer.
    // --- FIX: Pass Move by value instead of by const reference to match the D export ---
    Position* makeMove(const Move m) const;
    
    // (These functions are not used by our D module; the D version exports its own C string routines.)
    std::string to_string() const;
    std::string current_turn() const;
    bool is_in_check() const;
    
    // Game state checks.
    bool is_checkmate() const;
    bool is_stalemate() const;
    bool is_threefold_repetition() const;
    bool isInsufficientMaterial() const;
    
    // Zobrist hashing.
    uint64_t getZobristHash() const;
    
    // Parse a FEN string and return the corresponding position.
    // Note: The signature now accepts a C string.
    static Position* fromFEN(const char* fen);
    
    // Directional arrays.
    static const int bishopDir[4][2];
    static const int rookDir[4][2];
    static const int queenDir[8][2];
};

// -----------------------------------------------------------------------------
// NEURAL-NET PARAMS AND ENGINE FUNCTIONS (declarations)
// -----------------------------------------------------------------------------

struct NNParams {
    std::vector<double> w1;
    std::vector<double> b1;
    std::vector<double> w_policy;
    std::vector<double> b_policy;
    std::vector<double> w_value;
    std::vector<double> b_value;
};

bool load_weights(const std::string &filename, NNParams &params);
bool save_weights(const std::string &filename, const NNParams &params);
std::pair<std::vector<double>, double> forward_pass(const NNParams &params, const std::vector<double> &inputData);
std::vector<double> position_to_input(const Position* pos);

extern std::string currentWeightsFile;

inline std::string move_to_uci(const Move &m) {
    if (m.from < 0 || m.to < 0) return "0000";
    auto sq_to_str = [](int sq) -> std::string {
        char file = 'a' + (sq % 8);
        char rank = '1' + (sq / 8);
        return std::string{file, rank};
    };
    return sq_to_str(m.from) + sq_to_str(m.to);
}

inline std::string move_to_uci_with_promotion(const Position & /* pos */, const Move &m) {
    std::string uci = move_to_uci(m);
    if (uci.size() != 4)
        return uci;
    if (m.promotion != -1) {
        char prom;
        int promotionPiece = m.promotion;
        if (promotionPiece >= 6) {
            promotionPiece -= 6;
        }
        switch (promotionPiece) {
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

inline std::string get_default_weights_file() {
    if (!currentWeightsFile.empty())
        return currentWeightsFile;
    std::ifstream config("default_weights.conf");
    if (config) {
        std::string filename;
        std::getline(config, filename);
        currentWeightsFile = filename;
        return filename;
    }
    return "";
}

inline void set_default_weights_file(const std::string &filename) {
    currentWeightsFile = filename;
    std::ofstream config("default_weights.conf");
    if (config) {
        config << filename;
    }
}

// -----------------------------------------------------------------------------
// MCTS and Search Declarations
// -----------------------------------------------------------------------------

// Forward declaration for MCTSNode used by search functions.
struct MCTSNode;

std::pair<Move, std::vector<double>> selfplay_search_move(
    const Position* pos,
    const NNParams &params,
    int moveTimeMillis,
    int iterationsPerBatch);

Move iterative_deepening_search(
    const Position* pos,
    const NNParams &params,
    int moveTimeMillis,
    int iterationsPerBatch);

std::pair<Move, MCTSNode*> search_best_move_MCTS_with_root(
    const Position* pos,
    const NNParams &params,
    int maxIters,
    std::chrono::steady_clock::time_point endTime);

void cleanup_nodes();

// Utility function to get the principal variation string.
std::string get_pv_string(MCTSNode *root, const NNParams &params);

// Utility for writing game data.
void write_game_to_file(
    const std::vector<std::vector<double>> &positions,
    const std::vector<std::vector<double>> &policies,
    const std::vector<double> &values);

// Global atomic flag to control search stop requests.
extern std::atomic<bool> stopRequested;

#endif // ENGINE_H
