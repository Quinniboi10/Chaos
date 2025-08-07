#include "searcher.h"
#include "movegen.h"
#include "eval.h"

#include <cmath>
#include <numeric>
#include <functional>

void Searcher::search(vector<Node>& nodes, const SearchParameters params, const SearchLimits limits) {
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

    if (nodes.size() < limits.maxNodes)
        nodes.resize(limits.maxNodes);

    // Returns true if search has met a limit
    const auto stopSearching = [&]() {
        return currentIndex >= limits.maxNodes
            || (limits.depth > 0 && cumulativeDepth / iterations >= limits.depth)
            || (timeToSpend != 0 && static_cast<i64>(limits.commandTime.elapsed()) >= timeToSpend);
    };

    // Positions from root to the leaf
    vector<u64> posHistory;

    // ****** Define lambdas for node operations ******

    // Return if a node is an unexplored or terminal node
    const auto isLeaf = [](const Node& node) { return node.numChildren == 0; };

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
    const auto expandNode =
      [&](const Board& board, Node& node) {
          MoveList moves = Movegen::generateMoves(board);

          // Mates aren't handled until the simulation/rollout stage
          if (moves.length == 0)
              return;

          node.firstChild  = currentIndex;
          node.numChildren = moves.length;

          vector<double> policyScores;
          policyScores.reserve(moves.length);

          // In future, this would be replaced by a policy NN
          // Loop runs backwards so pop_back can be used
          for (i16 idx = static_cast<i16>(moves.length) - 1; idx >= 0; idx--) {
              const Move m = moves[idx];
              const PieceType capturedPiece = board.getPiece(m.to());

              const double policyScore = array<double, 7>{0.7, 2, 2, 3, 4, 0, 0}[capturedPiece];

              policyScores.push_back(policyScore);
        }

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
    };

    // Find the best child node from a parent
    const auto findBestChild = [&](const Node& node) -> Node& {
        double bestScore = puct(node, nodes[node.firstChild]);
        Node* bestChild = &nodes[node.firstChild];
        for (usize idx = node.firstChild + 1; idx < node.firstChild + node.numChildren; idx++) {
            const double score = puct(node, nodes[idx]);
            if (score > bestScore) {
                bestScore = score;
                bestChild = &nodes[idx];
            }
        }

        return *bestChild;
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

    const auto printUCI = [&]() {
        MoveList pv = findPV();
        cout << "info depth " << cumulativeDepth / iterations;
        cout << " seldepth " << seldepth;
        cout << " time " << limits.commandTime.elapsed();
        cout << " nodes " << nodeCount.load();
        if (limits.commandTime.elapsed() > 0)
            cout << " nps " << nodeCount.load() * 1000 / limits.commandTime.elapsed();
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

    const std::function<double(const Board&, Node&, usize)> searchNode = [&](const Board& board, Node& node, usize ply) {
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
            if (node.visits == 0)
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
    usize lastDepth = 0;
    usize lastSeldepth = 0;

    // Expand root
    expandNode(rootPos, nodes[0]);

    // Main search loop
    do {
        // Reset zobrist history
        posHistory = params.positionHistory;

        searchNode(rootPos, nodes[0], 0);

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