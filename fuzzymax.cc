#include "engine.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <limits>
#include <algorithm>
#include <array>
#include <random>
bool use_bandit_search = false;
int MaxDepth = 25;
bool stop_search = false;
std::vector<Move> getMoves(const Position* pos) {
    Move* movesArray = nullptr;
    size_t movesCount = 0;
    pos->genMoves(&movesArray, &movesCount);
    std::vector<Move> moves;
    if (movesArray != nullptr && movesCount > 0) {
        moves.assign(movesArray, movesArray + movesCount);
    }
    return moves;
}
int evaluate(const Position* pos) {
    std::array<int, 12> pieceValues = {100, 320, 330, 500, 900, 20000,
                                         -100, -320, -330, -500, -900, -20000};
    int score = 0;
    for (int i = 0; i < 12; i++) {
        score += pieceValues[i] * __builtin_popcountll(pos->pieces[i]);
    }
    return score;
}
double SMTS(const Position* pos, int depth, std::vector<Move>& pv) {
    double beta = 1.0;
    if (depth == 0) {
        pv.clear();
        return evaluate(pos);
    }
    std::vector<Move> moves = getMoves(pos);
    if (moves.empty()) {
        pv.clear();
        return evaluate(pos);
    }
    std::vector<double> child_vals;
    std::vector<std::vector<Move>> child_pvs;
    for (auto move : moves) {
        Position* child = pos->makeMove(move);
        std::vector<Move> child_pv;
        double val = -SMTS(child, depth - 1, child_pv);
        child_vals.push_back(val);
        child_pvs.push_back(child_pv);
    }
    double max_val = *std::max_element(child_vals.begin(), child_vals.end());
    double total_weight = 0.0;
    std::vector<double> weights;
    for (double val : child_vals) {
        double w = exp(beta * (val - max_val));
        weights.push_back(w);
        total_weight += w;
    }
    double r = ((double) rand() / RAND_MAX) * total_weight;
    double accum = 0.0;
    int chosen_index = 0;
    for (size_t i = 0; i < weights.size(); i++) {
        accum += weights[i];
        if (accum >= r) {
            chosen_index = i;
            break;
        }
    }
    pv.clear();
    pv.push_back(moves[chosen_index]);
    for (auto m : child_pvs[chosen_index])
        pv.push_back(m);
    
    double softmax_eval = (1.0 / beta) * log(total_weight) + max_val;
    return softmax_eval;
}
double MABS(const Position* pos, int depth, std::vector<Move>& pv) {
    if (depth == 0) {
        pv.clear();
        return evaluate(pos);
    }
    
    std::vector<Move> moves = getMoves(pos);
    if (moves.empty()) {
        pv.clear();
        return evaluate(pos);
    }
    int n = moves.size();
    const int iterations = 100;
    std::vector<int> arm_plays(n, 0);
    std::vector<double> arm_total_rewards(n, 0.0);
    std::vector<double> arm_best_reward(n, -std::numeric_limits<double>::infinity());
    std::vector<std::vector<Move>> arm_best_pv(n);
    for (int iter = 1; iter <= iterations; iter++) {
        int selected_arm = -1;
        double best_ucb = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < n; i++) {
            double avg = (arm_plays[i] > 0) ? (arm_total_rewards[i] / arm_plays[i]) : 0.0;
            double ucb = (arm_plays[i] > 0) ?
                         avg + sqrt(2 * log(iter) / arm_plays[i]) :
                         std::numeric_limits<double>::infinity();
            if (ucb > best_ucb) {
                best_ucb = ucb;
                selected_arm = i;
            }
        }
        Move move = moves[selected_arm];
        Position* child = pos->makeMove(move);
        std::vector<Move> local_pv;
        double reward = -MABS(child, depth - 1, local_pv);
        arm_plays[selected_arm]++;
        arm_total_rewards[selected_arm] += reward;
        if (arm_plays[selected_arm] == 1 || reward > arm_best_reward[selected_arm]) {
            arm_best_reward[selected_arm] = reward;
            arm_best_pv[selected_arm] = local_pv;
        }
    }
    int best_arm = 0;
    double best_avg = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; i++) {
        double avg = (arm_plays[i] > 0) ? (arm_total_rewards[i] / arm_plays[i])
                                        : -std::numeric_limits<double>::infinity();
        if (avg > best_avg) {
            best_avg = avg;
            best_arm = i;
        }
    }
    pv.clear();
    pv.push_back(moves[best_arm]);
    for (auto m : arm_best_pv[best_arm])
        pv.push_back(m);
    return best_avg;
}
Move uci_to_move(const std::string &moveStr) {
    if (moveStr.size() < 4)
        return Move(-1, -1);
    int fromFile = moveStr[0] - 'a';
    int fromRank = moveStr[1] - '1';
    int toFile   = moveStr[2] - 'a';
    int toRank   = moveStr[3] - '1';
    int from = fromRank * 8 + fromFile;
    int to   = toRank * 8 + toFile;
    int promotion = -1;
    if (moveStr.size() >= 5) {
        char prom = moveStr[4];
        switch(prom) {
            case 'n': promotion = 1; break;
            case 'b': promotion = 2; break;
            case 'r': promotion = 3; break;
            case 'q': promotion = 4; break;
            default:  promotion = 4; break;
        }
    }
    return Move(from, to, promotion);
}
uint64_t get_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
         std::chrono::steady_clock::now().time_since_epoch()).count();
}
extern "C" int cpp_main(int argc, char **argv) {
    using namespace std;
    ios::sync_with_stdio(false);
    string line;
    Position* pos = Position::create_start_position();
    int movetime = 0;
    while(getline(cin, line)) {
        if (line.substr(0, 3) == "uci") {
            cout << "id name fuzzy-Max (SMTS & MABS integrated)" << "\n";
            cout << "option name MAB type check default false" << "\n";
            cout << "uciok" << "\n";
        }
        else if (line.substr(0, 7) == "isready") {
            cout << "readyok" << "\n";
        }
        else if (line.substr(0, 10) == "ucinewgame") {
            pos = Position::create_start_position();
        }
        else if (line.substr(0, 8) == "position") {
            istringstream iss(line);
            string token;
            iss >> token;
            string posType;
            iss >> posType;
            if (posType == "startpos") {
                pos = Position::create_start_position();
                string movesToken;
                if (iss >> movesToken && movesToken == "moves") {
                    while (iss >> token) {
                        Move m = uci_to_move(token);
                        pos = pos->makeMove(m);
                    }
                }
            }
            else if (posType == "fen") {
                string fen;
                vector<string> fenParts;
                while (iss >> token && token != "moves") {
                    fenParts.push_back(token);
                }
                for (size_t i = 0; i < fenParts.size(); i++) {
                    fen += fenParts[i] + (i + 1 < fenParts.size() ? " " : "");
                }
                pos = Position::fromFEN(fen.c_str());
                if (token == "moves") {
                    while (iss >> token) {
                        Move m = uci_to_move(token);
                        pos = pos->makeMove(m);
                    }
                }
            }
        }
        else if (line.substr(0, 2) == "go") {
            movetime = 0;
            size_t posMovetime = line.find("movetime");
            if (posMovetime != string::npos) {
                istringstream iss(line.substr(posMovetime + 9));
                iss >> movetime;
            }
            uint64_t start_time = get_time_ms();
            int current_depth = 0;
            std::vector<Move> best_pv;
            double best_eval = 0.0;
            while (true) {
                current_depth++;
                if (stop_search) break;
                std::vector<Move> pv;
                double eval;
                if (use_bandit_search)
                    eval = MABS(pos, current_depth, pv);
                else
                    eval = SMTS(pos, current_depth, pv);
                
                cout << "info depth " << current_depth << " eval " << eval << " pv ";
                for (auto &m : pv) {
                    cout << move_to_uci(m) << " ";
                }
                cout << "\n";
                
                best_pv = pv;
                best_eval = eval;
                
                if (movetime > 0) {
                    uint64_t current_time = get_time_ms();
                    if (current_time - start_time > static_cast<uint64_t>(movetime))
                        break;
                } else {
                    if (current_depth >= MaxDepth)
                        break;
                }
            }
            if (!best_pv.empty()) {
                cout << "bestmove " << move_to_uci(best_pv[0]) << "\n";
                pos = pos->makeMove(best_pv[0]);
            } else {
                cout << "bestmove 0000" << "\n";
            }
        }
        else if (line.substr(0, 9) == "setoption") {
            if (line.find("name MAB") != string::npos) {
                if (line.find("value true") != string::npos)
                    use_bandit_search = true;
                else
                    use_bandit_search = false;
            }
        }
        else if (line.substr(0, 4) == "stop") {
            stop_search = true;
        }
        else if (line.substr(0, 4) == "quit") {
            break;
        }
    }
    return 0;
}
