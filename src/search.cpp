#include "searcher.h"
#include "globals.h"
#include "movegen.h"
#include "policy.h"
#include "eval.h"

// This file aims to implement the 4 main steps to MCTS search
// 1 - SELECTION  - Select a node to expand
// 2 - EXPANSION  - Expand the node and add its children to the tree
// 3 - SIMULATION - Traditionally a random playout, use the
// 4 - BACKPROP   - Propagate the score of the selected node all the way back to root


// ======================== HELPERS ========================
// Get the state of a position
RawGameState stateOf(const Board& board, const vector<u64>& posHistory) {
    if (board.isDraw(posHistory))
        return DRAW;
    if (Movegen::generateMoves(board).length == 0) {
        if (board.inCheck())
            return LOSS;
        return DRAW;
    }
    return ONGOING;
}

float getAdjustedScore(const Node& node) {
    const RawGameState s = node.state.load().state();

    if (s == DRAW)
        return 0;
    if (s == WIN)
        return 1;
    if (s == LOSS)
        return -1;
    if (node.visits.load() > 0)
        return node.getScore();
    return 0;
}

// Find the PV (best Q) move for a node
Move findPvMove(const Tree& tree, const Node& node) {
    const Node* child = &tree[node.firstChild.load()];

    float bestScore = -getAdjustedScore(*child);
    Move  bestMove  = child->move;
    for (usize idx = 1; idx < node.numChildren; idx++) {
        const float score = -getAdjustedScore(child[idx]);
        if (score > bestScore) {
            bestScore = score;
            bestMove  = child[idx].move;
        }
    }

    return bestMove;
}

// Search the tree for the PV line
// This function will search across halves
MoveList findPV(const Tree& tree, const Node* initialNode = nullptr) {
    MoveList pv{};

    const Node* node;
    if (initialNode == nullptr)
        node = &tree.root();
    else {
        node = initialNode;
        pv.add(node->move);
    }

    while (node->numChildren != 0) {
        const NodeIndex startIdx  = node->firstChild.load();
        const Node*     child     = &tree[startIdx];
        const Node*     bestChild = child;
        float           bestScore = -getAdjustedScore(*child);
        for (usize idx = 1; idx < node->numChildren; idx++) {
            const float score = -getAdjustedScore(child[idx]);
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


// ======================== SELECTION ========================
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
    return (v > 0 ? -child.getScore() : parentQ) + child.policy * parentScore / (v + 1);
}

float computeCpuct(const Node& node, const SearchParameters& params) {
    float cpuct = node.move.load().isNull() ? params.rootCpuct : params.cpuct;
    cpuct *= 1.0f + std::log((node.visits.load() + CPUCT_VISIT_SCALE) / 8192);
    return cpuct;
}

// Find the best child node from a parent
Node& findBestChild(Tree& tree, const Node& node, const SearchParameters& params) {
    const float cpuct       = computeCpuct(node, params);
    const float parentScore = parentPuct(node, cpuct);
    const float parentQ     = node.getScore();
    Node*       bestChild   = &tree[node.firstChild];
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
}


// ======================== EXPANSION ========================
// Expand a node, adding the new nodes to the tree
void expandNode(Tree& tree, const Board& board, Node& node, u64& currentIndex, const SearchParameters& params) {
    MoveList moves = Movegen::generateMoves(board);

    // Mates aren't handled until the simulation/rollout stage
    if (moves.length == 0)
        return;

    if (currentIndex + moves.length >= tree.activeTree().size()) {
        tree.switchHalves = true;
        return;
    }

    node.firstChild  = { currentIndex, tree.activeHalf() };
    node.numChildren = moves.length;

    Node* child = &tree.activeTree()[currentIndex];

    for (usize i = 0; i < moves.length; i++) {
        child[i].totalScore  = 0;
        child[i].visits      = 0;
        child[i].move        = moves[i];
        child[i].state       = ONGOING;
        child[i].numChildren = 0;
    }

    fillPolicy(board, tree, node, currentIndex == 1 ? params.rootPolicyTemp : params.policyTemp);

    currentIndex += moves.length;
}

// Copy children from the inactive half to the current one
void copyChildren(Tree& tree, Node& node, u64& currentIndex) {
    const u8 numChildren = node.numChildren;

    if (currentIndex + numChildren > tree.activeTree().size()) {
        tree.switchHalves = true;
        return;
    }

    const Node* oldChild = &tree[node.firstChild.load()];
    Node*       newChild = &tree.activeTree()[currentIndex];

    for (usize i = 0; i < numChildren; i++)
        newChild[i] = oldChild[i];

    node.firstChild.store({ currentIndex, tree.activeHalf() });

    currentIndex += node.numChildren;
}


// ======================== SIMULATION ========================
// Evaluate a position
float evaluateNode(const Node& node, const Board& board) {
    const RawGameState s = node.state.load().state();

    if (s == DRAW)
        return 0;
    if (s == WIN)
        return 1;
    if (s == LOSS)
        return -1;
    return cpToWDL(evaluate(board));
}

// Remove all references to the other half
void removeRefs(Tree& tree, Node& node) {
    const NodeIndex startIdx = node.firstChild.load();

    if (startIdx.half() == tree.activeHalf()) {
        Node* child = &tree[startIdx];
        for (usize idx = 0; idx < +node.numChildren; idx++)
            removeRefs(tree, child[idx]);
    }
    else
        node.numChildren = 0;
}

// A recursive implementation of the MCTS algorithm
// based on implementations from Monty and Jackal
float searchNode(Tree& tree, Node& node, const Board& board, u64& currentIndex, u64& seldepth, RelaxedAtomic<u64>& cumulativeDepth, vector<u64>& posHistory, const SearchParameters& params, const usize ply) {
    float score;

    // If the node is terminal (W/D/L) then return the score right away
    if (node.isTerminal())
        score = evaluateNode(node, board);
    // Otherwise if the node is being visited for the first time, set the state, then backprop
    // either the state's score or the NN's score
    else if (node.visits == 0) {
        node.state.store(stateOf(board, posHistory));
        score = evaluateNode(node, board);
    }
    else {
        const bool inCurrentHalf = node.firstChild.load().half() == tree.activeHalf();
        const u8 numChildren = node.numChildren.load();

        // If the node has no children, expand it
        if (numChildren == 0)
            expandNode(tree, board, node, currentIndex, params);
        // Otherwise, if the node's children are in the other
        // half, copy them across
        else if (!inCurrentHalf && numChildren > 0)
            copyChildren(tree, node, currentIndex);

        // A half switch means expansion/copy aborted
        // so avoid dereferencing bad children
        // Also guard against failing to create children when the half is full
        if (tree.switchHalves || node.numChildren == 0)
            return 0;

        // Now that the children are either expanded or in the current half,
        // travel deeper into the tree
        Node& bestChild = findBestChild(tree, node, params);
        Board newBoard = board;
        newBoard.move(bestChild.move);

        posHistory.push_back(newBoard.zobrist);
        score = -searchNode(tree, bestChild, newBoard, currentIndex, seldepth, cumulativeDepth, posHistory, params, ply + 1);
        posHistory.pop_back();
    }

    if (tree.switchHalves)
        return 0;

    // Backprop as the stack unwinds
    node.totalScore.getUnderlying().fetch_add(score, std::memory_order_relaxed);
    node.visits.getUnderlying().fetch_add(1, std::memory_order_relaxed);

    cumulativeDepth.getUnderlying().fetch_add(1, std::memory_order_relaxed);
    seldepth = std::max(seldepth, ply);

    return score;
}

// The entry point to the main search
Move Searcher::search(const SearchParameters params, const SearchLimits limits) {
    auto& cumulativeDepth = this->nodeCount;

    u64 currentIndex = 1;
    u64 iterations = 0;
    u64 halfChanges = 0;
    usize seldepth = 0;

    usize multiPV = std::min(::multiPV, Movegen::generateMoves(rootPos).length);

    // Time management
    i64 timeToSpend = limits.time / 20 + limits.inc / 2;

    if (limits.time != 0 || limits.inc != 0)
        timeToSpend = std::max<i64>(timeToSpend - static_cast<i64>(MOVE_OVERHEAD), 1);

    // Returns true if search has met a limit
    const auto stopSearching = [&]() {
        if (timeToSpend && tree.root().state.load().state() != ONGOING)
            return true;
        const u64 nodeCount = this->nodeCount.load();
        if (this->stopSearching.load() || (timeToSpend != 0 && static_cast<i64>(limits.commandTime.elapsed()) >= timeToSpend))
            return true;
        return (limits.nodes > 0 && nodeCount >= limits.nodes) || (limits.depth > 0 && cumulativeDepth / iterations >= limits.depth);
    };

    // Intervals to report on
    Stopwatch<std::chrono::milliseconds> stopwatch;
    RollingWindow<std::pair<u64, Move>>  bestMoves(std::max<int>(getTerminalRows() - 28 - multiPV, 1));
    usize                                lastDepth    = 0;
    usize                                lastSeldepth = 0;
    Move                                 lastMove     = Move::null();

    const auto printUCI = [&]() {
        vector<Node> children;
        const Node   root  = tree.root();
        const Node*  child = &tree[root.firstChild];
        const Node*  end   = child + root.numChildren;
        for (const Node* idx = child; idx != end; idx++)
            children.push_back(*idx);

        std::ranges::sort(children, std::greater{}, [](const Node& n) { return -getAdjustedScore(n); });

        const u64 time = limits.commandTime.elapsed();

        for (usize i = 1; i <= multiPV; i++) {
            const Node&    n  = children[i - 1];
            const MoveList pv = findPV(tree, &n);

            cout << "info depth " << cumulativeDepth / iterations;
            cout << " seldepth " << seldepth;
            cout << " time " << time;
            cout << " nodes " << nodeCount.load();
            if (time > 0)
                cout << " nps " << nodeCount.load() * 1000 / time;
            cout << " hashfull " << currentIndex * 1000 / tree.activeTree().size();
            cout << " hswitches " << halfChanges;
            cout << " multipv " << i;
            if (n.state.load().state() == ONGOING || n.state.load().state() == DRAW)
                cout << " score cp " << wdlToCP(-n.getScore());
            else
                cout << " score mate " << (n.state.load().distance() + 1) / 2 * (n.state.load().state() == WIN ? 1 : -1);
            cout << " pv";
            for (Move m : pv)
                cout << " " << m;
            cout << endl;
        }
    };

    const auto prettyPrint = [&]() {
        const MoveList pv = findPV(tree);
        cursor::goTo(1, 1);

        cout << rootPos.asString(pv[0]) << "\n";

        cout << Colors::GREY << " Tree Size:    " << Colors::WHITE << (tree.nodes[0].size() + tree.nodes[1].size() + 2) * sizeof(Node) / 1024 / 1024 << "MB\n";
        cout << Colors::GREY << " Half Usage:   " << Colors::WHITE;
        coloredProgBar(50, static_cast<float>(currentIndex) / tree.activeTree().size());
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
        const float rootWdl = tree.root().getScore();
        cout << Colors::GREY << " Score:   ";
        if (tree.root().state.load().state() == ONGOING || tree.root().state.load().state() == DRAW)
            printColoredScore(rootWdl);
        else
            cout << Colors::WHITE << "M in " << (tree.root().state.load().distance() + 1) / 2 * (tree.root().state.load().state() == WIN ? 1 : -1);
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
    expandNode(tree, rootPos, tree.root(), currentIndex, params);

    // Prepare for pretty printing
    if (params.doReporting && !params.doUci) {
        cursor::clearAll();
        cursor::hide();
        cursor::home();
    }

    // Main search loop
    do {
        // Reset zobrist history
        vector<u64> posHistory = params.posHistory;

        searchNode(tree, tree.root(), rootPos, currentIndex, seldepth, cumulativeDepth, posHistory, params, 0);

        // Switch halves
        if (tree.switchHalves) {
            tree.switchHalves = false;
            tree.inactiveTree()[0] = tree.root();
            removeRefs(tree, tree.root());
            currentIndex = 1;
            tree.switchHalf();
            copyChildren(tree, tree.root(), currentIndex);
            halfChanges++;
        }

        iterations++;

        // Check if UCI should be printed
        if (params.doReporting) {
            const Move bestMove = findPvMove(tree, tree.root());
            if (params.doUci && !params.minimalUci && (lastDepth != cumulativeDepth / iterations || lastSeldepth != seldepth || bestMove != lastMove || stopwatch.elapsed() >= UCI_REPORTING_FREQUENCY)) {
                const Move bestMove = findPvMove(tree, tree.root());
                printUCI();

                lastDepth    = cumulativeDepth / iterations;
                lastSeldepth = seldepth;
                lastMove     = bestMove;
                stopwatch.reset();
            }
            else if (!params.doUci && (iterations == 2 || stopwatch.elapsed() >= 40)) {
                const Move bestMove = findPvMove(tree, tree.root());
                if (bestMove != lastMove)
                    bestMoves.push({ limits.commandTime.elapsed(), bestMove });
                prettyPrint();

                lastDepth    = cumulativeDepth / iterations;
                lastSeldepth = seldepth;
                lastMove     = bestMove;
                stopwatch.reset();
            }
        }
        if (iterations % 1024)
            currentMove = findPvMove(tree, tree.root());
    } while (!stopSearching());

    const Move bestMove = findPvMove(tree, tree.root());

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

    currentMove = findPvMove(tree, tree.root());

    return bestMove;
}

// These two functions provide the capability to instantly move
// based on the move prediction from either of the two NNs

Move Searcher::searchPolicy(const SearchParameters params) {
    fillRootPolicy(rootPos);

    const Node  root  = tree.root();
    const Node* child = &tree[root.firstChild];
    const Node* end   = child + root.numChildren;

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
        i32  eval;

        MoveEvalPair() = default;
        MoveEvalPair(const Move move, const i32 eval) :
            move(move),
            eval(eval){};
    };

    vector<MoveEvalPair> moves;

    const MoveList legalMoves = Movegen::generateMoves(rootPos);

    for (const Move m : legalMoves) {
        Board b = rootPos;
        b.move(m);
        const i32 score = evaluate(b);
        moves.emplace_back(m, score);
    }

    const Move best = std::ranges::max_element(moves, {}, [](const MoveEvalPair& m) { return m.eval; })->move;

    if (params.doReporting)
        cout << "bestmove " << best << endl;

    return best;
}
