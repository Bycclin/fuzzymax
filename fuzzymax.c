#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <mach/mach_time.h> // macOS time
#include <stdint.h>

int StartKey;

#define EMPTY 0
#define WHITE 8
#define BLACK 16

#define STATE 64

/* Global variables */
int Side;
int Move;
int PromPiece;
int Result;
int TimeLeft;   /* Time left (in milliseconds) provided by UCI "go movetime" */
int MovesLeft;
int MaxDepth = 25;  /* default search depth when no time limit is provided */
int Post;
int Fifty;
int UnderProm;

int Ticks, tlim;

int GameHistory[1024];
char HistoryBoards[1024][STATE];
int GamePtr, HistPtr;

/* Global move storage (from-square and to-square indices) */
int K, L;

#define M 136
#define S 128
#define I 8000  /* used as a large constant */

/* piece values for piece types 0..7 */
int w[] = {0,2,2,7,-1,8,12,23};
int o[] = {
    -16,-15,-17,0,1,16,0,1,16,15,17,0,14,18,31,33,0,
    7,-1,11,6,8,3,6,
    6,3,5,7,4,5,3,6
};
char b[129];   /* board representation */
int T[1035];   /* hash translation table (unused in softmax version) */

char n[] = ".?+nkbrq?*?NKBRQ"; /* piece symbols */

/* Global variable for root search depth */
int root_depth;

/* New global variable: the side the engine is assigned to play */
int engine_side = EMPTY;

/* New global flag to choose search algorithm.
   0 = standard softmax tree search (SMTS),
   1 = multi-armed bandit search (MABS) */
int use_bandit_search = 0;

/* ---------- New Bit-Board Code Added Below ---------- */

/* Global bitboard variables for white pieces */
unsigned long long white_pawns   = 0ULL;
unsigned long long white_knights = 0ULL;
unsigned long long white_bishops = 0ULL;
unsigned long long white_rooks   = 0ULL;
unsigned long long white_queens  = 0ULL;
unsigned long long white_kings   = 0ULL;

/* Global bitboard variables for black pieces */
unsigned long long black_pawns   = 0ULL;
unsigned long long black_knights = 0ULL;
unsigned long long black_bishops = 0ULL;
unsigned long long black_rooks   = 0ULL;
unsigned long long black_queens  = 0ULL;
unsigned long long black_kings   = 0ULL;

/* Convert a 0x88 board square (0..127 with off-board skipped) to a bit index (0..63).
   We map the board so that bit 0 = a1 and bit 63 = h8. */
int square_to_bit_index(int sq) {
    int rank = sq >> 4;   // rank 0 (top) to 7 (bottom)
    int file = sq & 7;    // file 0 to 7
    return (7 - rank) * 8 + file; // a1 is bit0, a8 is bit56, h8 is bit63
}

/* Update all bitboards from the board array 'b' */
void update_bitboards() {
    white_pawns = white_knights = white_bishops = white_rooks = white_queens = white_kings = 0ULL;
    black_pawns = black_knights = black_bishops = black_rooks = black_queens = black_kings = 0ULL;
    
    int i;
    for(i = 0; i < 128; i++) {
        if(i & 0x88) continue;  /* Skip off-board squares */
        int piece = b[i];
        if(piece == 0) continue;
        int bit = square_to_bit_index(i);
        unsigned long long mask = 1ULL << bit;
        int type = piece & 7;
        if(piece & WHITE) {
            if(type == 1)
                white_pawns |= mask;
            else if(type == 2)
                white_knights |= mask;
            else if(type == 3)
                white_bishops |= mask;
            else if(type == 4)
                white_rooks |= mask;
            else if(type == 5)
                white_queens |= mask;
            else if(type == 6)
                white_kings |= mask;
        } else if(piece & BLACK) {
            if(type == 1)
                black_pawns |= mask;
            else if(type == 2)
                black_knights |= mask;
            else if(type == 3)
                black_bishops |= mask;
            else if(type == 4)
                black_rooks |= mask;
            else if(type == 5)
                black_queens |= mask;
            else if(type == 6)
                black_kings |= mask;
        }
    }
}

/* ---------- End of Bit-Board Code ---------- */

/* macOS GetTickCount replacement */
unsigned long GetTickCount() {
    static mach_timebase_info_data_t timebase_info;
    if (timebase_info.denom == 0) {
        (void) mach_timebase_info(&timebase_info);
    }
    uint64_t absolute_time = mach_absolute_time();
    uint64_t nanoseconds = absolute_time * timebase_info.numer / timebase_info.denom;
    return nanoseconds / 1000000; // Convert nanoseconds to milliseconds
}

/*---------------------------------------------------------*/
/* Static evaluation function: simple sum over board       */
/*---------------------------------------------------------*/
int static_eval() {
    int score = 0;
    int i;
    for(i = 0; i < 128; i++) {
        if(i & 0x88) continue;  /* skip off-board squares */
        int p = b[i];
        if(p) {
            int piece_type = p & 7;
            int val = w[piece_type];
            if(p & WHITE)
                score += val;
            else if(p & BLACK)
                score -= val;
        }
    }
    return score;
}

/*---------------------------------------------------------*/
/* SMTS: Softmax Tree Search with Principal Variation      */
/*---------------------------------------------------------*/
double SMTS(int side, int depth, int pv[], int *pv_len) {
    double beta = 1.0; /* temperature parameter */
    if(depth == 0) {
        *pv_len = 0;
        return static_eval();
    }
    
    int moves[256];
    double child_vals[256];
    int pv_storage[256][64];
    int pv_lengths[256];
    int num_moves = 0;
    
    int x;
    for(x = 0; x < 128; x++) {
        if(x & 0x88) continue;
        int piece = b[x];
        if(piece & side) {  /* piece belongs to current side */
            int d;
            for(d = 0; d < 8; d++) {
                int offset;
                switch(d) {
                    case 0: offset = -16; break;
                    case 1: offset = -15; break;
                    case 2: offset = 1; break;
                    case 3: offset = 17; break;
                    case 4: offset = 16; break;
                    case 5: offset = 15; break;
                    case 6: offset = -1; break;
                    case 7: offset = -17; break;
                    default: offset = 0; break;
                }
                int y = x + offset;
                if(y < 0 || y >= 128 || (y & 0x88)) continue;  /* off board */
                int target = b[y];
                if(target & side) continue;  /* cannot capture own piece */
                /* Pawn move logic fixes */
                if((piece & 7) == 1) {
                    if(side == WHITE) {
                        if(offset == -16) {
                            if(target != 0) continue;  // forward move: square must be empty
                        } else if(offset == -15 || offset == -17) {
                            if(target == 0 || (target & WHITE)) continue;  // capture: must capture enemy
                        } else {
                            continue; // disallow any other offsets for pawns in single moves
                        }
                    } else { /* side == BLACK */
                        if(offset == 16) {
                            if(target != 0) continue;
                        } else if(offset == 15 || offset == 17) {
                            if(target == 0 || (target & BLACK)) continue;
                        } else {
                            continue;
                        }
                    }
                }
                /* Make the move */
                int saved_from = b[x], saved_to = b[y];
                b[y] = b[x];
                b[x] = 0;
                int local_pv[64];
                int local_len = 0;
                double child_val = -SMTS(24 - side, depth - 1, local_pv, &local_len);
                /* Undo the move */
                b[x] = saved_from;
                b[y] = saved_to;
                
                moves[num_moves] = (x << 8) | y;
                child_vals[num_moves] = child_val;
                memcpy(pv_storage[num_moves], local_pv, local_len * sizeof(int));
                pv_lengths[num_moves] = local_len;
                num_moves++;
            }
            /* Pawn double move (two-square forward) */
            if((piece & 7) == 1) {
                if(side == WHITE && ((x >> 4) == 6)) {  // white pawn starting rank
                    int y = x - 16;
                    if(y >= 0 && y < 128 && !(y & 0x88) && b[y] == 0) {
                        int y2 = x - 32;
                        if(y2 >= 0 && y2 < 128 && !(y2 & 0x88) && b[y2] == 0) {
                            moves[num_moves] = (x << 8) | y2;
                            int saved_from = b[x], saved_to = b[y2];
                            b[y2] = b[x];
                            b[x] = 0;
                            int local_pv[64];
                            int local_len = 0;
                            double child_val = -SMTS(24 - side, depth - 1, local_pv, &local_len);
                            b[x] = saved_from;
                            b[y2] = saved_to;
                            child_vals[num_moves] = child_val;
                            memcpy(pv_storage[num_moves], local_pv, local_len * sizeof(int));
                            pv_lengths[num_moves] = local_len;
                            num_moves++;
                        }
                    }
                } else if(side == BLACK && ((x >> 4) == 1)) {  // black pawn starting rank
                    int y = x + 16;
                    if(y >= 0 && y < 128 && !(y & 0x88) && b[y] == 0) {
                        int y2 = x + 32;
                        if(y2 >= 0 && y2 < 128 && !(y2 & 0x88) && b[y2] == 0) {
                            moves[num_moves] = (x << 8) | y2;
                            int saved_from = b[x], saved_to = b[y2];
                            b[y2] = b[x];
                            b[x] = 0;
                            int local_pv[64];
                            int local_len = 0;
                            double child_val = -SMTS(24 - side, depth - 1, local_pv, &local_len);
                            b[x] = saved_from;
                            b[y2] = saved_to;
                            child_vals[num_moves] = child_val;
                            memcpy(pv_storage[num_moves], local_pv, local_len * sizeof(int));
                            pv_lengths[num_moves] = local_len;
                            num_moves++;
                        }
                    }
                }
            }
        }
    }
    if(num_moves == 0) {
        *pv_len = 0;
        return static_eval();
    }
    
    /* Compute softmax weights for all moves */
    double weights[256];
    double max_val = child_vals[0];
    for (int i = 1; i < num_moves; i++) {
         if(child_vals[i] > max_val)
             max_val = child_vals[i];
    }
    double total_weight = 0.0;
    for (int i = 0; i < num_moves; i++) {
         weights[i] = exp(beta * (child_vals[i] - max_val));
         total_weight += weights[i];
    }
    
    /* Randomly sample one move according to the softmax probabilities */
    double r = ((double) rand() / (RAND_MAX)) * total_weight;
    int chosen_index = 0;
    double accum = 0.0;
    for (int i = 0; i < num_moves; i++) {
         accum += weights[i];
         if (accum >= r) {
              chosen_index = i;
              break;
         }
    }
    
    int chosen_move = moves[chosen_index];
    pv[0] = chosen_move;
    for(int i = 0; i < pv_lengths[chosen_index]; i++){
         pv[i+1] = pv_storage[chosen_index][i];
    }
    *pv_len = pv_lengths[chosen_index] + 1;
    
    /* Return a softmax-style evaluation value */
    double softmax_eval = (1.0 / beta) * log(total_weight) + max_val;
    return softmax_eval;
}

/*---------------------------------------------------------*/
/* MABS: Multi-Armed Bandit Search with Principal Variation*/
/*---------------------------------------------------------*/
double MABS(int side, int depth, int pv[], int *pv_len) {
    if(depth == 0) {
        *pv_len = 0;
        return static_eval();
    }
    
    // Step 1: Collect all legal moves (each move is treated as an "arm")
    int moves[256];
    int moves_count = 0;
    int x;
    for(x = 0; x < 128; x++) {
        if(x & 0x88) continue;
        int piece = b[x];
        if(piece & side) {
            int d;
            for(d = 0; d < 8; d++) {
                int offset;
                switch(d) {
                    case 0: offset = -16; break;
                    case 1: offset = -15; break;
                    case 2: offset = 1; break;
                    case 3: offset = 17; break;
                    case 4: offset = 16; break;
                    case 5: offset = 15; break;
                    case 6: offset = -1; break;
                    case 7: offset = -17; break;
                    default: offset = 0; break;
                }
                int y = x + offset;
                if(y < 0 || y >= 128 || (y & 0x88)) continue;
                int target = b[y];
                if(target & side) continue;
                /* Pawn move logic fixes */
                if((piece & 7) == 1) {
                    if(side == WHITE) {
                        if(offset == -16) {
                            if(target != 0) continue;
                        } else if(offset == -15 || offset == -17) {
                            if(target == 0 || (target & WHITE)) continue;
                        } else {
                            continue;
                        }
                    } else { /* side == BLACK */
                        if(offset == 16) {
                            if(target != 0) continue;
                        } else if(offset == 15 || offset == 17) {
                            if(target == 0 || (target & BLACK)) continue;
                        } else {
                            continue;
                        }
                    }
                }
                moves[moves_count++] = (x << 8) | y;
            }
            /* Pawn double move for MABS */
            if((piece & 7) == 1) {
                if(side == WHITE && ((x >> 4) == 6)) {
                    int y = x - 16;
                    if(y >= 0 && y < 128 && !(y & 0x88) && b[y] == 0) {
                        int y2 = x - 32;
                        if(y2 >= 0 && y2 < 128 && !(y2 & 0x88) && b[y2] == 0) {
                            moves[moves_count++] = (x << 8) | y2;
                        }
                    }
                } else if(side == BLACK && ((x >> 4) == 1)) {
                    int y = x + 16;
                    if(y >= 0 && y < 128 && !(y & 0x88) && b[y] == 0) {
                        int y2 = x + 32;
                        if(y2 >= 0 && y2 < 128 && !(y2 & 0x88) && b[y2] == 0) {
                            moves[moves_count++] = (x << 8) | y2;
                        }
                    }
                }
            }
        }
    }
    if(moves_count == 0) {
        *pv_len = 0;
        return static_eval();
    }
    
    // Step 2: Initialize bandit statistics for each move (arm)
    int arm_plays[256] = {0};
    double arm_total_rewards[256] = {0.0};
    double arm_best_reward[256] = {0.0};
    int arm_best_pv[256][64] = {{0}};
    int arm_best_pv_len[256] = {0};

    // Step 3: Run a fixed number of iterations to simulate and update rewards
    int iterations = 100; 
    for (int iter = 1; iter <= iterations; iter++) {
        int selected_arm = -1;
        double best_ucb = -INFINITY;
        // UCB1 selection
        for (int i = 0; i < moves_count; i++) {
            double average_reward = (arm_plays[i] > 0) ? (arm_total_rewards[i] / arm_plays[i]) : 0.0;
            double ucb = (arm_plays[i] > 0) ? 
                         (average_reward + sqrt(2 * log(iter) / arm_plays[i])) : 
                         INFINITY;
            if (ucb > best_ucb) {
                best_ucb = ucb;
                selected_arm = i;
            }
        }
        
        // Simulate the selected move
        int move = moves[selected_arm];
        int from = move >> 8;
        int to = move & 0xFF;
        int saved_from = b[from], saved_to = b[to];
        b[to] = b[from];
        b[from] = 0;
        
        // Recursively evaluate (negamax style)
        int local_pv[64];
        int local_pv_len = 0;
        double reward = -MABS(24 - side, depth - 1, local_pv, &local_pv_len);
        
        // Undo
        b[from] = saved_from;
        b[to] = saved_to;
        
        // Update statistics
        arm_plays[selected_arm]++;
        arm_total_rewards[selected_arm] += reward;
        if (arm_plays[selected_arm] == 1 || reward > arm_best_reward[selected_arm]) {
            arm_best_reward[selected_arm] = reward;
            arm_best_pv_len[selected_arm] = local_pv_len;
            memcpy(arm_best_pv[selected_arm], local_pv, local_pv_len * sizeof(int));
        }
    }
    
    // Step 4: Choose the move with the highest average reward
    int best_arm = 0;
    double best_avg = -INFINITY;
    for (int i = 0; i < moves_count; i++) {
        double avg = (arm_plays[i] > 0) ? (arm_total_rewards[i] / arm_plays[i]) : -INFINITY;
        if (avg > best_avg) {
            best_avg = avg;
            best_arm = i;
        }
    }
    
    // Step 5: Return the best move and its principal variation
    int best_move = moves[best_arm];
    pv[0] = best_move;
    int best_local_pv_len = arm_best_pv_len[best_arm];
    memcpy(pv+1, arm_best_pv[best_arm], best_local_pv_len * sizeof(int));
    *pv_len = best_local_pv_len + 1;
    return best_avg;
}

/*---------------------------------------------------------*/
/* PrintResult: Check for draw/mate conditions             */
/*---------------------------------------------------------*/
int PrintResult(int s)
{
    int j, k, cnt = 0;
    for(j = 2; j <= 100; j += 2) {
        for(k = 0; k < STATE; k++)
            if(HistoryBoards[HistPtr][k] != HistoryBoards[(HistPtr - (j & 1023)) & 1023][k])
                goto differs;
        if(++cnt == 2) {
            printf("1/2-1/2 {Draw by repetition}\n");
            return 1;
        }
    differs: ;
    }
    /* One‐ply search as dummy mate/stalemate check */
    int cnt_eval = (int) SMTS(s, 1, NULL, &(int){0});
    if(cnt_eval == 0 && K == 0 && L == 0) {
        printf("1/2-1/2 {Stalemate}\n");
        return 2;
    }
    if(cnt_eval <= -I + 1) {
        if(s == WHITE)
            printf("0-1 {Black mates}\n");
        else
            printf("1-0 {White mates}\n");
        return 3;
    }
    if(Fifty >= 100) {
        printf("1/2-1/2 {Draw by fifty move rule}\n");
        return 4;
    }
    return 0;
}

/*---------------------------------------------------------*/
/* InitEngine: Initialize board evaluation tables          */
/*---------------------------------------------------------*/
int InitEngine()
{
    int j;
    int K_index;
    for(K_index = 8; K_index--; ) {
        int L_index;
        for(L_index = 8; L_index--; )
            b[16 * L_index + K_index + 8] = (K_index - 4) * (K_index - 4) + (L_index - 3.5) * (L_index - 3.5);
    }
    int N = 1035;
    while(N-- > M) T[N] = rand() >> 9;
    return 0;
}

/*---------------------------------------------------------*/
/* InitGame: Set up starting position                      */
/*---------------------------------------------------------*/
int InitGame()
{
    int i;
    for(i = 0; i < 128; i++) b[i & ~M] = 0;
    int K_index;
    for(K_index = 8; K_index--; ) {
        b[K_index] = (b[K_index + 112] = o[K_index + 24] + 8) + 8;
        b[K_index + 16] = 18;
        b[K_index + 96] = 9;
    }
    Side = WHITE;  /* White to move first */
    Fifty = 0;
    UnderProm = -1;
    
    update_bitboards();  // Update bitboards after initialization
    return 0;
}

/*---------------------------------------------------------*/
/* CopyBoard: Save current board into history             */
/*---------------------------------------------------------*/
void CopyBoard(int s)
{
    int j;
    for(j = 0; j < 64; j++)
        HistoryBoards[s][j] = b[j + (j & 0x38)];
}

/*---------------------------------------------------------*/
/* format_move: Convert board index to a coordinate string */
/*---------------------------------------------------------*/
void format_move(int sq, char *buf) {
    buf[0] = 'a' + (sq & 7);
    buf[1] = '8' - (sq >> 4);
    buf[2] = '\0';
}

/*---------------------------------------------------------*/
/* Global variable to track the "stop" command             */
/*---------------------------------------------------------*/
int stop_search = 0;

/*---------------------------------------------------------*/
/* main: Command loop for UCI protocol                     */
/*---------------------------------------------------------*/
int main(int argc, char **argv)
{
    char line[256];
    int from, to;

    /* Set stdout unbuffered so that output is immediately sent to the GUI */
    setbuf(stdout, NULL);

    /* Initialize engine and board state */
    InitEngine();
    InitGame();
    update_bitboards();  // Ensure bitboards are updated at start
    GamePtr = 0;
    HistPtr = 0;
    /* Default: engine plays Black; human plays White */
    engine_side = BLACK;
    
    while(fgets(line, sizeof(line), stdin)) {
        if(strncmp(line, "uci", 3) == 0) {
            printf("id name fuzzy-Max (micro-Max 4.8 + SMTS with PV)\n");
            /* UCI option for switching search algorithm */
            printf("option name MAB type check default false\n");
            printf("uciok\n");
        } else if(strncmp(line, "isready", 7) == 0) {
            printf("readyok\n");
        } else if(strncmp(line, "setoption", 9) == 0) {
            if(strstr(line, "name MAB") != NULL) {
                if(strstr(line, "value true") != NULL)
                    use_bandit_search = 1;
                else
                    use_bandit_search = 0;
            }
        } else if(strncmp(line, "ucinewgame", 10) == 0) {
            InitGame();
            update_bitboards();  // Update bitboards for new game
            GamePtr = 0;
            HistPtr = 0;
        } else if(strncmp(line, "position", 8) == 0) {
            /* Only support "startpos" with optional moves */
            if(strstr(line, "startpos") != NULL) {
                InitGame();
                update_bitboards();  // Update bitboards after position reset
                GamePtr = 0;
                HistPtr = 0;
                char *moves_ptr = strstr(line, "moves");
                if(moves_ptr != NULL) {
                    moves_ptr += 6; // skip "moves "
                    char move[10];
                    while(sscanf(moves_ptr, "%s", move) == 1) {
                        if(strlen(move) < 4)
                            break;
                        from = (move[0]-'a') + (8 - (move[1]-'0')) * 16;
                        to   = (move[2]-'a') + (8 - (move[3]-'0')) * 16;
                        /* Apply move */
                        b[to] = b[from];
                        b[from] = 0;
                        Side ^= 24;  /* toggle side */
                        GameHistory[GamePtr++] = (from << 8) | to;
                        CopyBoard(HistPtr = (HistPtr + 1) & 1023);
                        update_bitboards();  // Update bitboards after each move
                        moves_ptr = strchr(moves_ptr, ' ');
                        if(moves_ptr == NULL)
                            break;
                        moves_ptr++;
                    }
                }
            } else if(strstr(line, "fen") != NULL) {
                /* FEN parsing not supported in this version */
                printf("info string FEN not supported, only startpos moves allowed\n");
            }
        } else if(strncmp(line, "go", 2) == 0) {
            /* Parse movetime if provided */
            Ticks = GetTickCount();
            TimeLeft = 0; // reset time limit
            stop_search = 0; // reset stop flag at start of a new search
            char *movetime_ptr = strstr(line, "movetime");
            if(movetime_ptr != NULL) {
                movetime_ptr += 9; // skip "movetime "
                TimeLeft = atoi(movetime_ptr);
            }
            
            int best_move_global = -1;
            int best_move_k = 0, best_move_l = 0;
            int best_pv[64];
            int best_pv_length = 0;
            
            /* If no time limit is given, use the fixed MaxDepth */
            if(TimeLeft == 0) {
                for (int depth = 1; depth <= MaxDepth; depth++) {
                    if(stop_search) break;  // stop if "stop" is received
                    int pv[64];
                    int pv_length = 0;
                    double eval;
                    if(use_bandit_search)
                        eval = MABS(Side, depth, pv, &pv_length);
                    else
                        eval = SMTS(Side, depth, pv, &pv_length);
                    if (pv_length > 0) {
                        best_move_global = pv[0];
                        best_move_k = pv[0] >> 8;
                        best_move_l = pv[0] & 0xFF;
                        memcpy(best_pv, pv, pv_length * sizeof(int));
                        best_pv_length = pv_length;
                    }
                    char pv_str[256] = "";
                    char move_str[10];
                    char fromStr[3], toStr[3];
                    for (int i = 0; i < pv_length; i++) {
                        int move = pv[i];
                        format_move(move >> 8, fromStr);
                        format_move(move & 0xFF, toStr);
                        sprintf(move_str, "%s%s", fromStr, toStr);
                        strcat(pv_str, move_str);
                        if(i < pv_length - 1)
                            strcat(pv_str, " ");
                    }
                    printf("info depth %d pv %s eval %f\n", depth, pv_str, eval);
                }
            } else {
                /* Iterative deepening using time control */
                int depth = 0;
                while (1) {
                    depth++;
                    if(stop_search) break;  // stop if "stop" is received
                    int pv[64];
                    int pv_length = 0;
                    double eval;
                    if(use_bandit_search)
                        eval = MABS(Side, depth, pv, &pv_length);
                    else
                        eval = SMTS(Side, depth, pv, &pv_length);
                    if (pv_length > 0) {
                        best_move_global = pv[0];
                        best_move_k = pv[0] >> 8;
                        best_move_l = pv[0] & 0xFF;
                        memcpy(best_pv, pv, pv_length * sizeof(int));
                        best_pv_length = pv_length;
                    }
                    char pv_str[256] = "";
                    char move_str[10];
                    char fromStr[3], toStr[3];
                    for (int i = 0; i < pv_length; i++) {
                        int move = pv[i];
                        format_move(move >> 8, fromStr);
                        format_move(move & 0xFF, toStr);
                        sprintf(move_str, "%s%s", fromStr, toStr);
                        strcat(pv_str, move_str);
                        if(i < pv_length - 1)
                            strcat(pv_str, " ");
                    }
                    printf("info depth %d pv %s eval %f\n", depth, pv_str, eval);
                    if(GetTickCount() - Ticks > TimeLeft) break;
                }
            }
            if (best_pv_length > 0) {
                K = best_move_k;
                L = best_move_l;
                char fromStr[3], toStr[3];
                format_move(K, fromStr);
                format_move(L, toStr);
                printf("bestmove %s%s\n", fromStr, toStr);
                /* Apply move to update board state */
                b[L] = b[K];
                b[K] = 0;
                Side ^= 24;
                GameHistory[GamePtr++] = (K << 8) | L;
                CopyBoard(HistPtr = (HistPtr + 1) & 1023);
                update_bitboards();  // Update bitboards after best move applied
            } else {
                printf("bestmove (none)\n");
            }
        } else if(strncmp(line, "stop", 4) == 0) {
            /* Signal that we should stop searching */
            stop_search = 1;
        } else if(strncmp(line, "quit", 4) == 0) {
            break;
        }
    }
    return 0;
}
