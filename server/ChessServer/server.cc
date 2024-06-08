#include "chess.hh"
#include "search.hh"
#include <unordered_map>
#include <algorithm>
#include <restbed>
#include <fstream>

const std::string root_dir = "D:/chess";
const char *file_paths[] = { "/", "/index.html", "/index.css", "/app.js", "/pieces.png" };

using namespace restbed;

inline int digit_hex_to_int(char x) {
    return x >= 'a' ? x - 'a' + 10 : x - '0';
}

void process_file(const std::shared_ptr<Session> session)
{
    const auto request = session->get_request();

    size_t content_length = request->get_header("Content-Length", 0);

    session->fetch(content_length, [request](const std::shared_ptr<Session> session, const Bytes &body)
    {
        auto bye = [session](int code, const restbed::Bytes& data) {
            session->close(code, data, {
                { "Content-Length", std::to_string(data.size()) },
                { "Connection", "close" },
                { "Access-Control-Allow-Origin", "*" }
            });
        };

        std::string path = request->get_path();

        if (path == "/")
            path += "index.html";

        std::string s = root_dir + path;
        std::ifstream is(s, std::ios::binary);
        restbed::Bytes file(std::istreambuf_iterator<char>(is), { });

        bye(OK, file);
    });
};

void process_move(const std::shared_ptr<Session> session)
{
    const auto request = session->get_request();

    size_t content_length = request->get_header("Content-Length", 0);

    session->fetch(content_length, [request](const std::shared_ptr<Session> session, const Bytes &body)
    {
        auto board = std::make_shared<chessboard>();

        auto bye = [session](int code, std::string data) {
            session->close(code, data.c_str(), {
                { "Content-Length", std::to_string(data.length()) },
                { "Connection", "close" },
                { "Access-Control-Allow-Origin", "*" } 
            });
        };

        std::string sbody = std::string((const char *)body.data(), body.size());

        std::cout << "------------------------------\n";
        std::cout << "Request from " << session->get_origin() << '\n';

        std::istringstream iss(sbody);
        char line[66] = {};
        rated_move response;

        iss.getline(line, 65);

        for (int i = 0; i < 64; i++) {
            if (line[i] >= '0' && line[i] <= '9' || line[i] >= 'a' && line[i] <= 'f') {
                int data = digit_hex_to_int(line[i]);
                int type = data & type_mask, side = data >> side_shift;

                board->pieces[i] = data;

                if (data) {
                    board->side_sets[side] |= 1ull << i;
                    board->piece_sets[type] |= 1ull << i;

                    if ((side & 1) != side || type > pawn || type == 0) {
                        bye(BAD_REQUEST, "Incorrect chess piece format");
                        return;
                    }
                }
            }
            else {
                bye(BAD_REQUEST, "Incorrect chess piece format");
                return;
            }
        }

        int max_depth = 0, max_time = 0, moves = 0;
        iss >> max_depth >> max_time >> moves;

        if (max_depth <= 0 || max_depth > 64) {
            bye(BAD_REQUEST, "Invalid maximum depth value");
            return;
        }

        if (max_time <= 0 || max_time > 30) {
            bye(BAD_REQUEST, "Invalid maximum time value");
            return;
        }

        chessmove m;

        // Initial hash for the board
        board->hash = board->zobrist();

        for (int i = 0; i < moves; i++) {
            iss >> m.org_x >> m.org_y >> m.dest_x >> m.dest_y;
            iss.ignore();

            if (!board->valid_pos(m.org_x, m.org_y) ||
                !board->valid_pos(m.dest_x, m.dest_y)) {
                bye(BAD_REQUEST, "Incorrect move format");
                return;
            }

            bits legal_moves[64] = { 0 };

            board->generate_moves(board->side_to_move, legal_moves);

            if (~legal_moves[m.org_x + m.org_y * 8] & (1ull << m.dest_x + m.dest_y * 8)) {
                bye(BAD_REQUEST, "Illegal move");
                return;
            }

            board->make_move(m.org_x, m.org_y, m.dest_x, m.dest_y);
        }

        board->print();

        engine::iterative_deepening_negamax(*board, response, max_depth, max_time, evaluation::pesto, 5, 4);

        std::ostringstream oss;

        oss <<
            response.move.org_x << ' ' << response.move.org_y << ' ' <<
            response.move.dest_x << ' ' << response.move.dest_y;

        auto ev = evaluation::to_string(response.value);
        
        std::printf("Output move: (%i, %i) -> (%i, %i), score = %s\n",
            response.move.org_x, response.move.org_y,
            response.move.dest_x, response.move.dest_y,
            ev.c_str());

        bye(OK, oss.str());
    });
}

constexpr int file_count = sizeof file_paths / sizeof *file_paths;
std::shared_ptr<Resource> files[file_count];

int main(const int, const char **)
{
    init_lookups();

    auto resource = std::make_shared<Resource>();
    resource->set_path("/chess_engine");
    resource->set_method_handler("POST", process_move);

    for (int i = 0; i < file_count; i++) {
        files[i] = std::make_shared<Resource>();
        files[i]->set_path(file_paths[i]);
        files[i]->set_method_handler("GET", process_file);
    }

    auto settings = std::make_shared<Settings>();
    settings->set_port(2023);

    Service service;
    service.publish(resource);
    for (int i = 0; i < file_count; i++) service.publish(files[i]);
    service.start(settings);

    return EXIT_SUCCESS;
}