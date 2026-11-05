#pragma once

// The pictures gathered from the editor (on its main thread), and how they go out as clips.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ColorizingDMDPlugin.h"
#include "Pictures.h"

namespace comfy {

struct Options {
    // Put into the workflow where it says {seed}.
    int64_t seed = 42;
    ColorMode colors = ColorMode::Neighbourhood;
    // Frames are sent with their background where it shows through; else those pixels take
    // the nearest frame color.
    bool backgroundInInput = true;
    // Dynamic zones are left as they are (libserum colors them at run time anyway).
    bool skipDynamicZones = false;
    // The most consecutive frames sent as one clip, when the workflow takes clips.
    uint32_t runLength = 16;
};

// The pictures cannot be sent, or a run failed or was cancelled; the text says why, for the
// author.
class PluginError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// On the editor's main thread. Each throws PluginError for a project without an HD plane of
// twice the SD plane's size.
[[nodiscard]] std::vector<SourcePicture> gatherFrames(const ColorizingDMDEditorAPI* api,
                                                      const std::vector<uint32_t>& frames,
                                                      const Options& options);
[[nodiscard]] std::vector<SourcePicture> gatherBackgrounds(
    const ColorizingDMDEditorAPI* api,
    const std::vector<uint32_t>& backgrounds);
// Sprites without any pixel in the SD sprite's area are left out.
[[nodiscard]] std::vector<SourcePicture> gatherSprites(const ColorizingDMDEditorAPI* api,
                                                       const std::vector<uint32_t>& sprites);

// The backgrounds and the sprites of the frames, ascending, each once.
[[nodiscard]] std::vector<uint32_t> backgroundsOf(const ColorizingDMDEditorAPI* api,
                                                  const std::vector<uint32_t>& frames);
[[nodiscard]] std::vector<uint32_t> spritesOf(const ColorizingDMDEditorAPI* api,
                                              const std::vector<uint32_t>& frames);

// The pictures as clips, as indices into sources: consecutive frames together, at most
// runLength of them; every background and sprite alone.
[[nodiscard]] std::vector<std::vector<std::size_t>> sequencesOf(
    const std::vector<SourcePicture>& sources,
    uint32_t runLength);

// A short name of the picture, for file names and messages ("f12", "b0", "s3").
[[nodiscard]] std::string pictureId(const SourcePicture& source);

}  // namespace comfy
