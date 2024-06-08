#pragma once
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <bitset>
#include <limits>
#include <algorithm>
#include <stack>
#include <unordered_map>
#include <intrin.h>
#include <unordered_set>
#include "fastmap.hh"

typedef size_t bits;

struct sliding_mask {
    bits steps[64];
    bits last;
};

enum : int {
    none, king, queen, bishop, knight, rook, pawn,
    type_mask = 0b0111, side_shift = 3
};

void init_lookups();

struct chessmove {
    int org_x = 0, org_y = 0, org_had_moved = 0;
    int dest_x = 0, dest_y = 0;
    int captured_x = 0, captured_y = 0, captured_type = 0, captured_had_moved = 0;
    int promotion_type = 0, two_squares_flag_changed = 0;

    inline bool empty() const { return !org_x && !org_y && !dest_x && !dest_y; }
};

struct chessboard {
private:
    bits legalize(int side, int i, bits moves);
public:
    int appended_moves = 0;

    //std::unordered_multiset<size_t> previous_states;
    size_t hash = 0;
    fastmap<uint16_t> previous_states;
    std::vector<chessmove> move_stack;
    std::array<char, 64> pieces{ 0 };
    bits side_sets[2]{ 0 }, piece_sets[pawn + 1]{ 0 };
    bits has_moved = 0;
    int move_count = 0, side_to_move = 1;

    chessboard() { move_stack.reserve(64); }

    inline bool valid_pos(int x, int y) const { return (x & 7) == x && (y & 7) == y; }

    inline char &piecetype(int x, int y) { return pieces[y * 8ll + x]; }

    inline char &piecetype(int x, int y, int &type, int &side) {
        char &p = pieces[y * 8ll + x];
        type = p & type_mask;
        side = p >> side_shift;
        return p;
    }

    inline size_t count_pieces() {
        return __popcnt64(side_sets[0] | side_sets[1]);
    }

    void make_move(int org_x, int org_y, int dest_x, int dest_y);
    void unmake_move();

    size_t zobrist();

    inline bool is_move_safe(int for_side, int org_x, int org_y, int dest_x, int dest_y) {
        make_move(org_x, org_y, dest_x, dest_y);
        bool check = in_check(for_side);
        unmake_move();

        return !check;
    }

    bool any_pseudo_captures(int side, bits target = ~0ull);

    bool any_moves(int side);

    bool in_check(int side);
    bool generate_moves(int side, bits *matrices, bool pseudo = false, bool exit_on_legal = false, bits mask = ~0ull);

    void print();

};