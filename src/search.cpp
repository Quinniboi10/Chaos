#include "searcher.h"
#include "movegen.h"
#include "policy.h"
#include "eval.h"
#include "globals.h"

#include <cmath>
#include <functional>

bool switchHalves = false;

// Return if a node is an unexplored or terminal node in the current half
bool isLeaf(const Node& node, const u8 currentHalf) { return node.numChildren == 0 || node.firstChild.load().half() != currentHalf; }

// Return if a node is threefold
bool isThreefold(const vector<u64>& posHistory) {
    return false;
};

// Return the parent portion of the PUCT score
float parentPuct(const Node& parent, const float cpuct) { return cpuct * std::sqrt(static_cast<float>(parent.visits + 1)); }

// Return the PUCT score of a node
float puct(const float parentScore, const float parentQ, const Node& child) {
    // V + C * P * (N.max(1).sqrt() / (n + 1))
    // V = Q = total score / visits
    // C = CPUCT
    // P = move policy score
    // N = parent visits
    // n = child visits
    const u64 v = child.visits.load();
    return (v > 0 ? -child.getScore(v) : parentQ) + child.policy * parentScore / (v + 1);
};

// Expand a node
void expandNode(Tree& nodes, const Board& board, Node& node, u64& currentIndex, const u8& currentHalf, const SearchParameters& params) {
    MoveList moves = Movegen::generateMoves(board);

    // Mates aren't handled until the simulation/rollout stage
    if (moves.length == 0)
        return;

    if (currentIndex + moves.length >= nodes.nodes[currentHalf].size()) {
        switchHalves = true;
        return;
    }

    node.firstChild  = { currentIndex, currentHalf };
    node.numChildren = moves.length;

    Node* child = &nodes[{ currentIndex, currentHalf }];

    for (usize i = 0; i < moves.length; i++) {
        child[i].totalScore  = 0;
        child[i].visits      = 0;
        child[i].move        = moves[i];
        child[i].state       = ONGOING;
        child[i].numChildren = 0;
    }

    currentIndex += moves.length;

    fillPolicy(board, nodes, node, params.temp);
}

float computeCpuct(const Node& node, const SearchParameters& params) {
    float cpuct = params.cpuct;
    cpuct *= 1.0f + std::log((node.visits.load() + CPUCT_VISIT_SCALE) / 8192);
    return cpuct;
}

// Find the best child node from a parent
Node& findBestChild(Tree& nodes, const Node& node, const SearchParameters& params) {
    const float cpuct = computeCpuct(node, params);
    const float parentScore = parentPuct(node, cpuct);
    const float parentQ = node.getScore();
    Node*       bestChild   = &nodes[node.firstChild];
    Node*       child       = bestChild;
    float       bestScore   = puct(parentScore, parentQ, *child);
    for (usize idx = 1; idx < node.numChildren; idx++) {
        const float score = puct(parentScore, parentQ, child[idx]);
        if (score > bestScore) {
            bestScore = score;
            bestChild = child + idx;
        }
    }

    return *bestChild;
};

// Evaluate node
float simulate(const Board& board, const vector<u64>& posHistory, Node& node) {
    assert(node.state == ONGOING);

    if (board.isDraw() || isThreefold(posHistory) || (node.numChildren == 0 && !board.inCheck()))
        node.state = DRAW;
    else if (node.numChildren == 0)
        node.state = LOSS;
    if (node.state != ONGOING)
        return node.getScore();
    return cpToWDL(evaluate(board));
};

// Find the PV (best Q) move for a node
Move findPvMove(const Tree& nodes, const Node& node) {
    const Node* child = &nodes[node.firstChild.load()];

    float bestScore = -child->getScore();
    Move  bestMove  = child->move;
    for (usize idx = 1; idx < node.numChildren; idx++) {
        const float score = -child[idx].getScore();
        if (score > bestScore) {
            bestScore = score;
            bestMove  = child[idx].move;
        }
    }

    return bestMove;
}

// Search the tree for the PV line
// This function will search across halves
MoveList findPV(const Tree& nodes, const u8 currentHalf, const Node* initialNode = nullptr) {
    MoveList pv{};

    const Node* node;
    if (initialNode == nullptr)
        node = &nodes[{ 0, currentHalf }];
    else {
        node = initialNode;
        pv.add(node->move);
    }

    while (node->numChildren != 0) {
        const NodeIndex startIdx  = node->firstChild.load();
        const Node*     child     = &nodes[startIdx];
        const Node*     bestChild = child;
        float           bestScore = -child->getScore();
        for (usize idx = 1; idx < node->numChildren; idx++) {
            const float score = -child[idx].getScore();
            if (score > bestScore) {
                bestScore = score;
                bestChild = child + idx;
            }
        }

        pv.add(bestChild->move);
        node = bestChild;
    }

    return pv;
}

// Copy children from the constant half to the current one
void copyChildren(Tree& nodes, Node& node, u64& currentIndex, const u8 currentHalf) {
    const u8 numChildren = node.numChildren;
    if (currentIndex + numChildren > nodes.nodes[currentHalf].size()) {
        switchHalves = true;
        return;
    }

    const Node* oldChild = &nodes[node.firstChild.load()];
    Node*       newChild = &nodes[{ currentIndex, currentHalf }];

    for (usize i = 0; i < numChildren; i++)
        newChild[i] = oldChild[i];

    node.firstChild.store({ currentIndex, currentHalf });

    currentIndex += node.numChildren;
};

// Remove all references to the other half
void removeRefs(Tree& nodes, Node& node, const u8 currentHalf) {
    const NodeIndex startIdx = node.firstChild.load();

    if (startIdx.half() == currentHalf) {
        Node* child = &nodes[startIdx];
        for (usize idx = 0; idx < +node.numChildren; idx++)
            removeRefs(nodes, child[idx], currentHalf);
    }
    else
        node.numChildren = 0;
}

float searchNode(
  Tree& nodes, u64& cumulativeDepth, usize& seldepth, u64& currentIndex, const u8 currentHalf, vector<u64>& posHistory, const Board& board, Node& node, const SearchParameters& params, usize ply) {
    // Check for an early return
    if (node.state != ONGOING)
        return node.getScore();

    float score;

actionBranch:
    // Selection
    if (!isLeaf(node, currentHalf)) {
        Node& bestChild = findBestChild(nodes, node, params);
        Board newBoard  = board;
        newBoard.move(bestChild.move);

        posHistory.push_back(newBoard.zobrist);
        score = -searchNode(nodes, cumulativeDepth, seldepth, currentIndex, currentHalf, posHistory, newBoard, bestChild, params, ply + 1);
        posHistory.pop_back();
    }
    // Expansion + simulation
    else {
        if (node.firstChild.load().half() != currentHalf && node.numChildren > 0) {
            if (switchHalves)
                return 0;
            copyChildren(nodes, node, currentIndex, currentHalf);
            goto actionBranch;
        }
        else if (node.visits == 0)
            score = cpToWDL(evaluate(board));
        else {
            expandNode(nodes, board, node, currentIndex, currentHalf, params);
            score = simulate(board, posHistory, node);
        }
    }

    if (switchHalves)
        return 0;

    // Backprop
    node.totalScore.getUnderlying().fetch_add(score, std::memory_order_relaxed);
    node.visits.getUnderlying().fetch_add(1, std::memory_order_relaxed);

    cumulativeDepth++;
    seldepth = std::max(seldepth, ply);

    return score;
};

Move Searcher::search(const SearchParameters params, const SearchLimits limits) {
    // Reset searcher
    this->nodeCount     = 0;
    this->stopSearching = false;

    usize multiPV = std::min(::multiPV, Movegen::generateMoves(rootPos).length);

    u64 currentIndex = 1;

    u64 halfChanges = 0;

    auto& iterations      = this->nodeCount;
    u64   cumulativeDepth = 0;

    usize seldepth = 0;

    // Time management
    i64 timeToSpend = limits.time / 20 + limits.inc / 2;

    if (timeToSpend)
        timeToSpend -= MOVE_OVERHEAD;

    // Returns true if search has met a limit
    const auto stopSearching = [&]() {
        const u64 nodeCount = this->nodeCount.load();
        if (nodeCount % 1024 == 0 && (this->stopSearching.load() || (timeToSpend != 0 && static_cast<i64>(limits.commandTime.elapsed()) >= timeToSpend)))
            return true;
        return (limits.nodes > 0 && nodeCount >= limits.nodes) || (limits.depth > 0 && cumulativeDepth / iterations >= limits.depth);
    };

    // Positions from root to the leaf
    vector<u64> posHistory;

    // Intervals to report on
    Stopwatch<std::chrono::milliseconds> stopwatch;
    RollingWindow<std::pair<u64, Move>>  bestMoves(std::max<int>(getTerminalRows() - 28 - multiPV, 1));
    usize                                lastDepth    = 0;
    usize                                lastSeldepth = 0;
    Move                                 lastMove     = Move::null();

    const auto printUCI = [&]() {
        vector<Node> children;
        const Node root = nodes[{ 0, currentHalf }];
        const Node* child = &nodes[root.firstChild];
        const Node* end = child + root.numChildren;
        for (const Node* idx = child; idx != end; idx++)
            children.push_back(*idx);

        std::ranges::sort(children, std::greater{}, [](const Node& n) { return -n.getScore(); });

        const u64 time = limits.commandTime.elapsed();

        for (usize i = 1; i <= multiPV; i++) {
            const Node& n = children[i - 1];
            const MoveList pv = findPV(nodes, currentHalf, &n);

            cout << "info depth " << cumulativeDepth / iterations;
            cout << " seldepth " << seldepth;
            cout << " time " << time;
            cout << " nodes " << nodeCount.load();
            if (time > 0)
                cout << " nps " << nodeCount.load() * 1000 / time;
            cout << " hashfull " << currentIndex * 1000 / nodes.nodes[currentHalf].size();
            cout << " hswitches " << halfChanges;
            cout << " multipv " << i;
            if (n.state == ONGOING || n.state == DRAW)
                cout << " score cp " << wdlToCP(-n.getScore());
            else
                cout << " score mate " << (pv.length + 1) / 2 * (n.state == WIN ? 1 : -1);
            cout << " pv";
            for (Move m : pv)
                cout << " " << m;
            cout << endl;
        }
    };

    const auto prettyPrint = [&]() {
        const MoveList pv = findPV(nodes, currentHalf);
        cursor::goTo(1, 14);

        cout << Colors::GREY << " Half Usage:   " << Colors::WHITE;
        coloredProgBar(50, static_cast<float>(currentIndex) / nodes.nodes[currentHalf].size());
        cout << "  \n";
        cout << Colors::GREY << " Half Changes: " << Colors::WHITE << formatNum(halfChanges) << "\n\n";

        cout << Colors::GREY << " Nodes:            " << Colors::WHITE << suffixNum(nodeCount.load()) << "   \n";
        cout << Colors::GREY << " Time:             " << Colors::WHITE << formatTime(limits.commandTime.elapsed() + 1) << "   \n";
        cout << Colors::GREY << " Nodes per second: " << Colors::WHITE << suffixNum(nodeCount.load() * 1000 / (limits.commandTime.elapsed() + 1)) << "   \n";
        cout << "\n";

        cursor::clear();
        cout << Colors::GREY << " Depth:     " << Colors::WHITE << cumulativeDepth / iterations << "\n";
        cout << Colors::GREY << " Max depth: " << Colors::WHITE << seldepth << "\n\n";

        cursor::clear();
        const float rootWdl = nodes[{ 0, currentHalf }].getScore();
        cout << Colors::GREY << " Score:     ";
        printColoredScore(rootWdl);
        cout << "\n";
        cursor::clear();
        cout << Colors::GREY << " PV line: ";
        printPV(pv);
        cout << "\n";
        cout << "\n";
        cout << " Best move history:" << "\n";
        for (const auto& m : bestMoves) {
            cout << "    " << Colors::GREY << formatTime(m.first) << Colors::WHITE << " -> " << m.second << "     \n";
        }


        cout << Colors::RESET;
        cout.flush();
    };

    // Expand root
    expandNode(nodes, rootPos, nodes[{ 0, currentHalf }], currentIndex, currentHalf, params);

    // Prepare for pretty printing
    if (params.doReporting && !params.doUci) {
        cursor::clearAll();
        cursor::hide();
        cursor::home();

        cout << rootPos << "\n";
        cout << Colors::GREY << " Tree Size:    " << Colors::WHITE << (nodes.nodes[0].size() + nodes.nodes[1].size()) * sizeof(Node) / 1024 / 1024 << "MB\n";
    }

    // Main search loop
    do {
        // Reset zobrist history
        posHistory = params.positionHistory;

        searchNode(nodes, cumulativeDepth, seldepth, currentIndex, currentHalf, posHistory, rootPos, nodes[{ 0, currentHalf }], params, 0);

        // Switch halves
        if (switchHalves) {
            switchHalves = false;
            nodes[{ 0, static_cast<u8>(currentHalf ^ 1) }] = nodes[{ 0, currentHalf }];
            removeRefs(nodes, nodes[{ 0, currentHalf }], currentHalf);
            currentIndex = 1;
            currentHalf ^= 1;
            copyChildren(nodes, nodes[{ 0, currentHalf }], currentIndex, currentHalf);
            halfChanges++;
        }
        iterations.getUnderlying().fetch_add(1, std::memory_order_relaxed);

        // Check if UCI should be printed
        if (params.doReporting) {
            const Move bestMove = findPvMove(nodes, nodes[{ 0, currentHalf }]);
            if (params.doUci && (lastDepth != cumulativeDepth / iterations || lastSeldepth != seldepth || bestMove != lastMove || stopwatch.elapsed() >= UCI_REPORTING_FREQUENCY)) {
                const Move bestMove = findPvMove(nodes, nodes[{ 0, currentHalf }]);
                printUCI();

                lastDepth    = cumulativeDepth / iterations;
                lastSeldepth = seldepth;
                lastMove     = bestMove;
                stopwatch.reset();
            }
            else if (!params.doUci && (iterations == 2 || stopwatch.elapsed() >= 40)) {
                const Move bestMove = findPvMove(nodes, nodes[{ 0, currentHalf }]);
                if (bestMove != lastMove)
                    bestMoves.push({ limits.commandTime.elapsed(), bestMove });
                prettyPrint();

                lastDepth    = cumulativeDepth / iterations;
                lastSeldepth = seldepth;
                lastMove     = bestMove;
                stopwatch.reset();
            }
        }
    } while (!stopSearching());

    const Move bestMove = findPvMove(nodes, nodes[{ 0, currentHalf }]);

    if (params.doReporting) {
        if (params.doUci) {
            printUCI();
            cout << "bestmove " << bestMove << endl;
        }
        else {
            prettyPrint();
            cout << "\n\nBest move: " << Colors::BRIGHT_BLUE << bestMove << Colors::RESET << endl;
            cursor::show();
        }
    }

    this->stopSearching = true;

    return bestMove;
}

Move Searcher::searchPolicy(const SearchParameters params) {
    fillRootPolicy(rootPos);

    const Node root = nodes[{ 0, currentHalf }];
    const Node* child = &nodes[root.firstChild];
    const Node* end = child + root.numChildren;

    const Node* bestNode = child;

    for (const Node* idx = child + 1; idx != end; idx++)
        if (idx->policy > bestNode->policy)
            bestNode = idx;

    if (params.doReporting)
        cout << "bestmove " << bestNode->move << endl;

    return bestNode->move;
}

Move Searcher::searchValue(const SearchParameters params) {
    struct MoveEvalPair {
        Move move;
        i32 eval;

        MoveEvalPair() = default;
        MoveEvalPair(const Move move, const i32 eval) : move(move), eval(eval) {};
    };

    vector<MoveEvalPair> moves;

    const MoveList legalMoves = Movegen::generateMoves(rootPos);

    for (const Move m : legalMoves) {
        Board b = rootPos;
        b.move(m);
        const i32 score = evaluate(b);
        moves.emplace_back(m, score);
    }

    const Move best = std::ranges::max_element(moves, {},  [](const MoveEvalPair& m) { return m.eval; })->move;

    if (params.doReporting)
        cout << "bestmove " << best << endl;

    return best;
}
