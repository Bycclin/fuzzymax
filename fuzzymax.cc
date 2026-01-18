// fuzzymax.cc
#include "engine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Define the global gameHashes variable.
std::vector<uint64_t> gameHashes;

static bool use_bandit_search = false;
static int MaxDepth = 25;
static std::atomic<bool> stop_search{false};

static std::vector<Move> getMoves(const Position* pos) {
    return pos->genMoves();
}

// Material-only evaluation in centipawns (positive = good for side to move).
static int evaluate(const Position* pos) {
    // White: P N B R Q K, Black: p n b r q k
    static constexpr std::array<int, 12> pieceValues = {
        100, 320, 330, 500, 900, 20000,
        -100, -320, -330, -500, -900, -20000
    };

    int scoreWhiteMinusBlack = 0;
    for (int i = 0; i < 12; i++) {
        scoreWhiteMinusBlack += pieceValues[i] * __builtin_popcountll(pos->pieces[i]);
    }

    // Convert to side-to-move perspective for negamax correctness.
    return (pos->side == 0) ? scoreWhiteMinusBlack : -scoreWhiteMinusBlack;
}

static double SMTS(const Position* pos, int depth, std::vector<Move>& pv, std::mt19937 &rng) {
    if (stop_search.load(std::memory_order_relaxed)) {
        pv.clear();
        return static_cast<double>(evaluate(pos));
    }

    if (depth == 0) {
        pv.clear();
        return static_cast<double>(evaluate(pos));
    }

    std::vector<Move> moves = getMoves(pos);
    if (moves.empty()) {
        pv.clear();
        return static_cast<double>(evaluate(pos));
    }

    const double beta = 1.0;

    std::vector<double> child_vals;
    child_vals.reserve(moves.size());
    std::vector<std::vector<Move>> child_pvs;
    child_pvs.reserve(moves.size());

    for (const auto &move : moves) {
        Position child = pos->makeMove(move);
        std::vector<Move> child_pv;
        double val = -SMTS(&child, depth - 1, child_pv, rng);
        child_vals.push_back(val);
        child_pvs.push_back(std::move(child_pv));

        if (stop_search.load(std::memory_order_relaxed)) break;
    }

    // If we were interrupted mid-loop, fall back to best-so-far.
    if (child_vals.empty()) {
        pv.clear();
        return static_cast<double>(evaluate(pos));
    }

    double max_val = *std::max_element(child_vals.begin(), child_vals.end());
    double total_weight = 0.0;

    std::vector<double> weights;
    weights.reserve(child_vals.size());

    for (double val : child_vals) {
        double w = std::exp(beta * (val - max_val));
        weights.push_back(w);
        total_weight += w;
    }

    std::uniform_real_distribution<double> dist(0.0, total_weight);
    double r = dist(rng);

    double accum = 0.0;
    size_t chosen_index = 0;
    for (size_t i = 0; i < weights.size(); i++) {
        accum += weights[i];
        if (accum >= r) {
            chosen_index = i;
            break;
        }
    }

    pv.clear();
    pv.push_back(moves[chosen_index]);
    for (const auto &m : child_pvs[chosen_index]) {
        pv.push_back(m);
    }

    double softmax_eval = (1.0 / beta) * std::log(total_weight) + max_val;
    return softmax_eval;
}

static double MABS(const Position* pos, int depth, std::vector<Move>& pv, std::mt19937 &rng) {
    if (stop_search.load(std::memory_order_relaxed)) {
        pv.clear();
        return static_cast<double>(evaluate(pos));
    }

    if (depth == 0) {
        pv.clear();
        return static_cast<double>(evaluate(pos));
    }

    std::vector<Move> moves = getMoves(pos);
    if (moves.empty()) {
        pv.clear();
        return static_cast<double>(evaluate(pos));
    }

    const int n = static_cast<int>(moves.size());
    const int iterations = 100;

    std::vector<int> plays(n, 0);
    std::vector<double> totalReward(n, 0.0);
    std::vector<double> bestReward(n, -std::numeric_limits<double>::infinity());
    std::vector<std::vector<Move>> bestPV(n);

    for (int iter = 1; iter <= iterations; iter++) {
        if (stop_search.load(std::memory_order_relaxed)) break;

        int selected = -1;
        double bestUcb = -std::numeric_limits<double>::infinity();

        for (int i = 0; i < n; i++) {
            double ucb;
            if (plays[i] == 0) {
                ucb = std::numeric_limits<double>::infinity();
            } else {
                double avg = totalReward[i] / plays[i];
                ucb = avg + std::sqrt(2.0 * std::log(static_cast<double>(iter)) / plays[i]);
            }

            if (ucb > bestUcb) {
                bestUcb = ucb;
                selected = i;
            }
        }

        if (selected < 0) break;

        Position child = pos->makeMove(moves[selected]);
        std::vector<Move> localPV;
        double reward = -MABS(&child, depth - 1, localPV, rng);

        plays[selected]++;
        totalReward[selected] += reward;

        if (plays[selected] == 1 || reward > bestReward[selected]) {
            bestReward[selected] = reward;
            bestPV[selected] = std::move(localPV);
        }
    }

    // Choose arm by highest average reward (fallback to bestReward if unplayed).
    int bestArm = 0;
    double bestAvg = -std::numeric_limits<double>::infinity();

    for (int i = 0; i < n; i++) {
        double avg;
        if (plays[i] > 0) {
            avg = totalReward[i] / plays[i];
        } else {
            avg = bestReward[i];
        }
        if (avg > bestAvg) {
            bestAvg = avg;
            bestArm = i;
        }
    }

    pv.clear();
    pv.push_back(moves[bestArm]);
    for (const auto &m : bestPV[bestArm]) {
        pv.push_back(m);
    }

    return bestAvg;
}

static Move uci_to_move(const std::string &moveStr) {
    if (moveStr.size() < 4) return Move(-1, -1);

    int fromFile = moveStr[0] - 'a';
    int fromRank = moveStr[1] - '1';
    int toFile   = moveStr[2] - 'a';
    int toRank   = moveStr[3] - '1';

    int from = fromRank * 8 + fromFile;
    int to   = toRank * 8 + toFile;

    int promotion = -1;
    if (moveStr.size() >= 5) {
        char prom = static_cast<char>(std::tolower(static_cast<unsigned char>(moveStr[4])));
        switch (prom) {
            case 'n': promotion = 1; break;
            case 'b': promotion = 2; break;
            case 'r': promotion = 3; break;
            case 'q': promotion = 4; break;
            default:  promotion = 4; break;
        }
    }

    return Move(from, to, promotion);
}

static uint64_t get_time_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

extern "C" int cpp_main(int /*argc*/, char ** /*argv*/) {
    using namespace std;

    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string line;
    Position pos = Position::create_start_position();

    gameHashes.clear();
    gameHashes.push_back(pos.getZobristHash());

    int movetime = 0;

    // RNG used by SMTS/MABS.
    std::mt19937 rng(static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    while (getline(cin, line)) {
        if (line == "uci") {
            cout << "id name fuzzy-Max (SMTS & MABS integrated)" << '\n';
            cout << "option name MAB type check default false" << '\n';
            cout << "uciok" << '\n';
        }
        else if (line.rfind("isready", 0) == 0) {
            cout << "readyok" << '\n';
        }
        else if (line.rfind("ucinewgame", 0) == 0) {
            pos = Position::create_start_position();
            gameHashes.clear();
            gameHashes.push_back(pos.getZobristHash());
            stop_search.store(false, std::memory_order_relaxed);
        }
        else if (line.rfind("position", 0) == 0) {
            istringstream iss(line);
            string token;

            iss >> token; // position
            string posType;
            iss >> posType;

            if (posType == "startpos") {
                pos = Position::create_start_position();
                gameHashes.clear();
                gameHashes.push_back(pos.getZobristHash());

                string movesToken;
                if (iss >> movesToken && movesToken == "moves") {
                    while (iss >> token) {
                        Move m = uci_to_move(token);
                        pos = pos.makeMove(m);
                        gameHashes.push_back(pos.getZobristHash());
                    }
                }
            }
            else if (posType == "fen") {
                vector<string> fenParts;
                while (iss >> token && token != "moves") {
                    fenParts.push_back(token);
                }

                string fen;
                for (size_t i = 0; i < fenParts.size(); i++) {
                    fen += fenParts[i];
                    if (i + 1 < fenParts.size()) fen += ' ';
                }

                pos = Position::fromFEN(fen);
                gameHashes.clear();
                gameHashes.push_back(pos.getZobristHash());

                if (token == "moves") {
                    while (iss >> token) {
                        Move m = uci_to_move(token);
                        pos = pos.makeMove(m);
                        gameHashes.push_back(pos.getZobristHash());
                    }
                }
            }
        }
        else if (line.rfind("go", 0) == 0) {
            movetime = 0;
            int target_depth = MaxDepth;
            int wtime = 0;
            int btime = 0;

            istringstream go_iss(line);
            string go_token;
            go_iss >> go_token; // go

            while (go_iss >> go_token) {
                if (go_token == "depth") {
                    go_iss >> target_depth;
                } else if (go_token == "movetime") {
                    go_iss >> movetime;
                } else if (go_token == "wtime") {
                    go_iss >> wtime;
                } else if (go_token == "btime") {
                    go_iss >> btime;
                }
            }

            if (movetime == 0) {
                movetime = (pos.side == 0 ? wtime : btime);
            }

            const uint64_t start_time = get_time_ms();
            stop_search.store(false, std::memory_order_relaxed);

            std::thread timer_thread;
            if (movetime > 0) {
                const uint64_t searchTime = static_cast<uint64_t>(movetime) / 15;
                timer_thread = std::thread([start_time, searchTime]() {
                    // sleep-loop to avoid burning CPU
                    while (!stop_search.load(std::memory_order_relaxed) &&
                           (get_time_ms() - start_time < searchTime)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    stop_search.store(true, std::memory_order_relaxed);
                });
            }

            int current_depth = 0;
            std::vector<Move> best_pv;

            while (true) {
                current_depth++;
                if (stop_search.load(std::memory_order_relaxed)) break;

                std::vector<Move> pv;
                double eval;

                if (use_bandit_search) {
                    eval = MABS(&pos, current_depth, pv, rng);
                } else {
                    eval = SMTS(&pos, current_depth, pv, rng);
                }

                cout << "info depth " << current_depth
                     << " score cp " << static_cast<int>(std::lround(eval))
                     << " pv ";

                for (const auto &m : pv) {
                    cout << move_to_uci(m) << ' ';
                }
                cout << '\n' << std::flush;

                if (!pv.empty()) {
                    best_pv = pv;
                }

                if (movetime == 0 && current_depth >= target_depth) {
                    break;
                }
            }

            if (timer_thread.joinable()) {
                timer_thread.join();
            }

            if (!best_pv.empty()) {
                cout << "bestmove " << move_to_uci(best_pv[0]) << '\n';
                pos = pos.makeMove(best_pv[0]);
                gameHashes.push_back(pos.getZobristHash());
            } else {
                cout << "bestmove 0000" << '\n';
            }
        }
        else if (line.rfind("setoption", 0) == 0) {
            if (line.find("name MAB") != string::npos) {
                use_bandit_search = (line.find("value true") != string::npos);
            }
        }
        else if (line.rfind("stop", 0) == 0) {
            stop_search.store(true, std::memory_order_relaxed);
        }
        else if (line.rfind("quit", 0) == 0) {
            break;
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    return cpp_main(argc, argv);
}
