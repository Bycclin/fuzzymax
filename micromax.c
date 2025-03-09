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
int TimeLeft;
int MovesLeft;
int MaxDepth;
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

/* Global flag to indicate XBoard/CECP mode */
int xboard_mode = 0;

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
/* Softmax Tree Search Implementation                      */
/*---------------------------------------------------------*/
/* Recursively generates moves up to a given depth. At the   */
/* root, the best move is stored in globals K and L.         */
double softmax_tree_search(int side, int depth) {
    double beta = 1.0; /* temperature parameter */
    if(depth == 0) return static_eval();
    double sum = 0.0;
    double best_val = -1e9;
    int best_from = -1, best_to = -1;
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
                /* For pawns (assumed type 1) restrict forward moves */
                if((piece & 7) == 1) {
                    if(side == WHITE && offset != -16) continue;
                    if(side == BLACK && offset != 16) continue;
                }
                /* Make the move */
                int saved_from = b[x], saved_to = b[y];
                b[y] = b[x];
                b[x] = 0;
                double child_val = -softmax_tree_search(24 - side, depth - 1);
                /* Undo the move */
                b[x] = saved_from;
                b[y] = saved_to;
                
                double weight = exp(beta * child_val);
                sum += weight;
                if(child_val > best_val) {
                    best_val = child_val;
                    best_from = x;
                    best_to = y;
                    move_found = 1;
                }
            }
        }
    }
    if(!move_found) {
        /* No legal moves: return static evaluation (checkmate/stalemate) */
        return static_eval();
    }
    /* At the root, record the best move */
    if(depth == root_depth) {
        K = best_from;
        L = best_to;
    }
    return (1.0 / beta) * log(sum);
}

/*---------------------------------------------------------*/
/* PrintResult: Check for draw/mate conditions             */
/*---------------------------------------------------------*/
int PrintResult(int s)
{
    int i, j, k, cnt = 0;
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
    int cnt_eval = (int) softmax_tree_search(s, 1);
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
    Side = WHITE;
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
/* main: Command loop for XBoard/CECP protocol              */
/*---------------------------------------------------------*/
int main(int argc, char **argv)
{
    int Computer, MaxTime, MaxMoves, TimeInc, sec, i;
    char line[256], command[256];
    int m, nr;
    
    if(argc > 1 && sscanf(argv[1], "%d", &m) == 1)
        ; // parameter U is unused in this softmax version
    signal(SIGINT, SIG_IGN);

    /* Set stdout unbuffered to ensure immediate output to XBoard */
    setbuf(stdout, NULL);

    printf("tellics say     micro-Max 4.8 (Softmax Tree Search version)\n");
    printf("tellics say     by H.G. Muller (modified)\n");

    InitEngine();
    InitGame();
    /* Default: human is White so engine plays Black */
    engine_side = BLACK;
    Computer = engine_side;
    MaxTime  = 10000;  /* 10 sec */
    MaxDepth = 4;      /* reduced depth for softmax search */
    
    for (;;) {
        fflush(stdout);
        
        /* If it's the engine’s turn, only move if Side matches engine_side */
        if(Side == Computer) {
            Ticks = GetTickCount();
            m = MovesLeft <= 0 ? 40 : MovesLeft;
            tlim = 0.6 * (TimeLeft + ((m - 1) * 0)) / (m + 7); /* simplified time control */
            PromPiece = 'q';
            /* Set root depth for softmax search */
            root_depth = MaxDepth;
            /* Compute move */
            softmax_tree_search(Side, MaxDepth);
            if(K >= 0 && L >= 0) {
                /* Toggle side after move */
                Side ^= 24;
                if(UnderProm >= 0 && UnderProm != L) {    
                    printf("tellics I hate under-promotions!\n");
                    printf("resign\n");
                    Computer = EMPTY;
                    continue;
                } else {
                    UnderProm = -1;
                }
                {
                    char from[3], to[3];
                    format_move(K, from);
                    format_move(L, to);
                    /* Output coordinate move (only for engine_side) */
                    if(xboard_mode)
                        printf("move %s%s\n", from, to);
                    else
                        printf("move %s%s\n", from, to);
                }
                Ticks = GetTickCount() - Ticks;
                TimeLeft -= Ticks;
                if(--MovesLeft == 0) {
                    MovesLeft = MaxMoves;
                    TimeLeft  = MaxTime;
                }
                GameHistory[GamePtr++] = (K << 8) | L;
                CopyBoard(HistPtr = (HistPtr + 1) & 1023);
                if(PrintResult(Side))
                    Computer = EMPTY;
            } else {
                if(!PrintResult(Side))
                    printf("resign\n");
                Computer = EMPTY;
            }
            continue;
        }
        if(!fgets(line, 256, stdin))
            return 0;
        if(line[0] == '\n')
            continue;
        sscanf(line, "%s", command);

        /* XBoard protocol commands */
        if(!strcmp(command, "xboard")) {
            xboard_mode = 1;
            continue;
        }
        if(!strcmp(command, "new")) {
            InitGame();
            GameHistory[GamePtr] = 0;
            HistPtr = 0;
            /* New game: human is White, so engine plays Black */
            engine_side = BLACK;
            Computer = engine_side;
            TimeLeft  = MaxTime;
            MovesLeft = MaxMoves;
            for(nr = 0; nr < 1024; nr++)
                memset(HistoryBoards[nr], 0, STATE);
            continue;
        }
        if(!strcmp(command, "quit"))
            return 0;
        if(!strcmp(command, "force")) {
            Computer = EMPTY;
            continue;
        }
        /* "go" command: resume engine play using assigned engine_side */
        if(!strcmp(command, "go")) {
            Computer = engine_side;
            continue;
        }
        if(!strcmp(command, "white")) {
            Side = WHITE;
            /* When human is White, engine plays Black */
            engine_side = BLACK;
            Computer = engine_side;
            continue;
        }
        if(!strcmp(command, "black")) {
            Side = BLACK;
            /* When human is Black, engine plays White */
            engine_side = WHITE;
            Computer = engine_side;
            continue;
        }
        if(!strcmp(command, "st")) {
            Ticks = GetTickCount();
            tlim = 1000;
            softmax_tree_search(Side, MaxDepth);
            if(K == 0 && L == 0)
                continue;
            {
                char from[3], to[3];
                format_move(K, from);
                format_move(L, to);
                printf("Hint: %s%s\n", from, to);
            }
            continue;
        }
        if(!strcmp(command, "undo") || !strcmp(command, "remove")) {
            if(GamePtr < 1)
                continue;
            GameHistory[--GamePtr];
            HistPtr = (HistPtr - 1) & 1023;
            memset(HistoryBoards[(HistPtr + 1) & 1023], 0, STATE);
            InitGame();
            for(nr = 0; nr < GamePtr; nr++) {
                int from = GameHistory[nr] >> 8;
                int to = GameHistory[nr] & 0xFF;
                b[to] = b[from];
                b[from] = 0;
                Side ^= 24;
            }
            continue;
        }
        if(!strcmp(command, "post")) {
            Post = 1;
            continue;
        }
        if(!strcmp(command, "nopost")) {
            Post = 0;
            continue;
        }
        if(!strcmp(command, "edit")) {
            int color = WHITE;
            while(fgets(line, 256, stdin)) {
                m = line[0];
                if(m == '.')
                    break;
                if(m == '#') {
                    for(i = 0; i < 128; i++) b[i & 0x77] = 0;
                    Fifty = 40;
                    continue;
                }
                if(m == 'c') {
                    color = WHITE + BLACK - color;
                    continue;
                }
                if(((m=='P') || (m=='N') || (m=='B') ||
                    (m=='R') || (m=='Q') || (m=='K')) &&
                   (line[1] >= 'a') && (line[1] <= 'h') &&
                   (line[2] >= '1') && (line[2] <= '8')) {
                    int pos = (line[1] - 'a') + (8 - (line[2] - '0')) * 16;
                    switch(line[0]) {
                        case 'P': b[pos] = (color == WHITE) ? 9 : 18; break;
                        case 'N': b[pos] = 3 + color; break;
                        case 'B': b[pos] = 5 + color; break;
                        case 'R': b[pos] = 6 + color; break;
                        case 'Q': b[pos] = 7 + color; break;
                        case 'K': b[pos] = 4 + color; break;
                    }
                    continue;
                }
            }
            if(Side != color)
                ; /* adjust evaluation if needed */
            continue;
        }
        /* Otherwise, assume an input move in coordinate form */
        if(line[0] < 'a' || line[0] > 'h' ||
           line[1] < '1' || line[1] > '8' ||
           line[2] < 'a' || line[2] > 'h' ||
           line[3] < '1' || line[3] > '8')
        {
            printf("Error (unknown command): %s\n", command);
        } else {
            int from = (line[0] - 'a') + (8 - (line[1] - '0')) * 16;
            int to   = (line[2] - 'a') + (8 - (line[3] - '0')) * 16;
            PromPiece = line[4];
            if(softmax_tree_search(Side, 1) != I) { /* dummy legality check */
                printf("Illegal move: %s\n", line);
            } else {
                GameHistory[GamePtr++] = (from << 8) | to;
                Side ^= 24;
                CopyBoard(HistPtr = (HistPtr + 1) & 1023);
                if(PrintResult(Side))
                    Computer = EMPTY;
            }
        }
    }
    return 0;
}
