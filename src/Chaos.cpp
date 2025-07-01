#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "searcher.h"
#include "tunable.h"
#include "constants.h"

// ****** UCI OPTIONS ******
usize hash = DEFAULT_HASH;

bool chess960 = false;

int main(int argc, char* argv[]) {
    Board::fillZobristTable();
    Movegen::initializeAllDatabases();

    Board    board;
    Searcher searcher{};

    vector<u64> positionHistory;

    string         command;
    vector<string> tokens;

    board.reset();

    const auto exists            = [&](const string& sub) { return command.find(" " + sub + " ") != string::npos; };
    const auto index             = [&](const string& sub, int offset = 0) { return findIndexOf(tokens, sub) + offset; };
    const auto getValueFollowing = [&](const string& value, int defaultValue) { return exists(value) ? std::stoi(tokens[index(value, 1)]) : defaultValue; };

    // *********** ./Prelude <ARGS> ************
    if (argc > 1) {
        // Convert args into strings
        vector<string> args;
        args.resize(argc);
        for (int i = 0; i < argc; i++)
            args[i] = argv[i];

        if (args[1] == "bench")
            searcher.bench(argc > 2 ? std::stoi(argv[2]) : 7);
        return 0;
    }

    cout << "Chaos ready and awaiting commands" << endl;
    while (true) {
        std::getline(std::cin, command);
        Stopwatch<std::chrono::milliseconds> commandTime;
        if (command == "")
            continue;
        tokens = split(command, ' ');

        // ************   UCI   ************

        if (command == "uci") {
            cout << "id name Chaos"
#ifdef GIT_HEAD_COMMIT_ID
                 << " (" << GIT_HEAD_COMMIT_ID << ")"
#endif
                 << endl;
            cout << "id author Quinniboi10" << endl;
            cout << "option name Threads type spin default 1 min 1 max 1" << endl;
            cout << "option name Hash type spin default " << DEFAULT_HASH << " min 1 max 1048576" << endl;
            cout << "option name UCI_Chess960 type check default false" << endl;
            cout << "uciok" << endl;
        }
        else if (command == "ucinewgame")
            board.reset();
        else if (command == "isready")
            cout << "readyok" << endl;
        else if (tokens[0] == "position") {
            if (tokens[1] == "startpos") {
                board.reset();
                if (tokens.size() > 2 && tokens[2] == "moves")
                    for (usize i = 3; i < tokens.size(); i++) {
                        positionHistory.push_back(board.zobrist);
                        board.move(tokens[i]);
                    }
            }
            else if (tokens[1] == "kiwipete")
                board.loadFromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
            else if (tokens[1] == "fen") {
                board.loadFromFEN(command.substr(13));
                if (tokens.size() > 8 && tokens[8] == "moves")
                    for (usize i = 9; i < tokens.size(); i++) {
                        positionHistory.push_back(board.zobrist);
                        board.move(tokens[i]);
                    }
            }
        }
        else if (tokens[0] == "go") {
            const usize depth = getValueFollowing("depth", 0);
            const u64 nodes = getValueFollowing("nodes", 0);

            const usize wtime = getValueFollowing("wtime", 0);
            const usize btime = getValueFollowing("btime", 0);

            const usize winc = getValueFollowing("winc", 0);
            const usize binc = getValueFollowing("binc", 0);

            const i64 time = board.stm == WHITE ? wtime : btime;
            const i64 inc = board.stm == WHITE ? winc : binc;

            const SearchParameters params(positionHistory, CPUCT, true);
            const SearchLimits limits(commandTime, hash, depth, nodes, time, inc);
            searcher.start(board, params, limits);
        }
        else if (tokens[0] == "setoption") {
            if (tokens[2] == "Hash")
                searcher.setHash(hash = getValueFollowing("value", DEFAULT_HASH));
        }

        // ************ NON-UCI ************

        else if (command == "d")
            cout << board << endl;
        else if (command == "tree")
            searcher.displayRootTree();
        else if (tokens[0] == "move")
            board.move(tokens[1]);
        else if (command == "eval")
            cout << materialEval(board) << endl;
        else if (tokens[0] == "perft")
            Movegen::perft(board, std::stoi(tokens[1]), false);
        else if (tokens[0] == "bulk")
            Movegen::perft(board, std::stoi(tokens[1]), true);
        else if (tokens[0] == "perftsuite")
            Movegen::perftSuite(tokens[1]);

        //************  DEBUG  ************

        else if (command == "debug.attacks") {
            cout << "STM attacks" << endl;
            printBitboard(board.attacking[board.stm]);
            cout << "NSTM attacks" << endl;
            printBitboard(board.attacking[~board.stm]);
        }
        else if (command == "debug.moves") {
            MoveList moves = Movegen::generateMoves(board);
            for (Move m : moves)
                cout << m << endl;
        }
        else if (command == "debug.checkers")
            printBitboard(board.checkers);
        else if (command == "debug.checkmask")
            printBitboard(board.checkMask);

        else
            cout << "Unknown command: " << command << endl;
    }


    return 0;
}