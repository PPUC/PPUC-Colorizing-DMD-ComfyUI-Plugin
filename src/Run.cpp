#include "Run.h"

#include <stdexcept>
#include <thread>

#include "Images.h"

namespace comfy {

namespace {

// The picture as it is sent: RGB, and RGBA for a sprite, transparent where the sprite does
// not cover (ComfyUI's mask).
Image inputImage(const SourcePicture& source)
{
    Image image;
    image.width = source.width;
    image.height = source.height;
    const std::vector<uint8_t> rgb = inputRgb(source);
    if (source.kind != CDMDPI_PICTURE_SPRITE || source.own.empty()) {
        image.pixels = rgb;
        return image;
    }
    image.channels = 4;
    for (std::size_t index = 0; index < source.own.size(); ++index) {
        image.pixels.insert(image.pixels.end(), rgb.begin() + static_cast<long>(index * 3),
                            rgb.begin() + static_cast<long>(index * 3 + 3));
        image.pixels.push_back(source.own[index] ? 255 : 0);
    }
    return image;
}

}  // namespace

std::vector<Proposal> runWorkflow(const ComfyClient& client,
                                  const Workflow& workflow,
                                  const std::vector<SourcePicture>& sources,
                                  const Options& options,
                                  const std::string& runName,
                                  const RunHooks& hooks)
{
    if (sources.empty()) {
        throw PluginError("There is nothing to send to ComfyUI.");
    }
    client.check();
    const std::vector<std::vector<std::size_t>> clips =
        sequencesOf(sources, workflow.takesClips ? options.runLength : 1);
    const uint32_t total = static_cast<uint32_t>(sources.size());
    uint32_t finished = 0;
    std::vector<Proposal> proposals(sources.size());
    for (const std::vector<std::size_t>& clip : clips) {
        std::vector<std::string> names;
        for (std::size_t index : clip) {
            const SourcePicture& source = sources[index];
            names.push_back(client.upload(kUploadPrefix + runName + "_" + pictureId(source) +
                                              ".png",
                                          encodePng(inputImage(source))));
        }
        const SourcePicture& first = sources[clip.front()];
        Placeholders placeholders;
        placeholders.seed = options.seed;
        placeholders.sdWidth = first.width;
        placeholders.sdHeight = first.height;
        placeholders.width = first.width * 2;
        placeholders.height = first.height * 2;
        placeholders.count = static_cast<uint32_t>(clip.size());
        const std::string id =
            client.queue(preparePrompt(workflow, names, placeholders), "colorizingdmd-" + runName);

        PromptState state;
        for (;;) {
            if (hooks.cancelled && hooks.cancelled()) {
                client.cancel(id);
                throw PluginError("The ComfyUI run was cancelled.");
            }
            state = client.state(id, workflow.outputNode);
            if (state.finished) {
                break;
            }
            std::this_thread::sleep_for(hooks.pollInterval);
        }
        if (state.failed) {
            throw ComfyError(state.error);
        }
        if (state.images.size() != clip.size()) {
            throw PluginError("The workflow's \"" + std::string(kOutputTitle) + "\" node gave " +
                              std::to_string(state.images.size()) + " picture(s) for " +
                              std::to_string(clip.size()) + " sent.");
        }
        for (std::size_t at = 0; at < clip.size(); ++at) {
            const SourcePicture& source = sources[clip[at]];
            Image result;
            try {
                result = areaResize(decodeRgb(client.download(state.images[at])),
                                    source.width * 2, source.height * 2);
                proposals[clip[at]] = makeProposal(source, result.pixels, result.width,
                                                   result.height, options.colors);
            } catch (const std::invalid_argument& error) {
                throw PluginError("The result for " + pictureId(source) + " is not usable: " +
                                  error.what());
            }
        }
        finished += static_cast<uint32_t>(clip.size());
        if (hooks.progress) {
            hooks.progress(finished, total);
        }
    }
    return proposals;
}

}  // namespace comfy
