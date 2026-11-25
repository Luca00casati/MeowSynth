// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Luca Casati
#pragma once
// Reader for module traces written by tools/trace_modules.cpp (see tools/trace_read.py).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct TracePlug { int index; bool out; uint32_t buf; int column; };
struct TraceInstance { int n; std::string cls; std::vector<TracePlug> plugs; };

class Trace {
public:
    int block = 0, blocks = 0, nplugs = 0;
    std::vector<TraceInstance> inst;

    bool load(const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::vector<char> raw;
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) raw.insert(raw.end(), buf, buf + n);
        std::fclose(f);
        std::string s(raw.begin(), raw.end());
        size_t data = s.find("DATA ");
        if (data == std::string::npos) return false;
        std::istringstream head(s.substr(0, data));
        std::string line;
        int col = 0;
        while (std::getline(head, line)) {
            std::istringstream ls(line);
            std::string tag, addr;
            TraceInstance ti;
            ls >> tag >> ti.n >> addr >> ti.cls;
            if (tag != "INST") continue;
            std::string p;
            while (ls >> p) {
                TracePlug tp;
                char dir[8];
                unsigned b;
                std::sscanf(p.c_str(), "%d:%3[^:]:%x", &tp.index, dir, &b);
                tp.out = !std::strcmp(dir, "out");
                tp.buf = b;
                tp.column = col++;
                ti.plugs.push_back(tp);
            }
            inst.push_back(ti);
        }
        nplugs = col;
        size_t nl = s.find('\n', data);
        block = std::atoi(s.c_str() + data + 5);
        size_t bytes = s.size() - (nl + 1);
        samplesRaw.resize(bytes / 4);
        std::memcpy(samplesRaw.data(), s.data() + nl + 1, samplesRaw.size() * 4);
        blocks = (int)(samplesRaw.size() / ((size_t)nplugs * block));
        return true;
    }

    std::vector<const TraceInstance*> find(const char* cls) const {
        std::vector<const TraceInstance*> r;
        for (auto& i : inst) if (i.cls == cls) r.push_back(&i);
        return r;
    }

    const TracePlug* plug(const TraceInstance& i, int index) const {
        for (auto& p : i.plugs) if (p.index == index) return &p;
        return nullptr;
    }

    // All samples of one plug.
    std::vector<float> samples(const TraceInstance& i, int index) const {
        std::vector<float> r;
        const TracePlug* p = plug(i, index);
        if (!p) return r;
        r.reserve((size_t)blocks * block);
        for (int b = 0; b < blocks; b++) {
            const float* src = &samplesRaw[((size_t)b * nplugs + p->column) * block];
            r.insert(r.end(), src, src + block);
        }
        return r;
    }

private:
    std::vector<float> samplesRaw;
};
