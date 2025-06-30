#include "worker.h"
#include "searcher.h"
#include "movegen.h"
#include "eval.h"

#include <cmath>
#include <functional>

void Worker::search(const Board& board, vector<Node>& nodes, const SearchParameters params, const SearchLimits limits) {
    // Reset worker
    this->nodes = 1;

    u64 currentIndex = 1;

    u64 iterations = 0;
    u64 cumulativeDepth = 0;

    usize seldepth = 0;

    if (nodes.size() < limits.maxNodes)
        nodes.resize(limits.maxNodes);
    for (Node& n : nodes)
        n = Node();

    // Returns true if search has met a limit
    const auto stopSearching = [&]() {
        return currentIndex >= limits.maxNodes
            || (limits.depth > 0 && cumulativeDepth / iterations >= limits.depth);
    };

    // Board at the leaf node, updated when a new leaf node is found to search
    Board boardAtLeaf = board;

    // ****** Define lambdas for node operations ******

    // Return if a node is an unexplored or terminal node
    const auto isLeaf = [](const Node& node) { return node.numChildren == 0; };

    // Return if a node is terminal
    const auto isTerminal = [&](const Node& node) { return node.state != ONGOING && isLeaf(node); };

    // Expand a node
    const auto expandNode = [&](const Board& board, Node& node) {
        MoveList moves = Movegen::generateMoves(board);

        if (currentIndex + moves.length > limits.maxNodes)
            return false;

        node.firstChild = currentIndex;
        node.numChildren = moves.length;

        this->nodes += moves.length;

        for (const Move move : moves) {
            Node& child = nodes[currentIndex++];
            child.move = move;
            child.parent = &node;
        }

        return true;
    };

    // PUCT formula
    const auto puct = [&](const Node& parent, const Node& child) {
        // V + C * P * (N.max(1).sqrt()/n + 1)
        // V = Q = total score / visits
        // C = CPUCT
        // P = move policy score
        // N = parent visits
        // n = child visits
        return parent.getScore() + params.cpuct * 1 / (parent.numChildren + 1) * (std::sqrt(std::max<u64>(parent.visits, 1)) / child.visits + 1);
    };

    // Find the best child node from a parent
    const auto findBestChild = [&](const Node& node) {
        double bestScore = puct(node, nodes[node.firstChild]);
        usize bestIdx = node.firstChild;
        for (usize idx = node.firstChild + 1; idx < node.firstChild + node.numChildren; idx++) {
            const double score = puct(node, nodes[idx]);
            if (score > bestScore) {
                bestScore = score;
                bestIdx = idx;
            }
        }

        return bestIdx;
    };

    // Search the tree for the PV line
    const auto findPV = [&]() {
        MoveList pv{};

        Node* node = &nodes[0];

        while (!isLeaf(*node)) {
            double bestScore = nodes[node->firstChild].getScore();
            usize bestIdx = node->firstChild;
            for (usize idx = node->firstChild + 1; idx < node->firstChild + node->numChildren; idx++) {
                const double score = nodes[idx].getScore();
                if (score > bestScore) {
                    bestScore = score;
                    bestIdx = idx;
                }
            }

            node = &nodes[bestIdx];
            pv.add(node->move);
        }

        return pv;
    };

    // Search the tree to find the next node to expand
    const auto findNextNode = [&]() {
        boardAtLeaf = board;

        Node* node = &nodes[0];

        usize depth = 0;

        while (!isLeaf(*node)) {
            const usize bestIdx = findBestChild(*node);
            node = &nodes[bestIdx];
            boardAtLeaf.move(node->move);
            depth++;
        }

        cumulativeDepth += depth;
        seldepth = std::max(seldepth, depth);

        // At this point, board is at the state of the best node,
        // and node points to that node
        return node;
    };

    // Backprop of game state
    const std::function<void(Node&)> backpropState = [&](Node& node) {
        GameState state = LOSS;
        for (usize idx = node.firstChild; idx < node.firstChild + node.numChildren; idx++)
            state = std::max<GameState>(state, nodes[idx].state);

        // Flip for stm
        if (state == LOSS)
            state = WIN;
        else if (state == WIN)
            state = LOSS;

        // Don't backprop draw scores
        node.state = static_cast<GameState>(state * state != DRAW);

        if (node.parent != nullptr)
            backpropState(*node.parent);
    };

    // Evaluate node
    const auto simulate = [&](Node& node) {
        if (isLeaf(node) && node.state == ONGOING) {
            if (boardAtLeaf.inCheck())
                node.state = LOSS;
            else
                node.state = DRAW;

            // Since the state of this node changed, backprop to others
            if (node.parent != nullptr)
                backpropState(*node.parent);

            return node.getScore();
        }

        return cpToWDL(materialEval(boardAtLeaf));
    };

    // Backprop a score until root
    const std::function<void(Node&, double)> backprop = [&](Node& node, double score) {
        node.totalScore += score * SCORE_QUANTIZATION;
        node.visits++;

        if (node.parent != nullptr)
            backprop(*node.parent, -score);
    };

    // Expand root
    expandNode(board, nodes[0]);

    // Main search loop
    do {
        Node* next = findNextNode();

        // If node is terminal, instantly backprop it
        double score;

        if (next->state == ONGOING) {
            // If expand node failed, hash limit has been hit
            if (!expandNode(boardAtLeaf, *next))
                break;

            score = simulate(*next);
        }
        else
            score = next->getScore();

        backprop(*next, score);

        iterations++;
    } while (!stopSearching());

    MoveList pv = findPV();

    if (params.doReporting) {
        cout << "info nodes " << this->nodes;
        cout << " depth " << cumulativeDepth / iterations;
        cout << " seldepth " << seldepth;
        if (nodes[0].state == ONGOING || nodes[0].state == DRAW)
            cout << " score cp " << wdlToCP(nodes[0].getScore());
        cout << " pv";
        for (Move m : pv)
            cout << " " << m;
        cout << endl;

        cout << "bestmove " << pv.moves[0] << endl;
    }
}
