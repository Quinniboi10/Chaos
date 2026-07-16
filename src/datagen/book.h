#pragma once

#include "../types.h"

#include <ctime>
#include <random>
#include <fstream>
#include <optional>

inline vector<string> readFileToVec(const string& path) {
    vector<string> lines;

    std::ifstream file(path);
    if (!file.is_open()) {
        cerr << "Failed to open " << path << endl;
        std::terminate();
    }

    string line;
    while (std::getline(file, line))
        lines.push_back(line);

    file.close();

    return lines;
}

struct OpeningBook {
    std::optional<vector<string>> fens;

    std::uniform_int_distribution<u64> dist;
    std::mt19937_64 eng;

    OpeningBook(const string& path, const i64 seed = std::time(nullptr), bool verbose = true) {
        if (path.empty() || path == "None") {
            fens = std::nullopt;
            if (verbose)
                cout << "info string found 0 book lines" << endl;
            return;
        }

        this->fens = readFileToVec(path);

        this->dist = std::uniform_int_distribution<u64>{0, fens->size()};

        if (verbose)
            cout << "info string found " << fens->size() << " book lines" << endl;
    }

    string get() {
        if (!fens.has_value()) {
            return "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        }
        return fens->operator[](dist(eng));
    }
};