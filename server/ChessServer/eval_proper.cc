#include "eval.hh"

constexpr bits file_A = 0x0101010101010101ULL;
constexpr bits rank_1 = 0xFF00000000000000ULL;

extern bits zobrist_table[64][16];
extern bits capture_masks[64][6];
extern sliding_mask bishop_masks[64][2][2];
extern sliding_mask rook_masks[64][2][2];
extern const sliding_mask *masks_fw[64][4];
extern const sliding_mask *masks_rev[64][4];

inline int pieces_on_file(const chessboard& board, int x, int type)
{
    return file_A << x & board.piece_sets[type];
}

inline int pieces_on_rank(const chessboard &board, int x, int type)
{
    return rank_1 >> x & board.piece_sets[type];
}

template<int side, int type> int eval_pieces(const chessboard& board)
{
    int score = 0;

    bits all_pieces = board.side_sets[0] | board.side_sets[1];
    bits not_friendly = board.side_sets[side ^ 1];
    bits b = board.piece_sets[type] & board.side_sets[side];

    unsigned long ind;

    while (b) {
        _BitScanForward64(&ind, b);

        int x = ind % 8, y = ind / 8;

        if constexpr (type == pawn) {
            score += 80;

            if ((x == 0 || !(file_A << (x - 1) & board.side_sets[side])) &&
                (x == 7 || !(file_A << (x + 1) & board.side_sets[side])))
                score -= 20;

            if (board.side_sets[side]
                & (side == 0 ? ind << 8 : ind >> 8))
                score -= 20;

            if constexpr (side) y ^= 7;
            score += 4 * (y - 1) * (y - 1);
            score -= 4 * (x - 4) * (x - 4);
        }
        else if constexpr (type == queen) {
            score += 1000;
        }
        else if constexpr (type == bishop) {
            score += 360;

            const sliding_mask **fw = masks_fw[ind];
            const sliding_mask **rev = masks_rev[ind];

            for (int i = 0; i < 2; i++) {
                unsigned long b;

                bits maskfw = fw[i]->last, maskrev = rev[i]->last;

                if (_BitScanForward64(&b, fw[i]->last & all_pieces))
                    maskfw = fw[i]->steps[b];

                if (_BitScanReverse64(&b, rev[i]->last & all_pieces))
                    maskrev = rev[i]->steps[b];

                score += __popcnt64((maskfw | maskrev) & not_friendly);
            }
        }
        else if constexpr (type == knight) {
            score += 320;
            score += -80 + 10 * __popcnt64(capture_masks[ind][knight] & ~board.side_sets[side]);
        }
        else if constexpr (type == rook) {
            score += 470;
            bits pawns = pieces_on_file(board, x, pawn);
            bool our_pawns   = pawns & board.side_sets[side ^ 1];
            bool their_pawns = pawns & board.side_sets[side];
            
            if (their_pawns)
                score += our_pawns ? 10 : 30;
        }
        else if constexpr (type == king) {
            static const int table[] = {
                -65, 23, 16, -15, -56, -34, 2, 13,
                29, -1, -20, -7, -8, -4, -38, -29,
                -9, 24, 2, -16, -20, 6, 22, -22,
                -17, -20, -12, -27, -30, -25, -14, -36,
                -49, -1, -27, -39, -46, -44, -33, -51,
                -14, -14, -22, -46, -44, -30, -15, -27,
                1, 7, -8, -64, -43, -16, 9, 8,
                -15, 36, 12, -54, 8, -28, 24, 14
            };
            score += table[ind ^ ((1 - side) * 0b111000)];
        }

        b &= b - 1;
    }

    return score;
}

extern std::atomic<int> g_total_nodes;

int evaluation::proper(const chessboard &board, int side)
{
    g_total_nodes++;
    return (side * 2 - 1) *
        (eval_pieces<1, pawn>(board) - eval_pieces<0, pawn>(board) +
        eval_pieces<1, king>(board) - eval_pieces<0, king>(board) +
        eval_pieces<1, rook>(board) - eval_pieces<0, rook>(board) +
        eval_pieces<1, knight>(board) - eval_pieces<0, knight>(board) +
        eval_pieces<1, bishop>(board) - eval_pieces<0, bishop>(board) +
        eval_pieces<1, queen>(board) - eval_pieces<0, queen>(board));
}