#include "Detector.h"

namespace core {

DetectResult detectItems(const Reading& base, const Reading& now, const Config& cfg,
                         const DistanceCutoffs& cutoffs) {
    DetectResult r;
    if (base.weightValid && now.weightValid) r.byWeight = (now.weightG - base.weightG) >= cfg.weightThresholdG;
    for (int i = 0; i < 2; ++i) {
        if (!base.distValid[i] || !now.distValid[i]) continue;
        r.byDist[i] = cutoffs[i] > 0 ? now.distMm[i] <= cutoffs[i]
                                     : base.distMm[i] - now.distMm[i] >= cfg.distThresholdMm;
    }
    r.byIr = now.irBlocked && !base.irBlocked;
    r.present = r.byWeight || r.byDist[0] || r.byDist[1] || r.byIr;
    return r;
}

}  // namespace core
