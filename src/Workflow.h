#pragma once

// A ComfyUI workflow in the API format (ComfyUI's "Export (API)"), the nodes the plugin feeds and
// reads, and the prompt for one clip.
//
// The author titles two nodes: "CDMD Input", a LoadImage node (one picture a run) or the
// "ColorizingDMD Load Clip" node of custom_nodes/colorizingdmd (several consecutive frames a
// run), and "CDMD Output", the SaveImage or PreviewImage node whose pictures are the results.
// Text inputs may hold placeholders, filled in for each run: {seed}, {width} and {height} (the
// size the results go to, twice the pictures sent), {sdwidth} and {sdheight} (the size of the
// pictures sent) and {count} (pictures in the run). An input that is only a placeholder becomes
// a number.

#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "Gather.h"

namespace comfy {

constexpr const char* kInputTitle = "CDMD Input";
constexpr const char* kOutputTitle = "CDMD Output";
// The class of the clip node (custom_nodes/colorizingdmd).
constexpr const char* kClipClass = "CDMDLoadClip";

struct Workflow {
    nlohmann::json prompt;
    std::string inputNode;
    std::string outputNode;
    // The input node takes several pictures (the clip node); else one (LoadImage).
    bool takesClips = false;
};

// Throws PluginError, with what the author has to change, for text that is not a workflow in
// the API format or lacks the two nodes.
[[nodiscard]] Workflow parseWorkflow(const std::string& text);

struct Placeholders {
    int64_t seed = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sdWidth = 0;
    uint32_t sdHeight = 0;
    uint32_t count = 0;
};

// The prompt for one run: the uploaded pictures' names in the input node, the placeholders
// filled in. Throws PluginError for more than one picture when the workflow takes one.
[[nodiscard]] nlohmann::json preparePrompt(const Workflow& workflow,
                                           const std::vector<std::string>& names,
                                           const Placeholders& placeholders);

}  // namespace comfy
