#include "chess.hh"
#include <random>

template<typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

template<typename T> int between(T x, T a, T b) {
    return a <= x && x < b;
}

bits zobrist_table[64][16];
bits capture_masks[64][6];
sliding_mask bishop_masks[64][2][2];
sliding_mask rook_masks[64][2][2];
const sliding_mask *masks_fw[64][4];
const sliding_mask *masks_rev[64][4];

void init_lookups() {
    std::memset(capture_masks, 0, sizeof(capture_masks));
    std::memset(bishop_masks, 0, sizeof(bishop_masks));
    std::memset(rook_masks, 0, sizeof(rook_masks));

    //std::random_device rd;
    std::mt19937_64 e(339532);

    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 16; j++)
            zobrist_table[i][j] = e();

    for (int i = 0; i < 64; i++) {
        masks_fw[i][0] = &bishop_masks[i][0][1];
        masks_fw[i][1] = &bishop_masks[i][1][1];
        masks_fw[i][2] = &rook_masks[i][1][0];
        masks_fw[i][3] = &rook_masks[i][1][1];
        masks_rev[i][0] = &bishop_masks[i][0][0];
        masks_rev[i][1] = &bishop_masks[i][1][0];
        masks_rev[i][2] = &rook_masks[i][0][0];
        masks_rev[i][3] = &rook_masks[i][0][1];
    }

    int I = 0;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++, I++) {
            auto a = [x, y](int i, int j) mutable {
                i += x, j += y;
                if ((i & 7) == i && (j & 7) == j)
                    return 1ull << i + j * 8;
                return 0ull;
            };

            for (int i = -2; i <= 2; i += 4)
                for (int j = -1; j <= 1; j += 2) {
                    capture_masks[I][knight] |= a(i, j);
                    capture_masks[I][knight] |= a(j, i);
                }

            for (int i = 0; i <= 1; i++) {
                for (int j = 0; j <= 1; j++) {
                    int X = 0, Y = 0;
                    bits M = 0;
                    do {
                        if (j) Y += i * 2 - 1;
                        else X += i * 2 - 1;
                        bits value = rook_masks[I][i][j].last |= M = a(X, Y);
                        if (M) rook_masks[I][i][j].steps[(x + X) + (y + Y) * 8] = value;
                    } while (M);
                }
            }

            for (int i = 0; i <= 1; i++) {
                for (int j = 0; j <= 1; j++) {
                    int X = 0, Y = 0;
                    bits M = 0;
                    do {
                        X += i * 2 - 1;
                        Y += j * 2 - 1;
                        bits value = bishop_masks[I][i][j].last |= M = a(X, Y);
                        if (M) bishop_masks[I][i][j].steps[(x + X) + (y + Y) * 8] = value;
                    } while (M);
                }
            }

            for (int j = -1; j <= 1; j += 2) {
                for (int i = -1; i <= 1; i++)
                    capture_masks[I][king] |= a(i, j);
                capture_masks[I][king] |= a(j, 0);
            }
        }
    }
}

void chessboard::make_move(int org_x, int org_y, int dest_x, int dest_y) {
    if (!org_x && !org_y && !dest_x && !dest_y) {
        side_to_move ^= 1;
        hash ^= zobrist_table[1][8];
        move_stack.push_back(chessmove());
        move_count++;
        return;
    }

    bool old_pawn_two_squares = false;

    if (move_stack.size()) {
        const auto &last = move_stack.back();
        old_pawn_two_squares =
            (pieces[last.dest_x + last.dest_y * 8] & type_mask) == pawn
            && std::abs(last.org_y - last.dest_y) == 2;
    }

    int org_ind = org_x + org_y * 8;
    int dest_ind = dest_x + dest_y * 8;
    bits org_mask = 1ull << org_ind;
    bits dest_mask = 1ull << dest_ind;

    // Remove moving piece from its original location
    int org_type, org_side;
    int org = piecetype(org_x, org_y, org_type, org_side);
    bool org_had_moved = has_moved & org_mask;

    has_moved &= ~org_mask;

    int delta_x = org_x - dest_x, delta_y = org_y - dest_y;
    int change_x = std::abs(delta_x), change_y = std::abs(delta_y);

    bool two_squares_changed = (change_y == 2 && org_type == pawn) != old_pawn_two_squares;

    if (two_squares_changed)
        hash ^= zobrist_table[0][8];

    // This is castling, move the rook
    if (org_type == king && change_x == 2) {
        int rook_org = (delta_x > 0 ? 0 : 7) + org_y * 8;
        int rook_dst = (dest_x + delta_x / 2) + org_y * 8;
        int rk = rook | org_side << side_shift;
        size_t rook_mask = 1ull << rook_org | 1ull << rook_dst;
        side_sets[org_side] ^= rook_mask;
        piece_sets[rook] ^= rook_mask;
        std::swap(pieces[rook_org], pieces[rook_dst]);
        hash ^= zobrist_table[rook_org][rk] ^ zobrist_table[rook_dst][rk];
    }

    int cap_type, cap_side;
    int captured = piecetype(dest_x, dest_y, cap_type, cap_side);
    int captured_x = dest_x, captured_y = dest_y;

    // This is en passant, capture the enemy pawn a rank behind
    if (!captured && org_type == pawn && change_x == 1)
        captured = piecetype(dest_x, captured_y = org_side ? dest_y + 1 : dest_y - 1, cap_type, cap_side);

    int cap_ind = captured_x + captured_y * 8;
    bits captured_mask = ~(1ull << cap_ind);
    bool captured_had_moved = has_moved & ~captured_mask;

    has_moved &= captured_mask;
    side_sets[cap_side] &= captured_mask;
    piece_sets[cap_type] &= captured_mask;
    piecetype(captured_x, captured_y) = 0;
    if (captured) hash ^= zobrist_table[cap_ind][captured];

    // Place the moving piece in its new location
    has_moved |= dest_mask;
    side_sets[org_side] ^= org_mask ^ dest_mask;
    piece_sets[org_type] ^= org_mask ^ dest_mask;
    piecetype(org_x, org_y) = 0;
    piecetype(dest_x, dest_y) = org;
    hash ^= zobrist_table[org_ind][org] ^ zobrist_table[dest_ind][org];

    int promotion = 0;

    // Promote to queen
    if (org_type == pawn && dest_y == (1 - org_side) * 7) {
        piece_sets[pawn] &= ~dest_mask;
        piece_sets[queen] |= dest_mask;
        int new_type = promotion = queen | org_side << side_shift;
        piecetype(dest_x, dest_y) = new_type;
        hash ^= zobrist_table[dest_ind][org] ^ zobrist_table[dest_ind][new_type];
    }

    side_to_move ^= 1;
    hash ^= zobrist_table[1][8];

    previous_states[hash]++;

    move_stack.push_back(chessmove {
        org_x, org_y, org_had_moved,
        dest_x, dest_y,
        captured_x, captured_y, captured, captured_had_moved,
        promotion, two_squares_changed
    });
    move_count++;
}

void chessboard::unmake_move()
{
    chessmove& move = move_stack.back(); move_stack.pop_back();
    move_count--;
    side_to_move ^= 1;

    if (!move.empty()) previous_states[hash]--;

    hash ^= zobrist_table[1][8];

    if (move.empty())
        return;

    if (move.two_squares_flag_changed)
        hash ^= zobrist_table[0][8];

    int org_ind = move.org_x + move.org_y * 8;
    int dest_ind = move.dest_x + move.dest_y * 8;
    int cap_ind = move.captured_x + move.captured_y * 8;
    int cap_type, cap_side, org_type, org_side;

    int org_piece = piecetype(move.dest_x, move.dest_y, org_type, org_side);

    int cap_piece = move.captured_type;
    cap_type = cap_piece & type_mask;
    cap_side = cap_piece >> side_shift;

    bits cap_mask = 1ull << cap_ind;
    bits org_mask = 1ull << org_ind;
    bits dest_mask = 1ull << dest_ind;

    int delta_x = move.org_x - move.dest_x, delta_y = move.org_y - move.dest_y;
    int change_x = std::abs(delta_x), change_y = std::abs(delta_y);

    // This is castling, move the rook
    if (org_type == king && change_x == 2) {
        int rook_org = (delta_x > 0 ? 0 : 7) + move.org_y * 8;
        int rook_dst = (move.dest_x + delta_x / 2) + move.org_y * 8;
        int rk = rook | org_side << side_shift;
        size_t rook_mask = 1ull << rook_org | 1ull << rook_dst;
        side_sets[org_side] ^= rook_mask;
        piece_sets[rook] ^= rook_mask;
        std::swap(pieces[rook_org], pieces[rook_dst]);
        hash ^= zobrist_table[rook_org][rk] ^ zobrist_table[rook_dst][rk];
    }

    has_moved = has_moved & ~(org_mask | dest_mask) | (bits)move.org_had_moved << org_ind;
    side_sets[org_side] ^= dest_mask ^ org_mask;
    piece_sets[org_type] ^= dest_mask;
    pieces[dest_ind] = 0;
    pieces[org_ind] = (org_type = move.promotion_type ? pawn : org_type) | org_side << side_shift;
    piece_sets[org_type] ^= org_mask;
    hash ^= zobrist_table[dest_ind][org_piece] ^ zobrist_table[org_ind][pieces[org_ind]];

    if (move.captured_type) {
        has_moved = has_moved & ~cap_mask | (bits)move.captured_had_moved << cap_ind;
        pieces[cap_ind] = cap_piece;
        side_sets[cap_side] |= cap_mask;
        piece_sets[cap_type] |= cap_mask;
        hash ^= zobrist_table[cap_ind][cap_piece];
    }
}

size_t chessboard::zobrist() {
    size_t h = 0;

    int pawn_two_squares = 0;

    if (move_stack.size()) {
        const auto &last = move_stack.back();
        pawn_two_squares =
            (pieces[last.dest_x + last.dest_y * 8] & type_mask) == pawn
            && std::abs(last.org_y - last.dest_y) == 2;
    }

    h ^= pawn_two_squares * zobrist_table[0][8];
    h ^= side_to_move * zobrist_table[1][8];

    for (int i = 0; i < 64; i++)
        if (int p = pieces[i])
            h ^= zobrist_table[i][p];

    return h;
}

bool chessboard::any_pseudo_captures(int side, size_t target) {
    unsigned long ind;

    const chessmove &last_move = move_stack.size() ? move_stack.back() : chessmove{};
    bits all_pieces = side_sets[0] | side_sets[1];
    bits last_move_dest = 1ull << last_move.dest_y * 8 + last_move.dest_x;

    int fside = side * 2 - 1;
    bits our = side_sets[side], theirs = side_sets[side ^ 1];
    bits free = ~all_pieces;
    bits not_friendly = free | theirs;

    // Process pawns
    bits pawns = piece_sets[pawn] & our;

    while (pawns) {
        _BitScanForward64(&ind, pawns);

        bits bit = 1ull << ind;
        bits capture_base = (0b101ull << (ind % 8) >> 1 & 255ull) << ((ind / 8 - fside) << 3);
        if (capture_base & target)
            return true;

        bool last_moved_enemy_pawn = piece_sets[pawn] & last_move_dest;

        if (int(last_move.org_y - last_move.dest_y == (fside * 2) && last_moved_enemy_pawn)
            * ((side ? last_move_dest << 8 : last_move_dest >> 8) & capture_base & target))
            return true;

        pawns &= pawns - 1;
    }

    bits set = (piece_sets[knight] | piece_sets[king]) & our;

    while (set) {
        _BitScanForward64(&ind, set);

        if (capture_masks[ind][pieces[ind] & type_mask] & target)
            return true;

        set &= set - 1;
    }

    // Process sliding pieces
    int sliding_types[] = { bishop, rook, queen };

    for (int i = 0; i < 3; i++) {
        int type = sliding_types[i];
        bits sliding = piece_sets[type] & our;

        const int start = (type == rook) ? 2 : 0;
        const int end = (type == bishop) ? 2 : 4;

        while (sliding) {
            _BitScanForward64(&ind, sliding);

            const sliding_mask **fw = masks_fw[ind];
            const sliding_mask **rev = masks_rev[ind];

            for (int i = start; i < end; i++) {
                long long maskfw = fw[i]->last & all_pieces;

                if ((maskfw & -maskfw & target) ||
                    (_BitScanReverse64(&ind, rev[i]->last & all_pieces) && (1ull << ind) & target))
                    return true;
            }

            sliding &= sliding - 1;
        }
    }

    return false;
}

bool chessboard::any_moves(int side) {
    bits b[64] = {0};
    return generate_moves(side, b, false, true);
}

bool chessboard::in_check(int side) {
    return any_pseudo_captures(side ^ 1, piece_sets[king] & side_sets[side]);
}

bits chessboard::legalize(int side, int i, bits moves)
{
    bits b = 0;
    unsigned long ind;
    int ox = i & 7, oy = i >> 3;

    while (moves) {
        _BitScanForward64(&ind, moves);
        if (is_move_safe(side, ox, oy, ind & 7, ind >> 3)) b |= 1ull << ind;
        moves &= moves - 1;
    }

    return b;
}

bool chessboard::generate_moves(int side, bits *matrices, bool pseudo, bool exit_on_legal, bits mask) {
    unsigned long ind;

    const chessmove &last_move = move_stack.size() ? move_stack.back() : chessmove{};
    bits all_pieces = side_sets[0] | side_sets[1];
    bits last_move_dest = 1ull << last_move.dest_y * 8 + last_move.dest_x;

    int fside = side * 2 - 1;
    bits our = side_sets[side], theirs = side_sets[side ^ 1];
    bits free = ~all_pieces;
    bits not_friendly = free | theirs;

    // Process pawns
    bits pawns = piece_sets[pawn] & our;

    bits shifted_free = side ? free >> 8 : free << 8;

    while (pawns) {
        _BitScanForward64(&ind, pawns);

        bits bit = 1ull << ind;
        bits capture_base = (0b101ull << (ind % 8) >> 1 & 255ull) << ((ind / 8 - fside) << 3);
        bits capture_mask = capture_base & theirs;
        bits step_mask = (side ? bit >> 8 : bit << 8) & free;
        bits double_mask = (~has_moved >> ind & 1) * ((side ? bit >> 16 : bit << 16) & free & shifted_free);

        bool last_moved_enemy_pawn = piece_sets[pawn] & last_move_dest;
        bits en_passant_mask =
            int(last_move.dest_y - last_move.org_y == (fside * 2) && last_moved_enemy_pawn)
            * ((side ? last_move_dest << 8 : last_move_dest >> 8) & capture_base);

        matrices[ind] = capture_mask | step_mask | double_mask | en_passant_mask;

        if (!pseudo && (matrices[ind] = legalize(side, ind, matrices[ind] & mask)) && exit_on_legal)
            return true;

        pawns &= pawns - 1;
    }

    // Process non-sliding pieces
    int types[] = { knight, king };

    for (int i = 0; i < 2; i++) {
        bits set = piece_sets[types[i]] & our;

        while (set) {
            _BitScanForward64(&ind, set);

            matrices[ind] = capture_masks[ind][types[i]] & not_friendly;

            if (types[i] == king) {
                int x = ind & 7, y = ind >> 3;

                auto piece = [this, all_pieces, x, y](int i, int j) -> bool {
                    i += x, j += y;
                    return (i & 7) == i && (j & 7) == j && all_pieces & (1ull << (i | j << 3));
                };

                auto can_castle = [this, piece, side, x, y, pseudo](int rx, int ry) -> bool {
                    if (has_moved & 1ull << rx + ry * 8ll ||
                        has_moved & 1ull << x + y * 8ll)
                        return false;

                    int dx = sgn(rx - x), X = -1;

                    for (int i = 1; between(X = x + i * dx, 2, 7); i++)
                        if (piece(i * dx, 0) || (i != 1 && !is_move_safe(side, x, y, X, y)))
                            return false;

                    return true;
                };

                if (!in_check(side)) {
                    bits relevant_rooks = our & piece_sets[rook];
                    bits left_rook = 1ull << 8 * y, right_rook = 1ull << 7 + 8 * y;

                    if (relevant_rooks & left_rook && can_castle(0, y)) {
                        matrices[ind] |= 1ull << ind - 2;

                        if (!pseudo && (matrices[ind] = legalize(side, ind, matrices[ind] & mask)) && exit_on_legal)
                            return true;
                    }

                    if (relevant_rooks & right_rook && can_castle(7, y)) {
                        matrices[ind] |= 1ull << ind + 2;

                        if (!pseudo && (matrices[ind] = legalize(side, ind, matrices[ind] & mask)) && exit_on_legal)
                            return true;
                    }
                }
            }

            if (!pseudo && (matrices[ind] = legalize(side, ind, matrices[ind] & mask)) && exit_on_legal)
                return true;

            set &= set - 1;
        }
    }

    // Process sliding pieces
    int sliding_types[] = { bishop, rook, queen };

    for (int i = 0; i < 3; i++) {
        int type = sliding_types[i];
        bits sliding = piece_sets[type] & our;

        const int start = (type == rook) ? 2 : 0;
        const int end = (type == bishop) ? 2 : 4;

        while (sliding) {
            _BitScanForward64(&ind, sliding);

            bits captures = 0;

            const sliding_mask **fw = masks_fw[ind];
            const sliding_mask **rev = masks_rev[ind];

            for (int i = start; i < end; i++) {
                unsigned long b;

                bits maskfw = fw[i]->last, maskrev = rev[i]->last;

                if (_BitScanForward64(&b, fw[i]->last & all_pieces))
                    maskfw = fw[i]->steps[b];

                if (_BitScanReverse64(&b, rev[i]->last & all_pieces))
                    maskrev = rev[i]->steps[b];

                captures |= (maskfw | maskrev) & not_friendly;
            }

            matrices[ind] = captures;

            if (!pseudo && (matrices[ind] = legalize(side, ind, matrices[ind] & mask)) && exit_on_legal)
                return true;

            sliding &= sliding - 1;
        }
    }

    return false;
}

void chessboard::print() {
    for (int i = 0; i < 64; i++) {
        std::printf("%x", pieces[i]);
        if (i % 8 == 7)
            std::printf("\n");
    }
}