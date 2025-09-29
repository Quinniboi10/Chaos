#include "tui.h"
#include "types.h"
#include "util.h"

#ifdef ENABLE_TUI

#include "board.h"
#include "movegen.h"
#include "searcher.h"

struct Coordinate {
    int x;
    int y;

    Coordinate() = default;
    Coordinate(int x, int y) : x(x), y(y) {}
};

#include <ncursesw/ncurses.h>

enum class CursorLocation {
    BOARD,
    RIGHT_MENU
};

void printTuiBoard(const Board& board, const Move bestMove, const Square from, const Square to) {
    const auto printInfo = [&](const usize line) {
        std::wostringstream ss;
        if (line == 1)
            ss << "FEN: " << board.fen().c_str();
        else if (line == 2)
            ss << "Hash: 0x" << std::hex << std::uppercase << board.zobrist << std::dec;
        else if (line == 3)
            ss << "Side to move: " << (board.stm == WHITE ? "WHITE" : "BLACK");
        else if (line == 4)
            ss << "En passant: " << (board.epSquare == NO_SQUARE ? "-" : squareToAlgebraic(board.epSquare)).c_str();
        return ss.str();
    };

    mvaddwstr(0, 0, L"┌─────────────────┐");
    for (i32 r = 0; r < 8; r++) {
        mvaddwstr(r + 1, 0, L"│");
        for (int f = 0; f < 8; f++) {
            const auto sq = static_cast<Square>((board.stm == WHITE ? 7 - r : r) * 8 + f);
            wchar_t piece = board.getPieceAt(sq);

            int color = ((1ULL << sq) & board.pieces(WHITE)) ? 1 : 2;
            if (sq == from || sq == to)
                color += 2;
            else if (!bestMove.isNull() && (sq == bestMove.from() || sq == bestMove.to()))
                color += 4;

            attron(COLOR_PAIR(color));
            mvaddch(r + 1, 2 * f + 2, piece);
            attroff(COLOR_PAIR(color));
        }
        mvaddwstr(r + 1, 18, L"│");
        mvaddwstr(r + 1, 20, std::to_wstring(r + 1).c_str());
        mvaddwstr(r + 1, 25, printInfo(r + 1).c_str());
    }
    mvaddwstr(9, 0, L"└─────────────────┘");
}

void printMenu(const CursorLocation cursorLoc) {
    if (cursorLoc == CursorLocation::RIGHT_MENU)
        attron(COLOR_PAIR(8));
    mvaddstr(8, 25, "POSITION FROM FEN");
    if (cursorLoc == CursorLocation::RIGHT_MENU)
        attroff(COLOR_PAIR(8));
}
#endif

void launchTui() {
    #ifdef ENABLE_TUI
    // Set up ncurses
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    setlocale(LC_ALL, ""); // Enable Unicode
    start_color();
    init_pair(1, COLOR_YELLOW, COLOR_BLACK);
    init_pair(2, COLOR_BLUE,   COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_WHITE);
    init_pair(4, COLOR_BLUE,   COLOR_WHITE);
    init_pair(5, COLOR_YELLOW, COLOR_CYAN);
    init_pair(6, COLOR_BLUE,   COLOR_CYAN);

    init_pair(8, COLOR_BLACK, COLOR_WHITE);

    // Set up the board
    Board board{};
    board.reset();

    // Set up searcher
    Stopwatch<std::chrono::milliseconds> stopwatch;
    vector<u64>                          posHistory;
    const SearchParameters               params(posHistory, ROOT_CPUCT, CPUCT, ROOT_POLICY_TEMPERATURE, POLICY_TEMPERATURE, false, false, true);
    const SearchLimits                   limits(stopwatch, 0, 0, 0, 0);

    Searcher searcher{};
    searcher.setHash(256);

    searcher.start(board, params, limits);

    Coordinate cursorPos(0, 0);

    Square fromSq = NO_SQUARE;

    MoveList moves = Movegen::generateMoves(board);

    auto cursorLoc = CursorLocation::BOARD;

    const auto handleBoardUpdate = [&]() {
        moves = Movegen::generateMoves(board);

        searcher.stop();
        searcher.start(board, params, limits);
    };

    while (true) {
        const Rank cursorRank = static_cast<Rank>(cursorPos.y);
        const Square cursorSq = cursorLoc == CursorLocation::BOARD ? flipRank(toSquare(cursorRank, static_cast<File>(cursorPos.x))) : NO_SQUARE;
        const Square relativeCursorSq = board.stm == WHITE ? cursorSq : flipRank(cursorSq);

        printTuiBoard(board, searcher.currentMove.load(), fromSq, relativeCursorSq);
        mvaddstr(6, 25, fmt::format("{:+.2f}", wdlToCP(searcher.tree.root().getScore()) / 100.0f).c_str());
        printMenu(cursorLoc);
        refresh();

        // Try to get a character, otherwise simply return to top to refresh the PV move
        int ch = getch();
        if (ch == ERR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (ch == 'q')
            break;

        if (cursorLoc == CursorLocation::BOARD) {
            if (ch == KEY_UP && cursorPos.y > 0)
                cursorPos.y--;
            else if (ch == KEY_DOWN && cursorPos.y < 7)
                cursorPos.y++;
            else if (ch == KEY_LEFT && cursorPos.x > 0)
                cursorPos.x--;
            else if (ch == KEY_RIGHT && cursorPos.x < 7)
                cursorPos.x++;
            else if (ch == KEY_RIGHT)
                cursorLoc = CursorLocation::RIGHT_MENU;

            else if (ch == 10) {
                if (fromSq == NO_SQUARE)
                    fromSq = relativeCursorSq;
                else {
                    const Square toSq = relativeCursorSq;
                    const Move m(squareToAlgebraic(fromSq) + squareToAlgebraic(toSq), board);

                    if (moves.has(m)) {
                        board.move(m);
                        posHistory.push_back(board.zobrist);
                        handleBoardUpdate();
                    }

                    fromSq = NO_SQUARE;
                }
            }
        }
        else if (cursorLoc == CursorLocation::RIGHT_MENU) {
            if (ch == KEY_LEFT)
                cursorLoc = CursorLocation::BOARD;
            else if (ch == 10) {
                // Popup dimensions
                int height = 5;
                int width  = 75;
                int starty = (LINES - height) / 2;
                int startx = (COLS - width) / 2;

                // Create centered popup
                WINDOW* popup = newwin(height, width, starty, startx);
                box(popup, 0, 0);
                mvwprintw(popup, 1, 2, "Enter FEN:");
                wmove(popup, 2, 2);
                wrefresh(popup);

                // Read user input inside popup
                echo();
                char fenBuf[256];
                wgetnstr(popup, fenBuf, sizeof(fenBuf) - 1);
                noecho();

                // Load the FEN
                string fen(fenBuf);

                // Vim
                if (fen == ":q")
                    break;

                board.loadFromFEN(fen);
                posHistory.clear();
                handleBoardUpdate();

                // Clean up popup
                delwin(popup);

                // Redraw main screen
                clear();
            }
        }
    }

    searcher.stop();

    endwin();
    #else
    cout << "This build of Chaos can't run the TUI. Try building with `make tui`" << endl;
    #endif
}