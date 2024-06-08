#include "search.hh"
#include "eval.hh"
#include <unordered_map>
#include <chrono>
#include "fastmap.hh"
#include <thread>
#include <mutex>
#include <algorithm>
#include <iostream>

using namespace std::chrono;

using timepoint = steady_clock::time_point;

class out_of_time_exception : std::exception {};

struct search_config {
    timepoint deadline;
    eval_func eval;
    int depth;
};

constexpr int transposition_lower = 1;
constexpr int transposition_upper = 2;
constexpr int transposition_exact = 3;
constexpr int max_transpositions_size = 1 << 27;

struct transposition_entry {
    size_t hash;
    int value;
    char depth, type;

    inline transposition_entry(size_t hsh, int v, int d, int t)
        : hash(hsh), value(v), depth(d), type(t) {}

    inline transposition_entry() {}
};

struct spinlock {
    std::atomic<bool> lock_ = { 0 };

    void lock() noexcept {
        for (;;) {
            if (!lock_.exchange(true, std::memory_order_acquire))
                return;
            while (lock_.load(std::memory_order_relaxed));
        }
    }

    bool try_lock() noexcept {
        return !lock_.load(std::memory_order_relaxed) &&
            !lock_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        lock_.store(false, std::memory_order_release);
    }
};

fastmap<transposition_entry, max_transpositions_size> transpositions;
spinlock transposition_table_lock, killer_lock;
std::atomic<int> g_total_nodes = 0;
std::atomic_bool halt_search = false;
std::atomic<int> nodes_examined = 0, tt_found = 0;
std::vector<std::vector<chessmove>> killer_moves;

int wait_for_keypress()
{
    std::cin.get();
    halt_search = true;
    return 0;
}

// https://stackoverflow.com/questions/11828539/elegant-exception-handling-in-openmp
class delayed_exception {
    std::exception_ptr ptr;
public:
    delayed_exception() : ptr(nullptr) {}

    void rethrow() {
        if (this->ptr)
            std::rethrow_exception(this->ptr);
    }

    void capture() {
        this->ptr = std::current_exception();
    }

    template <typename function, typename... parameters>
    void run(function f, parameters... params)
    {
        try { f(params...); }
        catch (...) { capture(); }
    }
};

int timed_negamax_search(bool parallel,
    chessboard &board, int depth, int alpha, int beta, rated_move *move,
    const search_config &config, bool quiescence = false);

const int processor_count = std::thread::hardware_concurrency();

int search_helper(rated_move& to_make, bool search_pv, int move_index,
    chessboard &board, int depth, int alpha, int beta, rated_move *move,
    const search_config &config, bool quiescence = false)
{
    bool capture = board.pieces[to_make.move.dest_x + to_make.move.dest_y * 8];

    board.make_move(
        to_make.move.org_x, to_make.move.org_y,
        to_make.move.dest_x, to_make.move.dest_y);
    board.appended_moves++;

    int m, r = 0;

    // Late move pruning
    if (!quiescence &&
        depth >= 3 && move_index >= 3 &&
        !board.in_check(board.side_to_move ^ 1) &&
        !board.in_check(board.side_to_move) &&
        !capture) {
        r = move_index >= 9 ? depth / 3 : 1;
        m = -timed_negamax_search(false, board, depth - r - 1, -alpha - 1, -alpha, nullptr, config, quiescence);
        
        if (m > alpha)
            m = -timed_negamax_search(false, board, depth - 1, -beta, -alpha, nullptr, config, quiescence);
    }
    else {
        if (search_pv)
            m = -timed_negamax_search(false, board, depth - 1, -beta, -alpha, nullptr, config, quiescence);
        else {
            m = -timed_negamax_search(false, board, depth - 1, -alpha - 1, -alpha, nullptr, config, quiescence);
            if (m > alpha)
                m = -timed_negamax_search(false, board, depth - 1, -beta, -alpha, nullptr, config, quiescence);
        }
    }

    board.unmake_move();
    board.appended_moves--;

    return m;
}

#include <omp.h>

int timed_negamax_search(bool parallel,
    chessboard &board, int depth, int alpha, int beta, rated_move *move,
    const search_config &config, bool quiescence) {
    rated_move best_move;

    int orig_alpha = alpha;

    int side = board.side_to_move;

    size_t z = board.hash;

    nodes_examined++;

    // Threefold repetition is a draw
    if (board.previous_states[z] + 1 >= 3)
        return 0;

    {
        std::lock_guard<spinlock> lock(transposition_table_lock);
        // Memoization
        if (!move && transpositions[z].type && transpositions[z].hash == z) {

            auto entry = transpositions[z];

            if (entry.depth >= depth) {
                tt_found++;
                switch (entry.type) {
                case transposition_exact: return
                    entry.value >= +INT_MAX - 256 ? entry.value - board.appended_moves :
                    entry.value <= -INT_MAX + 256 ? entry.value + board.appended_moves :
                    entry.value;
                case transposition_lower: alpha = std::max(alpha, entry.value); break;
                case transposition_upper: beta = std::min(beta, entry.value); break;
                }

                if (alpha >= beta)
                    return entry.value;
            }
        }
    }

    if (depth >= 2 && (high_resolution_clock::now() >= config.deadline || halt_search)) {
        for (int i = 0; i < board.appended_moves; i++)
            board.unmake_move();
        throw out_of_time_exception();
    }

    bool checked = board.in_check(side);

    // Checkmate for this side or a stalemate
    if (!board.any_moves(side))
        return checked ? -INT_MAX + board.appended_moves : 0;

    int board_val = config.eval(board, side);

    if (quiescence && !checked)
        if (board_val >= beta)
            return beta;
        else if (alpha < board_val)
            alpha = board_val;

    bits bm[64];

    bool quiet = 
        (depth > 0 && !quiescence) || !checked && !board.generate_moves(side, bm, false, true, board.side_sets[side ^ 1]);

    if (depth <= 0) {
        // Perform quiescence search
        if (!quiescence && !quiet)
            return timed_negamax_search(parallel, board, 12, alpha, beta, nullptr,
                search_config{ config.deadline, config.eval, config.depth + 12 },
                true);

        return board_val;
    }

    if (quiescence && quiet)
        return board_val;

    int phase = evaluation::game_phase_score(board);

    // Null move pruning
    if (!quiescence &&
        phase < 14 &&
        depth >= 2 &&
        !checked &&
        !move &&
        board.appended_moves > config.depth / 4) {
        board.make_move(0, 0, 0, 0);
        board.appended_moves++;
        bool fail_high = -timed_negamax_search(false, board, depth - 3, -beta, -beta + 1, move, config, quiescence) >= beta;
        board.unmake_move();
        board.appended_moves--;

        if (fail_high)
            return beta;
    }

    int ply = board.appended_moves + 1;

    // Move generation
    std::vector<rated_move> moves;
    moves.reserve(128);

    std::memset(bm, 0, sizeof bm);
    board.generate_moves(side, bm, false, false, quiescence && !checked ? board.side_sets[side ^ 1] : ~0ull);

    for (int i = 0; i < 64; i++) {
        if (bits mask = bm[i]) {
            unsigned long ind;

            static const int piece_values[7] = { 0, 0, 1025, 365, 337, 477, 82 };

            while (mask) {
                _BitScanForward64(&ind, mask);

                int captured = board.pieces[ind] & type_mask;
                int capturing = board.pieces[i] & type_mask;

                char x = i & 7, y = i >> 3;
                char dx = ind & 7, dy = ind >> 3;
                int pre_count = board.count_pieces();

                board.make_move(x, y, dx, dy);

                bool ordered = false;
                int order_val = 0;

                bool capture = pre_count != board.count_pieces();

                size_t hash = board.hash;
                {
                    std::lock_guard<spinlock> guard(transposition_table_lock);
                    auto t = transpositions[hash];

                    // If we've already seen this position before,
                    // use its estimated value for move ordering
                    if (t.type == transposition_exact) {
                        ordered = true;
                        order_val = INT_MAX - 256 + t.depth;
                    }
                }

                if (!ordered) {
                    if (capture) {
                        int diff = piece_values[captured] - piece_values[capturing];
                        order_val = diff + (diff >= 0 ? 100000 : 40000);
                    }
                    else {
                        if (ply >= 2 && !quiescence) {
                            std::lock_guard<spinlock> guard(killer_lock);
                            for (const chessmove &killer : killer_moves[ply])
                                if (killer.org_x == x && killer.org_y == y &&
                                    killer.dest_x == dx && killer.dest_y == dy)
                                    order_val = 50000, ordered = true;
                        }

                        if (!ordered)
                            order_val = config.eval(board, side);
                    }
                }

                moves.push_back(rated_move(order_val, chessmove{ x, y, 0, dx, dy }));

                board.unmake_move();

                mask &= mask - 1;
            }
        }
    }

    if (moves.size())
        best_move = rated_move(-INT_MAX, moves[0].move);

    // Insertion sort
    for (unsigned i = 1; i < moves.size(); i++)
    {
        unsigned j = i;

        while (j > 0 && moves[j - 1].value < moves[j].value)
        {
            std::swap(moves[j - 1], moves[j]);
            j--;
        }
    }

    bool search_pv = true;

    std::vector<chessboard> par_boards;
    std::vector<rated_move> par_moves;
    std::vector<int> par_outputs;
    std::vector<delayed_exception> exceptions;

    if (parallel) {
        par_boards = std::vector<chessboard>(processor_count);
        par_moves = std::vector<rated_move>(processor_count);
        par_outputs = std::vector<int>(processor_count);
        exceptions = std::vector<delayed_exception>(processor_count);
    }

    for (int i = 0; i < moves.size(); i++) {
        if (parallel) {
            int cores = std::min(processor_count, int(moves.size() - i));

#pragma omp parallel for shared(par_boards, par_moves, par_outputs, exceptions)
            for (int j = 0; j < cores; j++) {
                par_moves[j] = moves[i + j];
                chessboard &b = par_boards[j] = board;

                exceptions[j].run([&]() mutable {
                    par_outputs[j] = search_helper(
                        par_moves[j], search_pv, i + j,
                        b, depth, alpha, beta, nullptr, config, quiescence);
                    }
                );
            }

            bool cutoff = false;

            for (int j = 0; j < cores; j++) {
                exceptions[j].rethrow();

                int m = par_outputs[j];

                if (m > best_move.value) {
                    best_move.value = m;
                    best_move.move = par_moves[j].move;
                }

                if (best_move.value > alpha) {
                    alpha = best_move.value;
                    search_pv = false;
                }

                if (alpha >= beta) {
                    cutoff = true;

                    if (ply >= 2) {
                        std::lock_guard<spinlock> guard(killer_lock);

                        if (killer_moves[ply].size() >= 2)
                            killer_moves[ply].pop_back();

                        killer_moves[ply].push_back(best_move.move);
                    }

                    break;
                }
            }

            if (cutoff)
                break;

            i += cores - 1;
        }
        else {
            int m = search_helper(moves[i], search_pv, i,
                board, depth, alpha, beta, nullptr, config, quiescence);

            if (m > best_move.value) {
                best_move.value = m;
                best_move.move = moves[i].move;
            }

            if (best_move.value > alpha) {
                alpha = best_move.value;
                search_pv = false;
            }

            if (alpha >= beta) {
                std::lock_guard<spinlock> guard(killer_lock);

                if (ply >= 2) {
                    if (killer_moves[ply].size() >= 2)
                        killer_moves[ply].pop_back();

                    killer_moves[ply].push_back(best_move.move);
                }
                break;
            }
        }
    }

    if (!quiescence) {
        auto e = transposition_entry(z, best_move.value, depth, 0);

        if (best_move.value <= orig_alpha)
            e.type = transposition_upper;
        else if (best_move.value >= beta)
            e.type = transposition_lower;
        else
            e.type = transposition_exact;

        std::lock_guard<spinlock> guard(transposition_table_lock);
        transpositions[z] = e;
    }

    if (move)
        *move = best_move;

    return alpha;
}

namespace engine
{
    bool iterative_deepening_negamax(chessboard& board, rated_move &result,
        int max_search_depth, int max_search_time, eval_func eval, int min_depth, int retries)
    {
        while (killer_moves.size() < max_search_depth + 256)
            killer_moves.push_back(std::vector<chessmove>());

        halt_search = false;
        g_total_nodes = 0;

        std::thread waiter(wait_for_keypress);

        auto start = high_resolution_clock::now();

        // Iterative deepening
        search_config config { high_resolution_clock::now() + seconds(max_search_time), eval, 1 };

        int i = 1; // min_depth + retries;
        int total_nodes_examined = 0;

        try {
            for (; i <= max_search_depth && high_resolution_clock::now() < config.deadline; i++) {
                nodes_examined = 0;
                tt_found = 0;

                timed_negamax_search(true, board, i, -INT_MAX, INT_MAX, &result, config);

                auto ev = evaluation::to_string(result.value);

                std::printf("%i/%i plies, %i/%i/%i nodes, score = %s\n",
                    i, max_search_depth, g_total_nodes.load(), nodes_examined.load(), tt_found.load(), ev.c_str());

                if (result.value >= INT_MAX  - 256 ||
                    result.value <= -INT_MAX + 256)
                    break;

                total_nodes_examined += nodes_examined;
                config.depth++;
            }
        }
        catch (out_of_time_exception &e) {
        }        

        waiter.detach();

        auto took = duration_cast<seconds>(high_resolution_clock::now() - start).count();

        //if (retries && result.move.empty())
        //    return iterative_deepening_negamax(board, result, max_search_depth, max_search_time, eval, retries - 1);

        if (took)
            std::printf("\n%i nodes in %i seconds => %i n/s\n", total_nodes_examined, took, total_nodes_examined / took);

        return !result.move.empty();
    }
}