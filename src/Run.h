#pragma once

// A run: the gathered pictures uploaded to ComfyUI, the workflow queued for each clip (or each
// picture), and the results made into proposals for the editor's review window.

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ComfyClient.h"
#include "Gather.h"
#include "Pictures.h"
#include "Workflow.h"

namespace comfy {

struct RunHooks {
    // Pictures done of all.
    std::function<void(uint32_t done, uint32_t total)> progress;
    // Asked between polls; true cancels the prompt and ends the run with PluginError.
    std::function<bool()> cancelled;
    std::chrono::milliseconds pollInterval{500};
};

// The names the pictures of a run get in ComfyUI's input folder start with this, then the run's
// prefix and the picture's ID: "cdmd_<run>_f12.png".
constexpr const char* kUploadPrefix = "cdmd_";

// The pictures run through the workflow and their proposals, in the order of the sources. The
// run's name keeps its uploads apart from other runs'. Throws ComfyError when ComfyUI fails,
// PluginError when cancelled or when the workflow's results do not fit.
[[nodiscard]] std::vector<Proposal> runWorkflow(const ComfyClient& client,
                                                const Workflow& workflow,
                                                const std::vector<SourcePicture>& sources,
                                                const Options& options,
                                                const std::string& runName,
                                                const RunHooks& hooks);

}  // namespace comfy
