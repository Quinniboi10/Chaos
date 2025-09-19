#include "datagen.h"

#include "board.h"
#include "searcher.h"
#include "stopwatch.h"
#include "movegen.h"

#include <filesystem>
#include <fstream>
#include <numeric>
#include <csignal>
#include <random>
#include <thread>
#include <mutex>

#ifdef _WIN32
    #include <windows.h>

static volatile bool ctrlCPressed = false;

BOOL WINAPI CtrlHandler(const DWORD fdwCtrlType) {
    if (fdwCtrlType == CTRL_C_EVENT) {
        ctrlCPressed = true;
        return TRUE;  // Signal handled
    }
    return FALSE;  // Pass to next handler
}
#else
    #include <pthread.h>
#endif

using VisitDistribution = vector<std::pair<u16, u32>>;

struct __attribute__((packed)) MontyFormatBoard {
    array<u64, 4> bbs;
    u8            stm;
    u8            epSquare;
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

        if (board.castling[castleIndex(WHITE, true)] != NO_SQUARE)
            flags |= whiteK;
        if (board.castling[castleIndex(WHITE, false)] != NO_SQUARE)
            flags |= whiteQ;
        if (board.castling[castleIndex(BLACK, true)] != NO_SQUARE)
            flags |= blackK;
        if (board.castling[castleIndex(BLACK, false)] != NO_SQUARE)
            flags |= blackQ;

        bbs           = { raw[1], raw[5] ^ raw[6] ^ raw[7], raw[3] ^ raw[4] ^ raw[7], raw[2] ^ raw[4] ^ raw[6] };
        stm           = board.stm == WHITE ? 0 : 1;
        epSquare      = board.epSquare == NO_SQUARE ? 0 : board.epSquare;
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
    u16       to   = m.to();

    MontyMoveType flag = QUIET;
    if (m.typeOf() == MoveType::CASTLE) {
        flag = to > from ? CASTLE_K : CASTLE_Q;
        to   = KING_CASTLE_END_SQ[castleIndex(board.stm, to > from)];
    }
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
        const Node& root     = searcher.tree.root();
        const u64   firstIdx = root.firstChild.load().index();

        visits.reserve(root.numChildren);

        bestMove = asMontyMove(searcher.rootPos, m);
        rootQ    = root.getScore();

        for (u64 idx = firstIdx; idx < firstIdx + root.numChildren; idx++) {
            const Node& node = searcher.tree.activeTree()[idx];
            const u16   move = asMontyMove(searcher.rootPos, node.move);

            visits.emplace_back(move, node.visits);
        }
    }
};

class FileWriter {
    Board                   board;
    vector<MontyFormatMove> moves;

    std::ofstream file;

    void writeU8(const u8 value) { file.write(reinterpret_cast<const char*>(&value), sizeof(u8)); }

    void writeU16(const u16 value) { file.write(reinterpret_cast<const char*>(&value), sizeof(u16)); }

    template<typename T>
    void write(const T& value) {
        file.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

   public:
    explicit FileWriter(const string& filePath) {
        board.reset();

        file = std::ofstream(filePath, std::ios::app | std::ios::binary);

        if (!file.is_open()) {
            cerr << "Error: Could not open the file " << filePath << " for writing." << endl;
            exit(-1);
        }
    }

    void setStartpos(const Board& board) { this->board = board; }

    void addMove(const Searcher& searcher, const Move m) { moves.emplace_back(searcher, m); }

    void writeGame(const usize wdl) {
#ifdef _WIN32
        SetConsoleCtrlHandler(CtrlHandler, TRUE);
        ctrlCPressed = false;
#else
        sigset_t set, oldset;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        pthread_sigmask(SIG_BLOCK, &set, &oldset);
#endif

        write(MontyFormatBoard(board));

        const auto getFile = [](const Square sq, const File fallback) { return sq == NO_SQUARE ? fallback : fileOf(sq); };

        writeU8(getFile(board.castling[castleIndex(WHITE, false)], FILE_A));
        writeU8(getFile(board.castling[castleIndex(WHITE, true)], FILE_H));
        writeU8(getFile(board.castling[castleIndex(BLACK, false)], FILE_A));
        writeU8(getFile(board.castling[castleIndex(BLACK, true)], FILE_H));

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

#ifdef _WIN32
        SetConsoleCtrlHandler(CtrlHandler, FALSE);  // Restore default behavior
        if (ctrlCPressed)
            raise(SIGINT);
#else
        sigset_t pending;
        sigpending(&pending);
        const bool hadSigint = sigismember(&pending, SIGINT);

        pthread_sigmask(SIG_SETMASK, &oldset, nullptr);

        if (hadSigint)
            raise(SIGINT);
#endif

        moves.clear();
    }
};

void makeRandomMove(Board& board) {
    const MoveList moves = Movegen::generateMoves(board);
    assert(moves.length > 0);

    static std::random_device          rd;
    static std::mt19937_64             engine(rd());
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

    Searcher searcher{};

    std::random_device                 rd;
    std::mt19937_64                    engine(rd());
    std::uniform_int_distribution<int> dist(0, 1);
    const auto                         randBool = [&]() { return dist(engine); };

    Stopwatch<std::chrono::milliseconds> stopwatch;
    vector<u64>                          posHistory;
    const SearchParameters               params(posHistory, datagen::ROOT_CPUCT, datagen::CPUCT, datagen::TEMPERATURE, false, false, true);
    const SearchLimits                   limits(stopwatch, 0, nodes, 0, 0);

    usize localPositions = 0;

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
            lk.unlock();
            if (board.isGameOver(posHistory))
                goto mainLoop;
        }

        fileWriter.setStartpos(board);
        posHistory = { board.zobrist };

        bool isFirstMove = true;

        while (!board.isGameOver(posHistory)) {
            Node& root       = searcher.tree.root();
            root             = Node();
            searcher.rootPos = board;
            const Move m     = searcher.search(params, limits);
            assert(!m.isNull());

            if (isFirstMove && std::abs(wdlToCP(root.getScore())) > datagen::MAX_STARTPOS_SCORE)
                goto mainLoop;

            fileWriter.addMove(searcher, m);

            lk.lock();
            board.move(m);
            lk.unlock();
            posHistory.push_back(board.zobrist);

            isFirstMove = false;

            localPositions++;
        }

        usize wdl;
        if (board.isDraw(posHistory) || !board.inCheck())
            wdl = 1;
        else if (board.stm == WHITE)
            wdl = 0;
        else
            wdl = 2;

        fileWriter.writeGame(wdl);
        posHistory.clear();

        if (localPositions >= datagen::POSITION_COUNT_BUFFER) {
            positions.fetch_add(localPositions, std::memory_order_relaxed);
            localPositions = 0;
        }
    }
}

void datagen::run(const string& params) {
    if (params.empty())
        return;

    if (!IS_LITTLE_ENDIAN) {
        cout << "ERROR: DATAGEN REQUIRES A LITTLE ENDIAN SYSTEM." << endl;
        std::abort();
    }

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

    // Order in which to fill the changing text
    constexpr std::string_view finishedText = "Chaos Datagen Complete!";

    // List of allowed characters
    constexpr std::string_view allowedChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{}\\|;':\",.<>?/`~";

    // Array storing the order in which to fill
    array<uint16_t, finishedText.size()> textFillOrder = [&] {
        array<uint16_t, finishedText.size()> a{};
        for (uint16_t i = 0; i < a.size(); ++i)
            a[i] = static_cast<uint16_t>(i + 1);
        std::mt19937 rng{ static_cast<uint32_t>(std::time(nullptr)) };
        std::shuffle(a.begin(), a.end(), rng);
        return a;
    }();

    std::mt19937                          rng{ static_cast<uint32_t>(std::time(nullptr) ^ 0x9E3779B9) };
    std::uniform_int_distribution<size_t> randIndex(0, allowedChars.size() - 1);

    // Returns the string
    static auto getFrame = [&](double t) -> string {
        t = std::clamp(t, 0.0, 1.0);

        const double N = static_cast<double>(finishedText.size());
        string       out;
        out.resize(finishedText.size());

        for (size_t i = 0; i < finishedText.size(); ++i) {
            const double threshold = static_cast<double>(textFillOrder[i]) / N;
            const bool   solved    = (t >= threshold);

            if (solved)
                out[i] = finishedText[i];
            else
                out[i] = allowedChars[randIndex(rng)];
        }
        return out;
    };

    for (auto& p : positions)
        p.store(0, std::memory_order_relaxed);
    for (auto& b : boards)
        b.reset();

    for (usize i = 0; i < threadCount; i++)
        threads.emplace_back(runThread, nodes, std::ref(boards[i]), std::ref(boardMutexes[i]), std::ref(positions[i]), std::cref(stop));

    cursor::hide();

    u64                  totalPositions = 0;
    RollingWindow<float> pastNPS(100);

    cursor::clearAll();

    while (totalPositions < numPositions) {
        boardMutexes[0].lock();
        Board board = boards[0];
        boardMutexes[0].unlock();

        if (board.stm == BLACK)
            continue;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        totalPositions = 0;
        for (usize i = 0; i < threadCount; i++)
            totalPositions += positions[i].load();

        totalPositions = std::max<u64>(1, totalPositions);

        pastNPS.push(static_cast<double>(totalPositions) * 1000 / time.elapsed());
        const float nps = std::accumulate(pastNPS.begin(), pastNPS.end(), 0.0f) / pastNPS.size();

        std::ostringstream ss{};

        const double progress = static_cast<double>(totalPositions) / numPositions;

        cursor::home(ss);
        ss << "************ " << getFrame(progress) << " ************" << "\n";
        ss << "\n";
        ss << "*** Parameters ***" << "\n";
        ss << "Threads:   " << threadCount << "\n";
        ss << "Positions: " << suffixNum(numPositions) << "\n";
        ss << "Nodes:     " << nodes << "\n";
        ss << "\n";
        ss << "\n";
        ss << "\n";
        for (usize i = 0; i < 4; i++) {
            cursor::down(ss);
            cursor::clear(ss);
        }
        for (usize i = 0; i < 4; i++)
            cursor::up(ss);
        ss << board << "\n";
        ss << "\n";
        ss << "\n";
        progressBar(50, progress, Colors::GREEN, ss);
        ss << "\n";
        cursor::clear(ss);
        ss << Colors::GREY << "Positions:            " << Colors::RESET << suffixNum(totalPositions) << "\n";
        cursor::clear(ss);
        ss << Colors::GREY << "Positions per second: " << Colors::RESET << suffixNum(nps) << "\n";
        ss << "\n";
        cursor::clear(ss);
        ss << Colors::GREY << "Time elapsed:             " << Colors::RESET << formatTime(time.elapsed()) << "\n";
        cursor::clear(ss);
        ss << Colors::GREY << "Estimated time remaining: " << Colors::RESET << formatTime(static_cast<double>(numPositions - std::min(totalPositions, numPositions)) / nps * 1000) << "\n";

        cout << ss.str() << std::flush;

        cout.flush();
    }

    stop.store(true);
    for (std::thread& thread : threads)
        if (thread.joinable())
            thread.join();

    cout << "\n\n";

    std::string dummy;
    std::getline(std::cin, dummy);

    cursor::home();
    cursor::clearAll();
    cursor::show();
    slowPrint("GREETINGS PROFESSOR FALKEN\n\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    slowPrint("HELLO\n\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    slowPrint("A STRANGE GAME.\n");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    slowPrint("THE ONLY WINNING MOVE IS\nNOT TO PLAY.\n\n");
    std::this_thread::sleep_for(std::chrono::seconds(4));
    slowPrint("HOW ABOUT A NICE GAME OF CHESS?\n");
    std::this_thread::sleep_for(std::chrono::seconds(2));
}


void datagen::genFens(const string& params) {
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

    const auto isValidPosition = [](const Board& board) {
        const Stopwatch<std::chrono::milliseconds> stopwatch;
        const vector<u64>                          posHistory;
        const SearchParameters                     params(posHistory, datagen::ROOT_CPUCT, datagen::CPUCT, datagen::TEMPERATURE, false, false, true);
        const SearchLimits                         limits(stopwatch, 0, datagen::GENFENS_VERIF_NODES, 0, 0);

        static Searcher searcher{};
        searcher.rootPos                            = board;
        searcher.tree.root() = Node();
        searcher.search(params, limits);

        return std::abs(wdlToCP(searcher.tree.root().getScore())) <= MAX_STARTPOS_SCORE;
    };

    const u64 numFens = std::stoull(getValueFollowing("genfens", 1));
    const u64 seed    = std::stoull(getValueFollowing("seed", std::time(nullptr)));

    std::mt19937                       eng(seed);
    std::uniform_int_distribution<int> dist(0, 1);
    auto                               randBool = [&]() { return dist(eng); };


    const vector<u64> posHistory;
    u64               fens = 0;
    while (fens < numFens) {
startLoop:
        Board board{};
        board.reset();
        const usize randomMoves = datagen::RAND_MOVES + randBool();
        for (usize i = 0; i < randomMoves; i++) {
            MoveList                           moves = Movegen::generateMoves(board);
            std::uniform_int_distribution<int> dist(0, moves.length - 1);
            board.move(moves.moves[dist(eng)]);
            if (board.isGameOver(posHistory))
                goto startLoop;
        }

        if (!isValidPosition(board))
            continue;

        cout << "info string genfens " << board.fen() << endl;
        fens++;
    }

    cout << "info string Generated " << fens << " positions" << endl;
}