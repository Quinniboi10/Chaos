#include "searcher.h"
#include "movegen.h"
#include "policy.h"
#include "eval.h"
#include "globals.h"

#include <cmath>
#include <functional>

bool switchHalves = false;

// Try to reuse the tree before searching
void Searcher::attemptTreeReuse(const Board& board) {
    if (board != rootPos) {
        Node* reusedNode = nullptr;

        Node& oldRoot = tree.inactiveTree()[0];
        if (oldRoot.numChildren > 0) {
            Node* child = &tree[oldRoot.firstChild.load()];

            for (usize i = 0; i < oldRoot.numChildren && reusedNode == nullptr; i++) {
                Board b1 = rootPos;
                b1.move(child[i].move);

                if (b1 == board) {
                    reusedNode = &child[i];
                    break;
                }

                if (child[i].numChildren > 0) {
                    Node* grandchild = &tree[child[i].firstChild.load()];

                    for (usize j = 0; j < child[i].numChildren; j++) {
                        Board b2 = b1;
                        b2.move(grandchild[j].move);

                        if (b2 == board) {
                            reusedNode = &grandchild[j];
                            break;
                        }
                    }
                }
            }
        }

        if (reusedNode != nullptr)
            tree.inactiveTree()[0] = *reusedNode;
        else
            tree.inactiveTree()[0] = Node();
    }

    tree.root() = Node();
    rootPos     = board;
}

// Return if a node is an unexplored or terminal node in the current half
bool isLeaf(const Node& node, const u8 currentHalf) { return node.numChildren == 0 || node.firstChild.load().half() != currentHalf; }

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
}

// Expand a node
void expandNode(Tree& tree, const Board& board, Node& node, u64& currentIndex, const SearchParameters& params) {
    MoveList moves = Movegen::generateMoves(board);

    // Mates aren't handled until the simulation/rollout stage
    if (moves.length == 0)
        return;

    if (currentIndex + moves.length >= tree.activeTree().size()) {
        switchHalves = true;
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

// Evaluate node
float simulate(const Board& board, const vector<u64>& posHistory, Node& node) {
    assert(node.state.load().state() == ONGOING);

    if (board.isDraw(posHistory) || (node.numChildren == 0 && !board.inCheck()))
        node.state = DRAW;
    else if (node.numChildren == 0)
        node.state = LOSS;
    if (node.state.load().state() != ONGOING)
        return node.getScore();
    return cpToWDL(evaluate(board));
}

// Returns the cp score of a move, including mate distance
i32 scoreNode(const Node& node) {
    const GameState state = node.state.load();
    const i32 score = wdlToCP(node.getScore());

    if (state.state() == WIN)
        return MATE_SCORE - state.distance();
    if (state.state() == LOSS)
        return -MATE_SCORE + state.distance();
    return score;
}

// Find the PV (best Q) move for a node
Move findPvMove(const Tree& tree, const Node& node) {
    const Node* child = &tree[node.firstChild.load()];

    i32 bestScore = -scoreNode(*child);
    Move  bestMove  = child->move;
    for (usize idx = 1; idx < node.numChildren; idx++) {
        const i32 score = -scoreNode(child[idx]);
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
        i32             bestScore = -scoreNode(*child);
        for (usize idx = 1; idx < node->numChildren; idx++) {
            const i32 score = -scoreNode(child[idx]);
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
void copyChildren(Tree& tree, Node& node, u64& currentIndex) {
    const u8 numChildren = node.numChildren;
    if (currentIndex + numChildren > tree.activeTree().size()) {
        switchHalves = true;
        return;
    }

    const Node* oldChild = &tree[node.firstChild.load()];
    Node*       newChild = &tree.activeTree()[currentIndex];

    for (usize i = 0; i < numChildren; i++)
        newChild[i] = oldChild[i];

    node.firstChild.store({ currentIndex, tree.activeHalf() });

    currentIndex += node.numChildren;
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

float searchNode(
  Tree& tree, RelaxedAtomic<u64>& cumulativeDepth, usize& seldepth, u64& currentIndex, vector<u64>& posHistory, const Board& board, Node& node, const SearchParameters& params, usize ply) {
    // Check for an early return
    if (node.state.load().state() != ONGOING)
        return node.getScore();

    float score;

actionBranch:
    // Selection
    if (!isLeaf(node, tree.activeHalf())) {
        Node& bestChild = findBestChild(tree, node, params);
        Board newBoard  = board;
        newBoard.move(bestChild.move);

        posHistory.push_back(newBoard.zobrist);
        score = -searchNode(tree, cumulativeDepth, seldepth, currentIndex, posHistory, newBoard, bestChild, params, ply + 1);
        posHistory.pop_back();

        // Mate backprop
        const GameState childState = bestChild.state.load();
        if (childState.state() == LOSS)
            node.state = GameState(WIN, childState.distance() + 1);
        else if (childState.state() == WIN) {
            bool isLoss = true;
            u16 maxLen = childState.distance();

            const Node* firstChild = &tree[node.firstChild.load()];
            const Node* end = firstChild + node.numChildren;
            for (const Node* child = firstChild; child != end; child++) {
                if (child->state.load().state() == WIN) {
                    maxLen = std::max(maxLen, child->state.load().distance());
                }
                else {
                    isLoss = false;
                    break;
                }
            }

            if (isLoss)
            node.state = GameState(LOSS, maxLen + 1);
        }
    }
    // Expansion + simulation
    else {
        if (node.firstChild.load().half() != tree.activeHalf() && node.numChildren > 0) {
            if (switchHalves)
                return 0;
            copyChildren(tree, node, currentIndex);
            goto actionBranch;
        }
        else if (node.visits == 0)
            score = cpToWDL(evaluate(board));
        else {
            expandNode(tree, board, node, currentIndex, params);
            score = simulate(board, posHistory, node);
        }
    }

    if (switchHalves)
        return 0;

    // Backprop
    node.totalScore.getUnderlying().fetch_add(score, std::memory_order_relaxed);
    node.visits.getUnderlying().fetch_add(1, std::memory_order_relaxed);

    cumulativeDepth.getUnderlying().fetch_add(1, std::memory_order_relaxed);
    seldepth = std::max(seldepth, ply);

    return score;
}

Move Searcher::search(const SearchParameters params, const SearchLimits limits) {
    // Reset searcher
    this->nodeCount     = 0;
    this->stopSearching = false;

    tree.root().move            = Move::null();
    tree.inactiveTree()[0].move = Move::null();

    usize multiPV = std::min(::multiPV, Movegen::generateMoves(rootPos).length);

    u64 currentIndex = 1;

    u64 halfChanges = 0;

    u64 iterations        = 0;
    auto& cumulativeDepth = this->nodeCount;

    usize seldepth = 0;

    // Time management
    i64 timeToSpend = limits.time / 20 + limits.inc / 2;

    if (timeToSpend)
        timeToSpend -= MOVE_OVERHEAD;

    // Returns true if search has met a limit
    const auto stopSearching = [&]() {
        if (tree.root().state.load().state() != ONGOING)
            return true;
        const u64 nodeCount = this->nodeCount.load();
        if (this->stopSearching.load() || (timeToSpend != 0 && static_cast<i64>(limits.commandTime.elapsed()) >= timeToSpend))
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
        const Node   root  = tree.root();
        const Node*  child = &tree[root.firstChild];
        const Node*  end   = child + root.numChildren;
        for (const Node* idx = child; idx != end; idx++)
            children.push_back(*idx);

        std::ranges::sort(children, std::greater{}, [](const Node& n) { return -scoreNode(n); });

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
        cursor::goTo(1, 14);

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

        cout << rootPos << "\n";
        cout << Colors::GREY << " Tree Size:    " << Colors::WHITE << (tree.nodes[0].size() + tree.nodes[1].size()) * sizeof(Node) / 1024 / 1024 << "MB\n";
    }

    // Main search loop
    do {
        // Reset zobrist history
        posHistory = params.positionHistory;

        searchNode(tree, cumulativeDepth, seldepth, currentIndex, posHistory, rootPos, tree.root(), params, 0);

        // Switch halves
        if (switchHalves) {
            switchHalves           = false;
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

    return bestMove;
}

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