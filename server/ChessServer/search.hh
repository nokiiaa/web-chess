#pragma once
#include "chess.hh"
#include "eval.hh"

struct rated_move {
    int value;
    chessmove move;
    size_t board_hash = 0;

    inline rated_move(int v, chessmove m) : value(v), move(m) {}
    inline rated_move() : value(-INT_MAX), move(chessmove()) {}
};

namespace engine
{
    bool iterative_deepening_negamax(chessboard &board, rated_move &result,
        int max_search_depth = 64, int max_search_time = 10, eval_func eval = evaluation::simplified,
        int min_depth = 4, int retries = 3);
}