// SPDX-License-Identifier: MIT
// Scratch scaling probe (not part of the deliverable): finds superlinear cost.
#include "Core.h"
#include "Diagnostics.h"
#include "Waml.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace wui;

namespace {
double NowUs() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double, std::micro>(clock::now() - start).count();
}

std::string ManyNodes(int n) {
    std::string s = "<Page><StackPanel>";
    for (int i = 0; i < n; ++i) s += "<TextBlock Text=\"x\"/>";
    s += "</StackPanel></Page>";
    return s;
}

std::string ManyAttributes(int blocks) {
    std::string s = "<Page><StackPanel>";
    for (int i = 0; i < blocks; ++i) {
        s += "<TextBlock Text=\"{Binding Title}\" FontSize=\"28\" FontWeight=\"SemiBold\" "
             "Foreground=\"TextPrimary\" Margin=\"4\"/>";
    }
    s += "</StackPanel></Page>";
    return s;
}

std::string LongText(int n) {
    std::string s = "<Page><TextBlock Text=\"";
    for (int i = 0; i < n; ++i) s += "abcdefghij";
    s += "\"/></Page>";
    return s;
}

template <class F>
void Scale(const char* label, F make, const std::vector<int>& sizes) {
    std::printf("%s\n", label);
    for (int n : sizes) {
        const std::string src = make(n);
        std::vector<double> samples;
        for (int i = 0; i < 30; ++i) {
            ParseOptions opt;
            opt.fileName = "scale.waml";
            DiagnosticBag bag;
            const double t0 = NowUs();
            auto doc = CompileWaml(src, opt, bag);
            const double t1 = NowUs();
            if (!doc) {
                std::printf("  n=%d FAILED: %s\n", n, bag.ToText().c_str());
                break;
            }
            if (i >= 5) samples.push_back(t1 - t0);
        }
        if (samples.empty()) continue;
        double best = samples[0];
        for (double v : samples) best = std::min(best, v);
        std::printf("  n=%-5d bytes=%-6zu %.1f us  (%.1f ns/node)\n", n, src.size(), best,
                    best * 1000.0 / static_cast<double>(n + 6));
    }
}
} // namespace

int main() {
    Scale("many nodes (Text only)", [](int n) { return ManyNodes(n); }, {50, 100, 200, 400, 800});
    Scale("many attributes", [](int n) { return ManyAttributes(n); }, {10, 20, 40, 80, 160});
    Scale("long text content", [](int n) { return LongText(n); }, {50, 100, 200, 400, 800});
    return 0;
}
