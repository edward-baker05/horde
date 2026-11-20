#include "FrameStatsOverlay.hpp"

#include <imgui.h>

namespace horde::ui {

void drawFrameStats(const scene::FrameStats& stats, bool* measureSimGpu) {
    ImGui::Text("%.1f fps: %.2f ms per frame (worst %.2f)", stats.frameMs > 0.0f ? 1000.0f / stats.frameMs : 0.0f,
                stats.frameMs, stats.frameMaxMs);

    // What the frame actually cost, as opposed to how often frames came.
    const float physics = stats.updateMs + stats.simGpuMs;
    const float busy = physics + stats.recordMs + stats.otherMs;
    if (ImGui::BeginTable("frame cost", 2, ImGuiTableFlags_SizingFixedFit)) {
        const auto row = [](const char* label, const char* format, float value, float worst = -1.0f) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            if (worst >= 0.0f) {
                ImGui::Text("%6.3f ms  (worst %.3f)", value, worst);
            } else {
                ImGui::Text(format, value);
            }
        };
        row("Physics, CPU", "%6.3f ms", stats.updateMs);
        if (stats.simGpuMeasured) {
            row("Physics, GPU", "", stats.simGpuMs, stats.simGpuMaxMs);
        } else {
            row("Physics, GPU", "not measured", 0.0f);
        }
        row("Physics total", "%6.3f ms", physics);
        row("Frame building, CPU", "%6.3f ms", stats.recordMs);
        row("Other (events, UI)", "%6.3f ms", stats.otherMs);
        row("Busy per frame", "%6.3f ms", busy);
        row("Waiting (cap, vsync)", "%6.3f ms", stats.waitMs);
        ImGui::EndTable();
    }

    if (busy > 0.0f) {
        ImGui::Text("Could run at ~%.0f fps uncapped", 1000.0f / busy);
    }
    if (measureSimGpu) {
        ImGui::Checkbox("Measure GPU physics", measureSimGpu);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Times the physics compute work on the GPU from a background thread,\n"
                              "without waiting for it, so the frame rate is unaffected. Figures trail\n"
                              "by a frame or two. Drawing's GPU time is not included in any line.");
        }
    }
}

} // namespace horde::ui
