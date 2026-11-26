#pragma once
// 「籃內有東西」的判定與讀數平滑。
#include <array>

#include "Config.h"

namespace core {

// 移動平均：保留最近 n 筆，避免單次雜訊誤判。
class Smoother {
public:
    explicit Smoother(int n = kSmoothSamples) : n_(n > kMax ? kMax : (n < 1 ? 1 : n)) {}
    void add(float v) {
        buf_[pos_] = v;
        pos_ = (pos_ + 1) % n_;
        if (count_ < n_) ++count_;
    }
    void clear() { count_ = pos_ = 0; }
    bool full() const { return count_ >= n_; }
    bool hasValue() const { return count_ > 0; }
    float value() const {
        float s = 0;
        for (int i = 0; i < count_; ++i) s += buf_[i];
        return count_ ? s / count_ : 0;
    }

private:
    static constexpr int kMax = 16;
    int n_;
    float buf_[kMax] = {};
    int pos_ = 0, count_ = 0;
};

// 一組（平滑後的）感測器讀數。也用來當「空籃基準」。
struct Reading {
    bool weightValid = false;
    float weightG = 0;
    bool distValid[2] = {false, false};
    int distMm[2] = {0, 0};
    bool irBlocked = false;
};

struct DetectResult {
    bool present = false;  // 任一條件成立
    bool byWeight = false;
    bool byDist[2] = {false, false};
    bool byIr = false;
};

// 每顆測距器的有物上限（含等於），0 代表使用相對於基準的距離門檻。
using DistanceCutoffs = std::array<int, 2>;

// 任一重量、測距或紅外線條件成立就算有物。讀不到的感測器不參與判定。
DetectResult detectItems(const Reading& baseline, const Reading& now, const Config& cfg,
                         const DistanceCutoffs& cutoffs = {});

}  // namespace core
