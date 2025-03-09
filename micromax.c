#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <mach/mach_time.h> // macOS time

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
/* Static evaluation function: simple sum over board      */
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
/* Softmax Tree Search with Principal Variation            */
/*---------------------------------------------------------*/
double softmax_tree_search_pv(int side, int depth, int pv[], int *pv_len) {
    double beta = 1.0; /* temperature parameter */
    if(depth == 0) {
        *pv_len = 0;
        return static_eval();
    }
    double sum = 0.0;
    double best_val = -1e9;
    int best_move = -1;
    int best_local_pv[64];
    int best_local_len = 0;
    int move_found = 0;
    
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
                /* For pawns (assumed type 1) restrict forward moves and require empty target */
                if((piece & 7) == 1) {
                    if(side == WHITE && offset != -16) continue;
                    if(side == BLACK && offset != 16) continue;
                    if(target != 0) continue;  // pawn forward move must be to an empty square
                }
                /* Make the move */
                int saved_from = b[x], saved_to = b[y];
                b[y] = b[x];
                b[x] = 0;
                int local_pv[64];
                int local_len = 0;
                double child_val = -softmax_tree_search_pv(24 - side, depth - 1, local_pv, &local_len);
                /* Undo the move */
                b[x] = saved_from;
                b[y] = saved_to;
                
                double weight = exp(beta * child_val);
                sum += weight;
                if(child_val > best_val) {
                    best_val = child_val;
                    best_move = (x << 8) | y;
                    best_local_len = local_len;
                    memcpy(best_local_pv, local_pv, best_local_len * sizeof(int));
                    move_found = 1;
                }
            }
        }
    }
    if(!move_found) {
        *pv_len = 0;
        return static_eval();
    }
    pv[0] = best_move;
    for(int i = 0; i < best_local_len; i++){
         pv[i+1] = best_local_pv[i];
    }
    *pv_len = best_local_len + 1;
    return (1.0 / beta) * log(sum);
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
    int cnt_eval = (int) softmax_tree_search_pv(s, 1, NULL, &(int){0});
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
/* InitEngine: Initialize board evaluation tables         */
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
/* InitGame: Set up starting position                       */
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
/* format_move: Convert board index to a coordinate string  */
/* (a8 is index 0, a1 is index 112)                          */
/*---------------------------------------------------------*/
void format_move(int sq, char *buf) {
    buf[0] = 'a' + (sq & 7);
    buf[1] = '8' - (sq >> 4);
    buf[2] = '\0';
}

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
    GamePtr = 0;
    HistPtr = 0;
    /* Default: engine plays Black; human plays White */
    engine_side = BLACK;
    
    while(fgets(line, sizeof(line), stdin)) {
        if(strncmp(line, "uci", 3) == 0) {
            printf("id name fuzzy-Max (micro-Max 4.8 + Softmax Tree search with PV)\n");
            /* UCI options could be printed here if needed */
            printf("uciok\n");
        } else if(strncmp(line, "isready", 7) == 0) {
            printf("readyok\n");
        } else if(strncmp(line, "ucinewgame", 10) == 0) {
            InitGame();
            GamePtr = 0;
            HistPtr = 0;
        } else if(strncmp(line, "position", 8) == 0) {
            /* Only support "startpos" with optional moves */
            if(strstr(line, "startpos") != NULL) {
                InitGame();
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
            char *movetime_ptr = strstr(line, "movetime");
            if(movetime_ptr != NULL) {
                movetime_ptr += 9; // skip "movetime " (8 letters + space)
                TimeLeft = atoi(movetime_ptr);
            }
            
            int best_move_global = -1;
            int best_move_k = 0, best_move_l = 0;
            int best_pv[64];
            int best_pv_length = 0;
            
            /* If no time limit is given, use the fixed MaxDepth */
            if(TimeLeft == 0) {
                for (int depth = 1; depth <= MaxDepth; depth++) {
                    int pv[64];
                    int pv_length = 0;
                    double eval = softmax_tree_search_pv(Side, depth, pv, &pv_length);
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
                    int pv[64];
                    int pv_length = 0;
                    double eval = softmax_tree_search_pv(Side, depth, pv, &pv_length);
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
                    if(GetTickCount() - Ticks > TimeLeft)
                        break;
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
            } else {
                printf("bestmove (none)\n");
            }
        } else if(strncmp(line, "stop", 4) == 0) {
            /* Our search is instantaneous so nothing to stop */
        } else if(strncmp(line, "quit", 4) == 0) {
            break;
        }
    }
    return 0;
}
