#pragma once

// The preferred Linux live-video path. Each option retains safe fallbacks so
// a failed live player always ends at JPEG snapshots rather than a dead panel.
enum class LivePlaybackMethod {
    NativeMse,
    ProgressiveMp4,
    BrowserMse,
};
