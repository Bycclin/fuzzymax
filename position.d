module position;

extern (C++) struct Move {
    int from;
    int to;
    int promotion;
}

import std.string;
import std.conv;
import std.array;
import std.random;
import std.exception;
import std.algorithm;
import core.stdc.stdlib;

//-----------------------------------------------------
// Position class with a C++‑compatible interface
//-----------------------------------------------------
extern (C++) export class Position
{
public:
    ulong[12] pieces;
    ulong wOcc;
    ulong bOcc;
    ulong allOcc;
    int side; // 0 for white, 1 for black

    // Directional arrays.
    static __gshared immutable int[2][4] bishopDir = [
        [1,  1],
        [1, -1],
        [-1, 1],
        [-1, -1]
    ];
    static __gshared immutable int[2][4] rookDir = [
        [1,  0],
        [-1, 0],
        [0,  1],
        [0, -1]
    ];
    static __gshared immutable int[2][8] queenDir = [
        [1,  1],
        [1, -1],
        [-1, 1],
        [-1, -1],
        [1,  0],
        [-1, 0],
        [0,  1],
        [0, -1]
    ];

    static __gshared ulong[12][64] ZobristTable;
    static __gshared ulong ZobristBlack;
    static __gshared bool zobristInitialized = false;

    // Constructor: initialize standard starting position.
    this()
    {
        pieces = [
            0x000000000000FF00UL, // White pawns
            0x0000000000000042UL, // White knights
            0x0000000000000024UL, // White bishops
            0x0000000000000081UL, // White rooks
            0x0000000000000008UL, // White queen
            0x0000000000000010UL, // White king
            0x00FF000000000000UL, // Black pawns
            0x4200000000000000UL, // Black knights
            0x2400000000000000UL, // Black bishops
            0x8100000000000000UL, // Black rooks
            0x0800000000000000UL, // Black queen
            0x1000000000000000UL  // Black king
        ];
        wOcc = pieces[0] | pieces[1] | pieces[2] | pieces[3] | pieces[4] | pieces[5];
        bOcc = pieces[6] | pieces[7] | pieces[8] | pieces[9] | pieces[10] | pieces[11];
        allOcc = wOcc | bOcc;
        side = 0; // White to move by default.
    }

    // Create and return a new starting position.
    static Position* create_start_position()
    {
        return cast(Position*) new Position();
    }

    // Return a rotated (cloned) position.
    Position* rotate() const
    {
        return clone();
    }

    // Helper: perform a deep copy and return a new Position.
    Position* clone() const
    {
        auto p = new Position();
        p.pieces = this.pieces.dup;
        p.wOcc = this.wOcc;
        p.bOcc = this.bOcc;
        p.allOcc = this.allOcc;
        p.side = this.side;
        return cast(Position*) p;
    }

    // Flip a bitboard.
    static ulong flip(ulong bb)
    {
        ulong ret = 0;
        for (int i = 0; i < 64; i++) {
            if (bb & (1UL << i))
                ret |= 1UL << (63 - i);
        }
        return ret;
    }

    // Initialize the Zobrist table.
    static void initZobrist()
    {
        if (zobristInitialized)
            return;
        auto rng = Random(42);
        for (int p = 0; p < 12; p++) {
            for (int sq = 0; sq < 64; sq++) {
                ZobristTable[p][sq] = uniform(ulong.min, ulong.max, rng);
            }
        }
        ZobristBlack = uniform(ulong.min, ulong.max, rng);
        zobristInitialized = true;
    }

    // Compute and return the Zobrist hash.
    ulong getZobristHash() const
    {
        initZobrist();
        ulong h = 0;
        for (int pieceType = 0; pieceType < pieces.length; pieceType++) {
            ulong bb = pieces[pieceType];
            ulong temp = bb;
            while (temp != 0)
            {
                int sq = __ctz(temp);
                temp &= temp - 1;
                h ^= ZobristTable[pieceType][sq];
            }
        }
        if (side == 1)
            h ^= ZobristBlack;
        return h;
    }

    // Return board representation as a C string.
    extern (C++) const(char)* to_string() const
    {
        char[512] buffer;
        for (int i = 0; i < 512; i++) buffer[i] = 0;
        char[64] board;
        for (int i = 0; i < 64; i++)
            board[i] = '.';
        immutable char* symbols = "PNBRQKpnbrqk";
        for (int i = 0; i < pieces.length; i++) {
            ulong bb = pieces[i];
            ulong temp = bb;
            while (temp != 0)
            {
                int sq = __ctz(temp);
                temp &= temp - 1;
                board[sq] = symbols[i];
            }
        }
        int pos = 0;
        for (int rank = 7; rank >= 0; rank--) {
            for (int file = 0; file < 8; file++) {
                buffer[pos++] = board[rank * 8 + file];
                buffer[pos++] = ' ';
            }
            buffer[pos++] = '\n';
        }
        char* result = cast(char*)malloc(pos + 1);
        for (int i = 0; i < pos; i++) {
            result[i] = buffer[i];
        }
        result[pos] = 0;
        return result;
    }

    // Return current turn as a C string ("white" or "black").
    extern (C++) const(char)* current_turn() const
    {
        const(char)* turn = side == 0 ? "white" : "black";
        int len = 5;
        char* result = cast(char*)malloc(len + 1);
        for (int i = 0; i < len; i++) {
            result[i] = turn[i];
        }
        result[len] = 0;
        return result;
    }

    // Stub: Generate moves.
    extern (C++) void genMoves(Move** outMoves, size_t* outCount) const
    {
        *outMoves = null;
        *outCount = 0;
    }

    // Stub: Generate sliding moves.
    extern (C++) void generateSlidingMoves(Move** outMoves, size_t* outCount, int pieceIdx, const(int[2])* dir, size_t dirCount) const
    {
        *outMoves = null;
        *outCount = 0;
    }

    // Stub: Generate knight moves.
    extern (C++) void generateKnightMoves(Move** outMoves, size_t* outCount) const
    {
        *outMoves = null;
        *outCount = 0;
    }

    // Stub: Generate king moves.
    extern (C++) void generateKingMoves(Move** outMoves, size_t* outCount) const
    {
        *outMoves = null;
        *outCount = 0;
    }

    // Stub: Generate pawn moves.
    extern (C++) void generatePawnMoves(Move** outMoves, size_t* outCount) const
    {
        *outMoves = null;
        *outCount = 0;
    }

    // Make a move and return a new position.
    extern (C++) Position* makeMove(const(Move) m) const
    {
        auto newPos = clone();
        int offset = (side == 0 ? 0 : 6);
        int enemyOffset = (side == 0 ? 6 : 0);
        int movingPiece = -1;
        for (int i = 0; i < 6; i++) {
            if (newPos.pieces[offset + i] & (1UL << m.from))
            {
                movingPiece = offset + i;
                break;
            }
        }
        if (movingPiece == -1)
            return newPos;
        newPos.pieces[movingPiece] &= ~(1UL << m.from);
        for (int i = 0; i < 6; i++) {
            if (newPos.pieces[enemyOffset + i] & (1UL << m.to))
            {
                newPos.pieces[enemyOffset + i] &= ~(1UL << m.to);
                break;
            }
        }
        newPos.pieces[movingPiece] |= (1UL << m.to);
        newPos.wOcc = newPos.pieces[0] | newPos.pieces[1] | newPos.pieces[2] |
                      newPos.pieces[3] | newPos.pieces[4] | newPos.pieces[5];
        newPos.bOcc = newPos.pieces[6] | newPos.pieces[7] | newPos.pieces[8] |
                      newPos.pieces[9] | newPos.pieces[10] | newPos.pieces[11];
        newPos.allOcc = newPos.wOcc | newPos.bOcc;
        newPos.side = 1 - side;
        return newPos;
    }

    // Simplified check detection (only pawn and knight checks).
    extern (C++) bool is_in_check() const
    {
        int kingIndex = (side == 0 ? 5 : 11);
        if (pieces[kingIndex] == 0)
            return false;
        int kingSquare = __ctz(pieces[kingIndex]);
        int kingRank = kingSquare / 8;
        int kingFile = kingSquare % 8;

        if (side == 0)
        {
            if (kingRank + 1 < 8)
            {
                if (kingFile + 1 < 8) {
                    int sq = (kingRank + 1) * 8 + (kingFile + 1);
                    if (pieces[6] & (1UL << sq))
                        return true;
                }
                if (kingFile - 1 >= 0) {
                    int sq = (kingRank + 1) * 8 + (kingFile - 1);
                    if (pieces[6] & (1UL << sq))
                        return true;
                }
            }
        }
        else
        {
            if (kingRank - 1 >= 0)
            {
                if (kingFile + 1 < 8) {
                    int sq = (kingRank - 1) * 8 + (kingFile + 1);
                    if (pieces[0] & (1UL << sq))
                        return true;
                }
                if (kingFile - 1 >= 0) {
                    int sq = (kingRank - 1) * 8 + (kingFile - 1);
                    if (pieces[0] & (1UL << sq))
                        return true;
                }
            }
        }
        int[2][] knightMoves = [[2,1],[1,2],[-1,2],[-2,1],[-2,-1],[-1,-2],[1,-2],[2,-1]];
        for (size_t i = 0; i < knightMoves.length; i++) {
            int r = kingRank + knightMoves[i][0];
            int f = kingFile + knightMoves[i][1];
            if (r >= 0 && r < 8 && f >= 0 && f < 8)
            {
                int sq = r * 8 + f;
                if (side == 0) {
                    if (pieces[7] & (1UL << sq))
                        return true;
                }
                else {
                    if (pieces[1] & (1UL << sq))
                        return true;
                }
            }
        }
        return false;
    }

    // Stub implementations for checkmate, stalemate, etc.
    extern (C++) bool is_checkmate() const { return is_in_check() && false; }
    extern (C++) bool is_stalemate() const { return !is_in_check() && false; }
    extern (C++) bool is_threefold_repetition() const { return false; }
    extern (C++) bool isInsufficientMaterial() const { return false; }

    // Parse a FEN string and return a new position.
    extern (C++) static Position* fromFEN(const char* fen)
    {
        auto pos = new Position();
        // FEN parsing is not implemented in this stub.
        return cast(Position*) pos;
    }
};

//-----------------------------------------------------
// Helper: Count trailing zeros in x.
int __ctz(ulong x)
{
    if(x == 0)
        return 64;
    int count = 0;
    while((x & 1UL) == 0)
    {
        count++;
        x >>= 1;
    }
    return count;
}
