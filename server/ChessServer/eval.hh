#pragma once
#include "chess.hh"

using eval_func = int(*)(const chessboard& board, int side);

namespace evaluation {
    int simplified(const chessboard &board, int side);
    int proper(const chessboard &board, int side);
    int pesto(const chessboard &board, int side);
    int game_phase_score(const chessboard &board);

    inline std::string to_string(int val)
    {
        return val >= INT_MAX - 256 ? std::string("#") + std::to_string((INT_MAX - val + 1) / 2) :
            val <= -INT_MAX + 256 ? std::string("#-") + std::to_string((val + INT_MAX + 1) / 2) :
            std::to_string(val / 100.);
    }
}