#include "searcher.h"
#include "movegen.h"
#include "policy.h"
#include "eval.h"

#include <cmath>
#include <numeric>
#include <functional>

Move Searcher::search(const SearchParameters params, const SearchLimits limits) {
    // Reset worker
    this->nodeCount = 0;

    u64 currentIndex = 1;

    auto& iterations = this->nodeCount;
    u64 cumulativeDepth = 0;

    usize seldepth = 0;

    // Time management
    i64 timeToSpend = limits.time / 20 + limits.inc / 2;

    if (timeToSpend)
        timeToSpend -= MOVE_OVERHEAD;

    // Returns true if search has met a limit
    const auto stopSearching = [&]() {
        return (limits.nodes > 0 && nodeCount.load() >= limits.nodes)
            || (limits.depth > 0 && cumulativeDepth / iterations >= limits.depth)
            || (timeToSpend != 0 && static_cast<i64>(limits.commandTime.elapsed()) >= timeToSpend);
    };

    // Positions from root to the leaf
    vector<u64> posHistory;

    // ****** Define lambdas for node operations ******

    // Return if a node is an unexplored or terminal node
    const auto isLeaf = [this](const Node& node) { return node.numChildren == 0 || node.firstChild.load().half() != currentHalf; };

    // Return if a node is threefold (or twofold if all positions are past root)
    const auto isThreefold = [&]() {
        usize reps = 0;
        const u64 current = posHistory.back();

        for (const u64 hash : posHistory)
            if (hash == current)
                if (++reps == 3)
                    return true;

        return false;
    };

    // PUCT formula
    const auto puct = [&](const Node& parent, const Node& child) {
        // V + C * P * (N.max(1).sqrt() / (n + 1))
        // V = Q = total score / visits
        // C = CPUCT
        // P = move policy score
        // N = parent visits
        // n = child visits
        return -child.getScore() + params.cpuct * child.policy * (std::sqrt(std::max<u64>(parent.visits, 1)) / (child.visits + 1));
    };

    // Expand a node
    const auto expandNode = [&](const Board& board, Node& node) {
        MoveList moves = Movegen::generateMoves(board);

        // Mates aren't handled until the simulation/rollout stage
        if (moves.length == 0)
            return;

        node.firstChild  = { currentIndex, currentHalf };
        node.numChildren = moves.length;

        for (const Move& move : moves) {
            Node& child = nodes[{ currentIndex++, currentHalf }];
            child.totalScore = 0;
            child.visits = 0;
            child.firstChild = { 0, 0 };
            child.state = ONGOING;
            child.move = move;
            child.numChildren = 0;
        }

        fillPolicy(board, nodes, node, params.temp);
    };

    // Find the best child node from a parent
    const auto findBestChild = [&](const Node& node) -> Node& {
        const u8 half = node.firstChild.load().half();
        double bestScore = puct(node, nodes[node.firstChild]);
        Node* bestChild = &nodes[node.firstChild];
        for (usize idx = node.firstChild.load().index() + 1; idx < node.firstChild.load().index() + node.numChildren; idx++) {
            const double score = puct(node, nodes[{ idx, half }]);
            if (score > bestScore) {
                bestScore = score;
                bestChild = &nodes[{ idx, half }];
            }
        }

        return *bestChild;
    };

    // Search the tree for the PV line
    const auto findPV = [&]() {
        MoveList pv{};

        Node* node = &nodes[{ 0, currentHalf }];

        while (!isLeaf(*node)) {
            double bestScore = -nodes[node->firstChild].getScore();
            usize bestIdx = node->firstChild.load().index();
            for (usize idx = node->firstChild.load().index() + 1; idx < node->firstChild.load().index() + node->numChildren; idx++) {
                const double score = -nodes[{ idx, currentHalf}].getScore();
                if (score > bestScore) {
                    bestScore = score;
                    bestIdx = idx;
                }
            }

            node = &nodes[{ bestIdx, currentHalf }];
            pv.add(node->move);
        }

        return pv;
    };

    // Evaluate node
    const auto simulate = [&](const Board& board, Node& node) {
        assert(node.state == ONGOING);

        if (board.isDraw() || isThreefold() || (node.numChildren == 0 && !board.inCheck()))
            node.state = DRAW;
        else if (node.numChildren == 0)
            node.state = LOSS;
        if (node.state != ONGOING)
            return node.getScore();
        return cpToWDL(evaluate(board));
    };

    const auto copyChildren = [&](Node& node) {
        const NodeIndex oldIdx = node.firstChild.load();

        for (usize i = 0; i < node.numChildren; i++) {
            Node& child = nodes[{ currentIndex + i, currentHalf }];
            child = nodes[{ oldIdx.index() + i, oldIdx.half() }];
        }

        node.firstChild.store({ currentIndex, currentHalf });

        currentIndex += node.numChildren;
    };

    // Remove all references to the other half
    const auto removeReferences = [&]() {
        const std::function<void(Node&)> removeRefs = [&](Node& node) {
            if (node.firstChild.load().half() == currentHalf) {
                for (usize idx = node.firstChild.load().index(); idx < node.firstChild.load().index() + node.numChildren; idx++)
                    removeRefs(nodes[{ idx, currentHalf }]);
            }
            else {
                node.numChildren = 0;
                node.firstChild = { 0, currentHalf };
            }
        };

        removeRefs(nodes[{ 0, currentHalf }]);
    };


    const std::function<double(const Board&, Node&, usize)> searchNode = [&](const Board& board, Node& node, const usize ply) {
        // Check for an early return
        if (node.state != ONGOING)
            return node.getScore();

        double score;

        // Selection
        if (!isLeaf(node)) {
            Node& bestChild = findBestChild(node);
            Board newBoard = board;
            newBoard.move(bestChild.move);

            posHistory.push_back(newBoard.zobrist);
            score = -searchNode(newBoard, bestChild, ply + 1);
            posHistory.pop_back();
        }
        // Expansion + simulation
        else {
            if (node.firstChild.load().half() != currentHalf && node.numChildren > 0) {
                copyChildren(node);
                score = searchNode(board, node, ply);
            }
            else if (node.visits == 0)
                score = cpToWDL(evaluate(board));
            else {
                expandNode(board, node);
                score = simulate(board, node);
            }
        }

        // Backprop
        node.totalScore += score;
        node.visits++;

        cumulativeDepth++;
        seldepth = std::max(seldepth, ply);

        return score;
    };

    // Intervals to report on
    Stopwatch<std::chrono::milliseconds> stopwatch;
    vector<std::pair<u64, Move>> bestMoves;
    usize lastDepth = 0;
    usize lastSeldepth = 0;
    Move lastMove = Move::null();

    const auto printUCI = [&](MoveList& pv) {
        cout << "info depth " << cumulativeDepth / iterations;
        cout << " seldepth " << seldepth;
        cout << " time " << limits.commandTime.elapsed();
        cout << " nodes " << nodeCount.load();
        if (limits.commandTime.elapsed() > 0)
            cout << " nps " << nodeCount.load() * 1000 / limits.commandTime.elapsed();
        if (nodes[{ 0, currentHalf }].state == ONGOING || nodes[{ 0, currentHalf }].state == DRAW)
            cout << " score cp " << wdlToCP(nodes[{ 0, currentHalf }].getScore());
        else
            cout << " score mate " << (pv.length + 1) / 2 * (nodes[{ 0, currentHalf }].state == WIN ? 1 : -1);
        cout << " pv";
        for (Move m : pv)
            cout << " " << m;
        cout << endl;
    };

    const auto prettyPrint = [&](const MoveList& pv) {
        cursor::goTo(1, 14);

        cursor::clear();
        cout << Colors::GREY << "Tree Usage: " << Colors::WHITE;
        coloredProgBar(40, static_cast<float>(currentIndex) / nodes.nodes[currentHalf].size());
        cout << "\n\n";

        cout << Colors::GREY << "Nodes:            " << Colors::WHITE << suffixNum(nodeCount.load()) << "\n";
        cursor::clear();
        cout << Colors::GREY << "Time:             " << Colors::WHITE << formatTime(limits.commandTime.elapsed() + 1) << "\n";
        cursor::clear();
        cout << Colors::GREY << "Nodes per second: " << Colors::WHITE << nodeCount.load() * 1000 / (limits.commandTime.elapsed() + 1) << "\n";
        cout << "\n";

        cursor::clear();
        cout << Colors::GREY << "Depth:     " << Colors::WHITE << cumulativeDepth / iterations << "\n";
        cout << Colors::GREY << "Max depth: " << Colors::WHITE << seldepth << "\n\n";

        cursor::clear();
        const double rootWdl = nodes[{ 0, currentHalf }].getScore();
        cout << Colors::GREY << "Score:     ";
        printColoredScore(rootWdl);
        cout << "\n";
        cursor::clear();
        cout << Colors::GREY << "Best move: " << Colors::WHITE << pv[0] << "\n";
        cout << "\n\n";
        cout << "Best move history:" << "\n";
        for (const auto& m : bestMoves)
            cout << "    " << Colors::GREY << formatTime(m.first) << Colors::WHITE << " -> " << m.second << "\n";


        cout << Colors::RESET;
        cout.flush();
    };

    // Expand root
    expandNode(rootPos, nodes[{ 0, currentHalf }]);

    // Prepare for pretty printing
    if (params.doReporting && !params.doUci) {
        cursor::clearAll();
        cursor::hide();
        cursor::home();

        cout << rootPos << "\n";
        cout << Colors::GREY << "Tree Size:  " << Colors::WHITE << (nodes.nodes[0].size() + nodes.nodes[1].size()) * sizeof(Node) / 1024 / 1024 << "MB\n";
    }

    // Main search loop
    do {
        // Reset zobrist history
        posHistory = params.positionHistory;

        searchNode(rootPos, nodes[{ 0, currentHalf }], 0);

        // Switch halves
        if (currentIndex >= nodes.nodes[0].size() - 256) {
            nodes[{ 0, static_cast<u8>(currentHalf ^ 1) }] = nodes[{ 0, currentHalf }];
            removeReferences();
            currentIndex = 1;
            currentHalf ^= 1;
            copyChildren(nodes[{ 0, currentHalf }]);
        }

        iterations++;

        // Check if UCI should be printed
        if (params.doReporting) {
            MoveList pv = findPV();
            if (params.doUci && (lastDepth != cumulativeDepth / iterations || lastSeldepth != seldepth || pv[0] != lastMove || stopwatch.elapsed() >= UCI_REPORTING_FREQUENCY)) {
                printUCI(pv);

                lastDepth = cumulativeDepth / iterations;
                lastSeldepth = seldepth;
                lastMove = pv[0];
                stopwatch.reset();
            }
            else if (!params.doUci && (iterations == 2 || stopwatch.elapsed() >= 40)) {
                if (pv[0] != lastMove)
                    bestMoves.emplace_back(limits.commandTime.elapsed(), pv[0]);
                prettyPrint(pv);

                lastDepth = cumulativeDepth / iterations;
                lastSeldepth = seldepth;
                lastMove = pv[0];
                stopwatch.reset();
            }
        }
    } while (!stopSearching());

    MoveList pv = findPV();

    if (params.doReporting) {
        if (params.doUci) {
            printUCI(pv);
            cout << "bestmove " << pv[0] << endl;
        }
        else {
            prettyPrint(pv);
            cout << "\n\nBest move: " << Colors::BRIGHT_BLUE << pv[0] << Colors::RESET << endl;
            cursor::show();
        }
    }

    return pv[0];
}