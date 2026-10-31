// Cipher - what an overlay's drawing costs (FrameCost) and its rows.
#include "cipher.hpp"
#include "esia/render/renderer.hpp"
#include <algorithm>

namespace cipher
{
    namespace
    {
        float MsSince(std::chrono::steady_clock::time_point t)
        {
            return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t).count();
        }
    }

    void FrameCost::BeginUi() { uiStart_ = Clock::now(); }

    void FrameCost::EndUi() { uiMs_ = MsSince(uiStart_); }

    void FrameCost::BeginRender() { renderStart_ = Clock::now(); }

    void FrameCost::EndRender(const esia::render::RenderStats& stats) { Add(uiMs_, MsSince(renderStart_), &stats); }

    void FrameCost::Add(float uiMs, float renderMs, const esia::render::RenderStats* stats)
    {
        const Clock::time_point now = Clock::now();
        if (frames_ == 0 && held_.frames == 0)
            since_ = now;
        ui_ += uiMs;
        render_ += renderMs;
        worstCpu_ = std::max(worstCpu_, uiMs + renderMs);
        if (stats && stats->gpu.valid)
        {
            gpu_ += stats->gpu.totalMs;
            worstGpu_ = std::max(worstGpu_, stats->gpu.totalMs);
            ++gpuFrames_;
        }
        if (stats)
            draws_ = stats->drawCalls;
        ++frames_;
        if (now - since_ < std::chrono::milliseconds(500))
            return;
        // half a second's worth: what is shown for the next half second
        held_.frames = frames_;
        held_.uiMs = (float)(ui_ / frames_);
        held_.renderMs = (float)(render_ / frames_);
        held_.cpuMs = held_.uiMs + held_.renderMs;
        held_.gpu = gpuFrames_ > 0;
        held_.gpuMs = gpuFrames_ > 0 ? (float)(gpu_ / gpuFrames_) : 0.0f;
        held_.worstCpuMs = worstCpu_;
        held_.worstGpuMs = worstGpu_;
        held_.draws = draws_;
        ui_ = render_ = gpu_ = 0.0;
        worstCpu_ = worstGpu_ = 0.0f;
        frames_ = gpuFrames_ = 0;
        since_ = now;
    }

    void Ui::FrameCostRows(const FrameCost& cost)
    {
        const FrameCost::Numbers& n = cost.Held();
        const bool any = n.frames > 0, gpu = any && n.gpu;
        // the same rows from the first frame on: their values come once half a second has been measured
        const auto ms = [&](const char* label, bool has, float v) {
            if (has)
                LabelText(label, "%.2f ms", (double)v);
            else
                LabelText(label, "%s", any ? "not timed" : "...");
        };
        PushId("##frame-cost");
        ms("Build", any, n.uiMs);
        ms("Render", any, n.renderMs);
        ms("GPU", gpu, n.gpuMs);
        ms("Peak CPU", any, n.worstCpuMs);
        ms("Peak GPU", gpu, n.worstGpuMs);
        if (any)
            LabelText("Draws", "%d", n.draws);
        else
            LabelText("Draws", "%s", "...");
        PopId();
    }
}
