#include "board.h"
#include "eval.h"
#include "movegen.h"
#include "searcher.h"
#include "tunable.h"
#include "constants.h"
#include "datagen.h"
#include "policy.h"
#include "tui.h"
#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #undef NOMINMAX
#endif

// ****** UCI OPTIONS ******
usize hash = DEFAULT_HASH;

bool  chess960 = false;
usize multiPV  = 1;

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    Board::fillZobristTable();
    Movegen::initializeAllDatabases();
    initPolicy();

    Board    board{};
    Searcher searcher{};

    string         command;
    vector<string> tokens;

    vector<u64> posHistory;

    bool doUci = false;
    bool uciMinimal = false;

    board.reset();

    const auto exists            = [&](const string& sub) { return command.find(" " + sub + " ") != string::npos; };
    const auto index             = [&](const string& sub, const int offset = 0) { return findIndexOf(tokens, sub) + offset; };
    const auto getValueFollowing = [&](const string& value, const int defaultValue) { return exists(value) ? std::stoi(tokens[index(value, 1)]) : defaultValue; };

    // *********** ./Chaos <ARGS> ************
    if (argc > 1) {
        // Convert args into strings
        vector<string> args;
        args.resize(argc);
        for (int i = 0; i < argc; i++)
            args[i] = argv[i];

        if (args[1] == "bench")
            searcher.bench(argc > 2 ? std::stoi(argv[2]) : 7);
        else if (args[1] == "perft")
            Movegen::perft(board, argc > 2 ? std::stoi(argv[2]) : 5, false);
        else if (args[1] == "bulk")
            Movegen::perft(board, argc > 2 ? std::stoi(argv[2]) : 6, true);
        else if (args[1] == "datagen") {
            std::ostringstream ss{};
            for (usize idx = 2; idx < argc; idx++) {
                ss << args[idx];
                if (idx < argc - 1)
                    ss << " ";
            }
            datagen::run(ss.str());
        }
        else if (args[1].substr(0, 7) == "genfens")
            datagen::genFens(args[1]);

        return 0;
    }

    cout << "Chaos ready and awaiting commands" << endl;
    while (true) {
        std::getline(std::cin, command);
        Stopwatch<std::chrono::milliseconds> commandTime;
        if (command.empty())
            continue;
        tokens = split(command, ' ');

        // ************   UCI   ************

        if (command == "uci") {
            doUci = true;
            cout << "id name Chaos"
#ifdef GIT_HEAD_COMMIT_ID
                 << " (" << GIT_HEAD_COMMIT_ID << ")"
#endif
                 << endl;
            cout << "id author Quinniboi10" << endl;
            cout << "option name Threads type spin default 1 min 1 max 1" << endl;
            cout << "option name Hash type spin default " << DEFAULT_HASH << " min 1 max 1048576" << endl;
            cout << "option name Minimal type check default false" << endl;
            cout << "option name MultiPV type spin default 1 min 1 max 255" << endl;
            cout << "option name UCI_Chess960 type check default false" << endl;
            cout << "option name SearchMode type string default full" << endl;
            cout << "uciok" << endl;
        }
        else if (command == "ucinewgame") {
            searcher.reset();
            board.reset();
            posHistory = { board.zobrist };
        }
        else if (command == "isready")
            cout << "readyok" << endl;
        else if (tokens[0] == "position") {
            board.reset();

            if (tokens[1] == "kiwipete")
                board.loadFromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
            else if (tokens[1] == "fen")
                board.loadFromFEN(command.substr(13));

            posHistory = { board.zobrist };

            if (const i32 idx = findIndexOf(tokens, "moves"); idx != -1) {
                for (i32 mIdx = idx + 1; mIdx < tokens.size(); mIdx++) {
                    board.move(tokens[mIdx]);
                    posHistory.push_back(board.zobrist);
                }
            }
        }
        else if (tokens[0] == "go") {
            const usize depth = getValueFollowing("depth", 0);
            const u64   nodes = getValueFollowing("nodes", 0);

            const usize mtime = getValueFollowing("movetime", 0);
            const usize wtime = getValueFollowing("wtime", 0);
            const usize btime = getValueFollowing("btime", 0);

            const usize winc = getValueFollowing("winc", 0);
            const usize binc = getValueFollowing("binc", 0);

            const bool mate = exists("mate");

            const i64 time = board.stm == WHITE ? wtime : btime;
            const i64 inc  = board.stm == WHITE ? winc : binc;

            const SearchParameters params(posHistory, ROOT_CPUCT, CPUCT, ROOT_POLICY_TEMPERATURE, POLICY_TEMPERATURE, true, doUci, uciMinimal);
            const SearchLimits     limits(commandTime, mate, depth, nodes, mtime, time, inc);
            searcher.start(board, params, limits);
        }
        else if (tokens[0] == "setoption") {
            if (tokens[2] == "Hash")
                searcher.setHash(hash = getValueFollowing("value", DEFAULT_HASH));
            else if (tokens[2] == "Minimal")
                uciMinimal = tokens[findIndexOf(tokens, "value") + 1] == "true";
            else if (tokens[2] == "MultiPV")
                multiPV = getValueFollowing("value", 1);
            else if (tokens[2] == "UCI_Chess960")
                chess960 = tokens[findIndexOf(tokens, "value") + 1] == "true";
            else if (tokens[2] == "SearchMode") {
                const string value = tokens[findIndexOf(tokens, "value") + 1];
                if (value == "policy")
                    searcher.searchMode = POLICY_ONLY;
                else if (value == "value")
                    searcher.searchMode = VALUE_ONLY;
                else
                    searcher.searchMode = FULL_SEARCH;
            }
        }
        else if (command == "stop")
            searcher.stop();
        else if (command == "quit")
            break;

        // ************ NON-UCI ************

        else if (command == "d")
            cout << board << endl;
        else if (command == "tree")
            searcher.launchInteractiveTree();
        else if (tokens[0] == "move")
            board.move(tokens[1]);
        else if (command == "eval")
            cout << evaluate(board) << endl;
        else if (command == "policy")
            searcher.printRootPolicy(board);
        else if (tokens[0] == "perft")
            Movegen::perft(board, std::stoi(tokens[1]), false);
        else if (tokens[0] == "bulk")
            Movegen::perft(board, std::stoi(tokens[1]), true);
        else if (tokens[0] == "perftsuite")
            Movegen::perftSuite(tokens[1]);
        else if (command == "tui")
            launchTui();

        //************  DEBUG  ************

        else if (command == "debug.attacks") {
            cout << "STM attacks" << endl;
            printBitboard(board.attacking[board.stm]);
            cout << "NSTM attacks" << endl;
            printBitboard(board.attacking[~board.stm]);
        }
        else if (command == "debug.moves") {
            const MoveList moves = Movegen::generateMoves(board);
            for (const Move m : moves)
                cout << m << endl;
        }
        else if (command == "debug.checkers")
            printBitboard(board.checkers);
        else if (command == "debug.checkmask")
            printBitboard(board.checkMask);

        else if (command == "debug.isdraw")
            cout << board.isDraw(posHistory) << endl;
        else if (command == "debug.isover")
            cout << board.isGameOver(posHistory) << endl;

        else
            cout << "Unknown command: " << command << endl;
    }


    return 0;
}
