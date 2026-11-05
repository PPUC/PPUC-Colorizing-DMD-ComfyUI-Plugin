#pragma once

// The client of ComfyUI's HTTP API: pictures uploaded into its input folder, a prompt queued,
// its history read until it ends, the output pictures downloaded.

#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "Gather.h"

namespace comfy {

// ComfyUI could not be reached, refused the prompt or failed to run it; the text says why.
class ComfyError : public PluginError {
public:
    using PluginError::PluginError;
};

// A picture a node gave, as ComfyUI's /view serves it.
struct OutputImage {
    std::string filename;
    std::string subfolder;
    std::string type;
};

struct PromptState {
    // The prompt has ended: run, failed or interrupted.
    bool finished = false;
    bool failed = false;
    std::string error;
    // The pictures the output node gave, in order.
    std::vector<OutputImage> images;
};

class ComfyClient {
public:
    // url: "http://host:port"; token: sent as a bearer token when not empty (for ComfyUI
    // behind a proxy that asks for one).
    ComfyClient(std::string url, std::string token, int timeoutSeconds = 30);

    // Throws ComfyError when ComfyUI does not answer.
    void check() const;
    // Into ComfyUI's input folder, replacing a file of the name; the name LoadImage takes.
    [[nodiscard]] std::string upload(const std::string& name, const std::string& png) const;
    // The prompt's ID. Throws ComfyError with ComfyUI's reasons when it refuses the prompt.
    [[nodiscard]] std::string queue(const nlohmann::json& prompt,
                                    const std::string& clientId) const;
    [[nodiscard]] PromptState state(const std::string& promptId,
                                    const std::string& outputNode) const;
    [[nodiscard]] std::string download(const OutputImage& image) const;
    // Takes the prompt out of the queue, or interrupts it when it runs.
    void cancel(const std::string& promptId) const;

private:
    std::string m_url;
    std::string m_token;
    int m_timeout;
};

// ComfyUI's answers parsed, for the tests: the reasons of a refused prompt, and a prompt's
// state from /history/<id>.
[[nodiscard]] std::string describeRefusal(const std::string& body);
[[nodiscard]] PromptState parseHistory(const std::string& body,
                                       const std::string& promptId,
                                       const std::string& outputNode);

}  // namespace comfy
