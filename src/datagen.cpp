#include "datagen.h"

#include "board.h"
#include "searcher.h"
#include "stopwatch.h"
#include "movegen.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <csignal>
#include <mutex>
#include <pthread.h>

using VisitDistribution = vector<std::pair<u16, u32>>;

struct __attribute__((packed)) MontyFormatBoard {
    array<u64, 4> bbs;
    u8            stm;
    Square        epSquare;
    u8            castleRights;
    u8            halfMoveClock;
    u16           fullMoveClock;

    MontyFormatBoard() = default;

    MontyFormatBoard(const Board& board) {
        const array<u64, 8> raw = { board.pieces(WHITE),  board.pieces(BLACK), board.pieces(PAWN),  board.pieces(KNIGHT),
                                    board.pieces(BISHOP), board.pieces(ROOK),  board.pieces(QUEEN), board.pieces(KING) };

        constexpr usize blackK = 0b1;
        constexpr usize blackQ = 0b10;
        constexpr usize whiteK = 0b100;
        constexpr usize whiteQ = 0b1000;

        usize flags = 0;

        if (board.castling[castleIndex(WHITE, true)])
            flags |= whiteK;
        if (board.castling[castleIndex(WHITE, false)])
            flags |= whiteQ;
        if (board.castling[castleIndex(BLACK, true)])
            flags |= blackK;
        if (board.castling[castleIndex(BLACK, false)])
            flags |= blackQ;

        bbs           = { raw[1], raw[5] ^ raw[6] ^ raw[7], raw[3] ^ raw[4] ^ raw[7], raw[2] ^ raw[4] ^ raw[6] };
        stm           = board.stm ^ 1;
        epSquare      = board.epSquare;
        castleRights  = flags;
        halfMoveClock = board.halfMoveClock;
        fullMoveClock = board.fullMoveClock;
    }
};

u16 asMontyMove(const Board& board, const Move m) {
    enum MontyMoveType : u16 {
        QUIET       = 0,
        DOUBLE_PUSH = 1,
        CASTLE_K    = 2,
        CASTLE_Q    = 3,
        CAPTURE     = 4,
        EP          = 5,
        PROMO_K     = 8,
        PROMO_B     = 9,
        PROMO_R     = 10,
        PROMO_Q     = 11,
        PROMOC_K    = 12,
        PROMOC_B    = 13,
        PROMOC_R    = 14,
        PROMOC_Q    = 15
    };

    const u16 from = m.from();
    const u16 to   = m.to();

    MontyMoveType flag = QUIET;
    if (m.typeOf() == MoveType::CASTLE)
        flag = to > from ? CASTLE_K : CASTLE_Q;
    else if (m.typeOf() == MoveType::EN_PASSANT)
        flag = EP;
    else if (m.typeOf() == MoveType::PROMOTION) {
        const bool capture = board.isCapture(m);

        switch (m.promo()) {
        case KNIGHT:
            flag = capture ? PROMOC_K : PROMO_K;
            break;
        case BISHOP:
            flag = capture ? PROMOC_B : PROMO_B;
            break;
        case ROOK:
            flag = capture ? PROMOC_R : PROMO_R;
            break;
        case QUEEN:
            flag = capture ? PROMOC_Q : PROMO_Q;
            break;
        default:
            break;
        }
    }
    else if (board.isCapture(m))
        flag = CAPTURE;
    else if (board.getPiece(from) == PAWN && std::abs(from - to) == 16)
        flag = DOUBLE_PUSH;

    return (from << 10) | (to << 4) | flag;
}

struct MontyFormatMove {
    u16               bestMove;
    double            rootQ;
    VisitDistribution visits;

    explicit MontyFormatMove(const Searcher& searcher, const Move m) {
        const Node& root     = searcher.nodes[{ 0, searcher.currentHalf }];
        const u64   firstIdx = root.firstChild.load().index();

        bestMove = asMontyMove(searcher.rootPos, m);
        rootQ    = root.getScore();

        for (u64 idx = firstIdx; idx < firstIdx + root.numChildren; idx++) {
            const Node& node = searcher.nodes[{ idx, searcher.currentHalf }];
            const u16   move = asMontyMove(searcher.rootPos, node.move);

            visits.emplace_back(move, node.visits);
        }
    }
};

struct FileWriter {
    Board                   board;
    MontyFormatBoard        compressedBoard;
    vector<MontyFormatMove> moves;

    std::ofstream file;

    explicit FileWriter(const string& filePath) {
        board.reset();
        compressedBoard = MontyFormatBoard(board);

        file = std::ofstream(filePath, std::ios::app | std::ios::binary);

        if (!file.is_open()) {
            cerr << "Error: Could not open the file " << filePath << " for writing." << endl;
            exit(-1);
        }
    }

    void setStartpos(const Board& board) {
        this->board     = board;
        compressedBoard = MontyFormatBoard(board);
    }

    void addMove(const Searcher& searcher, const Move m) { moves.emplace_back(searcher, m); }

    void writeU8(const u8 value) { file.write(reinterpret_cast<const char*>(&value), sizeof(u8)); }

    void writeU16(const u16 value) { file.write(reinterpret_cast<const char*>(&value), sizeof(u16)); }

    template<typename T>
    void write(const T& value) {
        file.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void writeGame(const usize wdl) {
        sigset_t set, oldset;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        pthread_sigmask(SIG_BLOCK, &set, &oldset);

        write(compressedBoard);

        const auto getFile = [](const Square sq, const File fallback) { return sq == NO_SQUARE ? fallback : fileOf(sq); };

        writeU8(getFile(board.castling[castleIndex(WHITE, false)], AFILE));
        writeU8(getFile(board.castling[castleIndex(WHITE, true)], HFILE));
        writeU8(getFile(board.castling[castleIndex(BLACK, false)], AFILE));
        writeU8(getFile(board.castling[castleIndex(BLACK, true)], HFILE));

        writeU8(wdl);

        for (MontyFormatMove& move : moves) {
            writeU16(move.bestMove);
            writeU16((move.rootQ + 1) / 2 * std::numeric_limits<u16>::max());

            // Sort by the move
            std::sort(move.visits.begin(), move.visits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            const u8 count = static_cast<u8>(move.visits.size());
            writeU8(count);

            if (count > 0) {
                u32 maxVisits = 0;
                for (const auto& [_, visits] : move.visits)
                    maxVisits = std::max(maxVisits, visits);

                for (const auto& [_, visits] : move.visits)
                    writeU8(static_cast<u8>(visits * 255.0 / maxVisits));
            }
        }

        writeU16(0);
        file.flush();

        sigset_t pending;
        sigpending(&pending);
        const bool hadSigint = sigismember(&pending, SIGINT);

        pthread_sigmask(SIG_SETMASK, &oldset, nullptr);

        if (hadSigint)
            raise(SIGINT);

        moves.clear();
    }
};

void makeRandomMove(Board& board) {
    const MoveList moves = Movegen::generateMoves(board);
    assert(moves.length > 0);

    std::random_device                 rd;
    std::mt19937_64                    engine(rd());
    std::uniform_int_distribution<int> dist(0, moves.length - 1);

    board.move(moves.moves[dist(engine)]);
}

string makeFileName() {
    std::random_device rd;

    std::mt19937_64 engine(rd());

    std::uniform_int_distribution<int> dist(0, INF_INT);

    // Get current time
    const auto now = std::chrono::system_clock::now();

    // Convert to time_t
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    // Convert to tm structure
    std::tm tm;

#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    const string randomStr = std::to_string(dist(engine));

    return "data-" + fmt::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) + "-" + randomStr + ".chaosdata";
}

void runThread(const u64 nodes, Board& board, std::mutex& boardMutex, atomic<u64>& positions, const atomic<bool>& stop) {
    string filePath = "./data/" + makeFileName();

    if (!std::filesystem::is_directory("./data/"))
        std::filesystem::create_directory("./data/");

    FileWriter fileWriter(filePath);

    Searcher searcher;

    std::random_device                 rd;
    std::mt19937_64                    engine(rd());
    std::uniform_int_distribution<int> dist(0, 1);
    const auto                         randBool = [&]() { return dist(engine); };

    Stopwatch<std::chrono::milliseconds> stopwatch;
    vector<u64>                          posHistory;
    const SearchParameters               params(posHistory, CPUCT, datagen::TEMPERATURE, false, false);
    const SearchLimits                   limits(stopwatch, 0, nodes, 0, 0);

    searcher.setHash(datagen::HASH_PER_T);

mainLoop:
    while (!stop.load()) {
        usize randomMoves = datagen::RAND_MOVES + randBool();

        std::unique_lock lk(boardMutex);
        board.reset();
        lk.unlock();


        for (usize i = 0; i < randomMoves; i++) {
            lk.lock();
            makeRandomMove(board);
            if (board.isGameOver(posHistory))
                goto mainLoop;
            lk.unlock();
        }

        fileWriter.setStartpos(board);
        posHistory = { board.zobrist };


        while (!board.isGameOver(posHistory)) {
            const Move m = searcher.start(board, params, limits);
            assert(!m.isNull());

            fileWriter.addMove(searcher, m);

            lk.lock();
            board.move(m);
            lk.unlock();
            posHistory.push_back(board.zobrist);

            positions++;
        }

        usize wdl;
        if (board.isDraw() || !board.inCheck())
            wdl = 1;
        else if (board.stm == WHITE)
            wdl = 0;
        else
            wdl = 2;

        fileWriter.write(wdl);
        posHistory.clear();
    }
}

void datagen::run(const string& params) {
    if (params.empty())
        return;

    vector<string> tokens = split(params, ' ');

    const auto getValueFollowing = [&](const string& value, const auto& defaultValue) {
        const auto  loc = std::find(tokens.begin(), tokens.end(), value);
        const usize idx = std::distance(tokens.begin(), loc) + 1;
        if (loc == tokens.end() || idx >= tokens.size()) {
            std::ostringstream ss;
            ss << defaultValue;
            return ss.str();
        }
        return tokens[idx];
    };

    const usize threadCount  = std::stoul(getValueFollowing("threads", 1));
    const u64   numPositions = std::stoull(getValueFollowing("positions", 100'000'000));
    const u64   nodes        = std::stoull(getValueFollowing("nodes", 2'000));

    Stopwatch<std::chrono::milliseconds> time;
    vector<std::thread>                  threads;
    vector<atomic<bool>>                 running(threadCount);
    vector<atomic<u64>>                  positions(threadCount);
    vector<Board>                        boards(threadCount);
    vector<std::mutex>                   boardMutexes(threadCount);
    atomic<bool>                         stop(false);

    for (auto& p : positions)
        p.store(0, std::memory_order_relaxed);
    for (auto& b : boards)
        b.reset();

    for (usize i = 0; i < threadCount; i++)
        threads.emplace_back(runThread, nodes, std::ref(boards[i]), std::ref(boardMutexes[i]), std::ref(positions[i]), std::cref(stop));

    cursor::hide();

    u64 totalPositions = 0;

    while (totalPositions < numPositions) {
        boardMutexes[0].lock();
        Board board = boards[0];
        boardMutexes[0].unlock();

        if (board.stm == BLACK)
            continue;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        cursor::goTo(1, 10);

        totalPositions = 0;
        for (usize i = 0; i < threadCount; i++)
            totalPositions += positions[i].load();

        totalPositions = std::max<u64>(1, totalPositions);

        std::ostringstream ss{};

        cursor::clearAll(ss);
        ss << "************ Chaos Datagen In Progress... ************" << "\n";
        ss << "\n";
        ss << "*** Parameters ***" << "\n";
        ss << "Threads:   " << threadCount << "\n";
        ss << "Positions: " << suffixNum(numPositions) << "\n";
        ss << "Nodes:     " << nodes << "\n";
        ss << "\n";
        ss << "\n";
        ss << "\n";
        ss << board << "\n";
        ss << "\n";
        ss << "\n";
        progressBar(50, static_cast<double>(totalPositions) / numPositions, Colors::GREEN, ss);
        ss << "\n";
        ss << Colors::GREY << "Positions:            " << Colors::RESET << suffixNum(totalPositions) << "\n";
        ss << Colors::GREY << "Positions per second: " << Colors::RESET << fmt::format(fmt::runtime("{:.2f}"), static_cast<float>(totalPositions) * 1000 / time.elapsed()) << "\n";
        ss << "\n";
        ss << Colors::GREY << "Time elapsed:             " << Colors::RESET << formatTime(time.elapsed()) << "\n";
        ss << Colors::GREY << "Estimated time remaining: " << Colors::RESET << formatTime(time.elapsed() * numPositions / std::min(totalPositions, numPositions) - time.elapsed()) << "\n";

        cout << ss.str() << std::flush;

        cout.flush();
    }

    cursor::home();
    cout << "************ Chaos Datagen Complete! ************" << endl;

    stop.store(true);
    for (std::thread& thread : threads)
        if (thread.joinable())
            thread.join();
    cursor::goTo(1, 30);
    cursor::show();
}