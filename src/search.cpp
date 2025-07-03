#include "worker.h"
#include "searcher.h"
#include "movegen.h"
#include "eval.h"

#include <cmath>
#include <numeric>
#include <functional>

void Worker::search(const Board& board, vector<Node>& nodes, const SearchParameters params, const SearchLimits limits) {
    // Reset worker
    this->nodes = 0;

    u64 currentIndex = 1;

    auto& iterations = this->nodes;
    u64 cumulativeDepth = 0;

    usize seldepth = 0;

    // Time management
    i64 timeToSpend = limits.time / 20 + limits.inc / 2;

    if (timeToSpend)
        timeToSpend -= MOVE_OVERHEAD;

    if (nodes.size() < limits.maxNodes)
        nodes.resize(limits.maxNodes);

    // Returns true if search has met a limit
    const auto stopSearching = [&]() {
        return currentIndex >= limits.maxNodes
            || (limits.depth > 0 && cumulativeDepth / iterations >= limits.depth)
            || (timeToSpend != 0 && static_cast<i64>(limits.commandTime.elapsed()) >= timeToSpend);
    };

    // Board at the leaf node, updated when a new leaf node is found to search
    Board boardAtLeaf = board;
    // Positions from root to the leaf
    vector<u64> posHistory;

    // ****** Define lambdas for node operations ******

    // Return if a node is an unexplored or terminal node
    const auto isLeaf = [](const Node& node) { return node.numChildren == 0; };

    // Return if a node is threefold (or twofold if all positions are past root)
    const auto isThreefold = [&](const Node& node) {
        const u64 leafHash = boardAtLeaf.zobrist;

        usize reps = 0;

        for (const u64 hash : params.positionHistory) {
            if (hash == leafHash) {
                reps++;
                if (reps >= 2)
                    return true;
            }
        }

        for (const u64 hash : posHistory) {
            if (hash == leafHash) {
                reps++;
                if (reps >= 2)
                    return true;
            }
        }

        return false;
    };

    // Performs a softmax on the given vector
    const auto softmax = [](vector<double>& scores) {
        assert(!scores.empty());

        // Find the max value
        double maxIn = scores[0];
        for (usize idx = 1; idx < scores.size(); idx++)
            maxIn = std::max(maxIn, scores[idx]);

        // Compute exponentials
        for (double& score : scores)
            score = std::exp(score - maxIn);

        const double sum = std::reduce(scores.begin(), scores.end());
        // Scale down by sum of exponents
        for (double& score : scores)
            score /= sum;
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

        if (currentIndex + moves.length > limits.maxNodes)
            return false;

        // Mates aren't handled until the simulation/rollout stage
        if (moves.length == 0)
            return true;

        node.firstChild = currentIndex;
        node.numChildren = moves.length;

        vector<double> policyScores;
        policyScores.reserve(moves.length);

        // In future, this would be replaced by a policy NN
        // Loop runs backwards so pop_back can be used
        for (i16 idx = static_cast<i16>(moves.length) - 1; idx >= 0; idx--)
            policyScores.push_back(1.0 / node.numChildren);

        softmax(policyScores);

        for (const Move& move : moves) {
            Node& child = nodes[currentIndex++];
            child.totalScore = 0;
            child.visits = 0;
            child.firstChild = 0;
            child.policy = policyScores.back();
            child.state = ONGOING;
            child.move = move;
            child.numChildren = 0;
            child.parent = &node;

            policyScores.pop_back();
        }

        return true;
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
            double bestScore = -nodes[node->firstChild].getScore();
            usize bestIdx = node->firstChild;
            for (usize idx = node->firstChild + 1; idx < node->firstChild + node->numChildren; idx++) {
                const double score = -nodes[idx].getScore();
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
        posHistory.clear();

        Node* node = &nodes[0];

        usize ply = 0;

        while (!isLeaf(*node)) {
            const usize bestIdx = findBestChild(*node);
            node = &nodes[bestIdx];
            posHistory.push_back(boardAtLeaf.zobrist);
            boardAtLeaf.move(node->move);
            ply++;
        }

        cumulativeDepth += ply;
        seldepth = std::max(seldepth, ply);

        // At this point, board is at the state of the best node,
        // and node points to that node
        return node;
    };

    // Backprop of game state
    const std::function<void(Node&)> backpropState = [&](Node& node) {
        GameState state = ONGOING;

        bool isLoss = true;

        for (usize idx = node.firstChild; idx < node.firstChild + node.numChildren; idx++) {
            const Node& child = nodes[idx];
            if (child.state == LOSS) {
                state = WIN;
                break;
            }
            if (child.state != WIN)
                isLoss = false;
        }

        if (isLoss)
            state = LOSS;

        node.state = state;

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

        return cpToWDL(evaluate(boardAtLeaf));
    };

    // Backprop a score until root
    const std::function<void(Node&, double)> backprop = [&](Node& node, double score) {
        node.totalScore += score;
        node.visits++;

        if (node.parent != nullptr)
            backprop(*node.parent, -score);
    };

    const auto printUCI = [&]() {
        MoveList pv = findPV();
        cout << "info depth " << cumulativeDepth / iterations;
        cout << " seldepth " << seldepth;
        cout << " time " << limits.commandTime.elapsed();
        cout << " nodes " << this->nodes.load();
        if (limits.commandTime.elapsed() > 0)
            cout << " nps " << this->nodes.load() * 1000 / limits.commandTime.elapsed();
        if (nodes[0].state == ONGOING || nodes[0].state == DRAW)
            cout << " score cp " << wdlToCP(nodes[0].getScore());
        else
            cout << " score mate " << (pv.length + 1) / 2 * (nodes[0].state == WIN ? 1 : -1);
        cout << " pv";
        for (Move m : pv)
            cout << " " << m;
        cout << endl;

        return pv;
    };

    // Expand root
    expandNode(board, nodes[0]);

    // Intervals to report on
    Stopwatch<std::chrono::milliseconds> stopwatch;
    usize lastDepth = 0;
    usize lastSeldepth = 0;

    // Main search loop
    do {
        Node* next = findNextNode();

        // If node is terminal, instantly backprop it
        double score;

        if (boardAtLeaf.isDraw() || isThreefold(*next)) {
            next->state = DRAW;
            score = 0;
        }
        else if (next->state == ONGOING) {

            // If expand node failed, hash limit has been hit
            if (!expandNode(boardAtLeaf, *next))
                break;

            score = simulate(*next);
        }
        else
            score = next->getScore();

        backprop(*next, score);

        iterations++;

        // Check if UCI should be printed
        if ((lastDepth != cumulativeDepth / iterations || lastSeldepth != seldepth || stopwatch.elapsed() >= UCI_REPORTING_FREQUENCY) && params.doReporting) {
            printUCI();
            lastDepth = cumulativeDepth / iterations;
            lastSeldepth = seldepth;
            stopwatch.reset();
        }
    } while (!stopSearching());

    if (params.doReporting) {
        MoveList pv = printUCI();

        cout << "bestmove " << pv[0] << endl;
    }
}
