#include "GAConfig.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <cctype>

static inline std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, e - b + 1);
}

GAConfig LoadGAConfig(const std::string& path) {
    GAConfig cfg;
    std::ifstream fin(path);
    if (!fin) {
        std::ifstream fin2(std::string("../") + path);
        if (fin2) fin.swap(fin2);
    }
    if (!fin) {
        return cfg; // 使用默认
    }

    std::string line;
    while (std::getline(fin, line)) {
        // 去除注释
        auto posc = line.find('#');
        if (posc != std::string::npos) line = line.substr(0, posc);
        line = trim(line);
        if (line.empty()) continue;

        std::string key, val;
        auto poseq = line.find('=');
        if (poseq != std::string::npos) {
            key = trim(line.substr(0, poseq));
            val = trim(line.substr(poseq + 1));
        } else {
            std::istringstream ss(line);
            ss >> key >> val;
            key = trim(key); val = trim(val);
        }
        std::string lkey = key;
        std::transform(lkey.begin(), lkey.end(), lkey.begin(), ::tolower);

        if (lkey == "seed") {
            // 支持字符串种子：对字符串做哈希并映射到非负 int
            bool looks_numeric = !val.empty() && (std::isdigit(static_cast<unsigned char>(val[0])) || val[0] == '-' || val[0] == '+');
            if (looks_numeric) {
                try {
                    cfg.seed = std::stoi(val);
                } catch (...) {
                    unsigned int hv = static_cast<unsigned int>(std::hash<std::string>{}(val));
                    cfg.seed = static_cast<int>(hv & 0x7fffffff);
                }
            } else {
                unsigned int hv = static_cast<unsigned int>(std::hash<std::string>{}(val));
                cfg.seed = static_cast<int>(hv & 0x7fffffff);
            }
        } else {
            try {
                if (lkey == "pop_size") cfg.pop_size = std::stoi(val);
                else if (lkey == "generations") cfg.generations = std::stoi(val);
                else if (lkey == "mutation_rate") cfg.mutation_rate = std::stod(val);
                else if (lkey == "tournament_k") cfg.tournament_k = std::stoi(val);
            } catch (...) {
                // 忽略非法项，保留默认值
            }
        }
    }
    return cfg;
}