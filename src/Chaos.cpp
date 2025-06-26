#include "board.h"
#include "movegen.h"

bool chess960 = false;

int main(int argc, char* argv[]) {
    Board::fillZobristTable();
    Movegen::initializeAllDatabases();

    Board               board;
    string              command;
    std::vector<string> tokens;

    board.reset();

    // *********** ./Prelude <ARGS> ************
    if (argc > 1) {
        // Convert args into strings
        std::vector<string> args;
        args.resize(argc);
        for (int i = 0; i < argc; i++)
            args[i] = argv[i];

        if (args[1] == "bench")
            Movegen::perft(board, argc > 2 ? std::stoi(argv[2]) : 6, true);
        return 0;
    }

    cout << "Chaos ready and awaiting commands" << endl;
    while (true) {
        std::getline(std::cin, command);
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
            cout << "option name Threads type spin default 1 min 1 max 512" << endl;
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
                    for (usize i = 3; i < tokens.size(); i++)
                        board.move(tokens[i]);
            }
            else if (tokens[1] == "kiwipete")
                board.loadFromFEN("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
            else if (tokens[1] == "fen") {
                board.loadFromFEN(command.substr(13));
                if (tokens.size() > 8 && tokens[8] == "moves")
                    for (usize i = 9; i < tokens.size(); i++)
                        board.move(tokens[i]);
            }
        }

        // ************ NON-UCI ************

        else if (command == "d")
            cout << board << endl;
        else if (tokens[0] == "move")
            board.move(tokens[1]);
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