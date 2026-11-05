// Tests of the plugin without the editor and without ComfyUI: the pictures, the workflow, the
// gathering on a fake editor API table, and the client and the run against a stub of ComfyUI's
// API in this program.

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "httplib.h"
#include "nlohmann/json.hpp"

#include "ColorizingDMDPlugin.h"
#include "ComfyClient.h"
#include "Gather.h"
#include "Images.h"
#include "Pictures.h"
#include "Run.h"
#include "Workflow.h"

namespace {

using namespace comfy;
using nlohmann::json;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: comfyui: " << message << '\n';
        std::exit(1);
    }
}

// RGB of a picture of RGB565 colors, doubled (nearest): what the stub service gives back.
std::vector<uint8_t> doubledRgb(const std::vector<uint16_t>& pixels, uint32_t width,
                                uint32_t height)
{
    std::vector<uint8_t> rgb(static_cast<std::size_t>(width) * height * 12);
    for (uint32_t y = 0; y < height * 2; ++y) {
        for (uint32_t x = 0; x < width * 2; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * width * 2 + x) * 3;
            fromRgb565(pixels[(y / 2) * width + x / 2], rgb[at], rgb[at + 1], rgb[at + 2]);
        }
    }
    return rgb;
}

std::vector<uint16_t> pattern(std::size_t count, uint16_t base)
{
    std::vector<uint16_t> pixels(count);
    for (std::size_t index = 0; index < count; ++index) {
        pixels[index] = static_cast<uint16_t>(base + (index % 7) * 0x0841);
    }
    return pixels;
}

void testColors()
{
    bool exact = true;
    for (uint32_t color = 0; color <= 0xffff; ++color) {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        fromRgb565(static_cast<uint16_t>(color), r, g, b);
        exact = exact && toRgb565(r, g, b) == color;
    }
    expect(exact, "every RGB565 color comes back exactly from 8 bits");
    expect(toRgb565(255, 255, 255) == 0xffff && toRgb565(4, 2, 3) == 0 &&
               toRgb565(5, 0, 0) == 0x0800,
           "8 bits round to the nearest level");
    expect(colorModeFromName(colorModeName(ColorMode::Neighbourhood)) ==
                   ColorMode::Neighbourhood &&
               colorModeFromName("x") == ColorMode::Any,
           "color mode names");
}

SourcePicture smallSource()
{
    // 4x2, the right half not the picture's own (background).
    SourcePicture source;
    source.kind = CDMDPI_PICTURE_FRAME;
    source.index = 3;
    source.width = 4;
    source.height = 2;
    source.pixels = {0xf800, 0x07e0, 0x0001, 0x0002, 0x001f, 0xffff, 0x0003, 0x0004};
    source.own = {1, 1, 0, 0, 1, 1, 0, 0};
    source.targetWidth = 8;
    source.targetHeight = 4;
    return source;
}

void testInputAndProposals()
{
    SourcePicture source = smallSource();
    std::vector<uint8_t> rgb = inputRgb(source);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    fromRgb565(0x07e0, r, g, b);
    expect(rgb.size() == 24 && rgb[2 * 3 + 1] == g && rgb[3 * 3 + 1] == g,
           "pixels not its own take the nearest own color");
    source.context = {0, 0, 0x1234, 0x1234, 0, 0, 0x1234, 0x1234};
    rgb = inputRgb(source);
    fromRgb565(0x1234, r, g, b);
    expect(rgb[2 * 3] == r && rgb[7 * 3 + 2] == b, "or the background when it is given");

    // The result written only where the picture is its own (write from the own pixels).
    source.write.assign(32, 0);
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            source.write[y * 8 + x] = 1;
        }
    }
    std::vector<uint8_t> result = doubledRgb(source.pixels, 4, 2);
    Proposal proposal = makeProposal(source, result, 8, 4, ColorMode::Any);
    expect(proposal.kind == CDMDPI_PICTURE_FRAME && proposal.index == 3 &&
               proposal.pixels.size() == 32 && proposal.pixels[1 * 8 + 3] == 0x07e0 &&
               proposal.write[1 * 8 + 3] == 1 && proposal.write[0 * 8 + 5] == 0 &&
               proposal.pixels[0 * 8 + 5] == 0,
           "a doubled result comes back exactly, where it may be written");

    // A color between red and green: Any rounds it, the color modes take one of the source's.
    result[(0 * 8 + 1) * 3] = 200;
    result[(0 * 8 + 1) * 3 + 1] = 30;
    result[(0 * 8 + 1) * 3 + 2] = 0;
    expect(makeProposal(source, result, 8, 4, ColorMode::Any).pixels[1] == toRgb565(200, 30, 0),
           "any color: rounded");
    expect(makeProposal(source, result, 8, 4, ColorMode::Picture).pixels[1] == 0xf800 &&
               makeProposal(source, result, 8, 4, ColorMode::Neighbourhood).pixels[1] == 0xf800,
           "the picture's or the neighbours' nearest color");
    // Blue at HD (3, 0), over SD pixel (1, 0): its neighbours include 0x001f diagonally below.
    result[(0 * 8 + 3) * 3] = 0;
    result[(0 * 8 + 3) * 3 + 1] = 0;
    result[(0 * 8 + 3) * 3 + 2] = 255;
    expect(makeProposal(source, result, 8, 4, ColorMode::Neighbourhood).pixels[3] == 0x001f,
           "the neighbours include the diagonal ones");

    bool refused = false;
    try {
        static_cast<void>(makeProposal(source, std::vector<uint8_t>(10), 3, 1, ColorMode::Any));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, "a result of another size is refused");
}

// ---- A fake editor -----------------------------------------------------------------------
// Three frames of 128x32 with an HD plane; frame 1 shows background 0 on its right half and
// has no HD yet; frame 2 has HD and dynamic zones. Sprite 0 is a 3x2 block at (10, 4).

struct FakeProject {
    bool hd = true;
    std::vector<std::vector<uint16_t>> frames;
    std::vector<uint16_t> background;
};
FakeProject g_fake;

template <typename T>
uint32_t give(const std::vector<T>& values, T* buffer, uint32_t capacity)
{
    if (buffer && capacity >= values.size()) {
        std::copy(values.begin(), values.end(), buffer);
    }
    return static_cast<uint32_t>(values.size());
}

std::vector<uint8_t> rightHalf(uint32_t width, uint32_t height)
{
    std::vector<uint8_t> mask(static_cast<std::size_t>(width) * height, 0);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = width / 2; x < width; ++x) {
            mask[y * width + x] = 1;
        }
    }
    return mask;
}

std::vector<uint8_t> spriteShape(bool hd)
{
    std::vector<uint8_t> shape(CDMDPI_SPRITE_WIDTH * CDMDPI_SPRITE_HEIGHT, 0);
    const uint32_t scale = hd ? 2 : 1;
    for (uint32_t y = 4 * scale; y < 6 * scale; ++y) {
        for (uint32_t x = 10 * scale; x < 13 * scale; ++x) {
            shape[y * CDMDPI_SPRITE_WIDTH + x] = 1;
        }
    }
    return shape;
}

ColorizingDMDEditorAPI fakeApi()
{
    ColorizingDMDEditorAPI api{};
    api.version = 3;
    api.GetProjectInfo = [](ColorizingDMDProjectInfo* info) -> int {
        *info = ColorizingDMDProjectInfo{};
        info->hasProject = 1;
        info->frameCount = static_cast<uint32_t>(g_fake.frames.size());
        info->sdWidth = 128;
        info->sdHeight = 32;
        info->hdWidth = g_fake.hd ? 256 : 0;
        info->hdHeight = g_fake.hd ? 64 : 0;
        info->backgroundCount = 1;
        info->spriteCount = 1;
        return 1;
    };
    api.GetFrameInfo = [](uint32_t frame, ColorizingDMDFrameInfo* info) -> int {
        if (frame >= g_fake.frames.size()) {
            return 0;
        }
        *info = ColorizingDMDFrameInfo{};
        info->background = frame == 1 ? 0 : CDMDPI_NONE;
        info->hasHd = frame == 2 ? 1 : 0;
        info->hasDynamicPixels = frame == 2 ? 1 : 0;
        return 1;
    };
    api.GetColorizedFrame = [](uint32_t frame, int hd, uint16_t* pixels, uint32_t capacity,
                               uint32_t*, uint32_t*) -> uint32_t {
        return frame < g_fake.frames.size() && !hd ? give(g_fake.frames[frame], pixels, capacity)
                                                   : 0;
    };
    api.GetBackgroundMask = [](uint32_t frame, int hd, uint8_t* pixels, uint32_t capacity,
                               uint32_t*, uint32_t*) -> uint32_t {
        const uint32_t width = hd ? 256 : 128;
        const uint32_t height = hd ? 64 : 32;
        return give(frame == 1 ? rightHalf(width, height)
                               : std::vector<uint8_t>(static_cast<std::size_t>(width) * height),
                    pixels, capacity);
    };
    api.GetBackgroundPicture = [](uint32_t background, int hd, uint16_t* pixels,
                                  uint32_t capacity, uint32_t*, uint32_t*) -> uint32_t {
        return background == 0 && !hd ? give(g_fake.background, pixels, capacity) : 0;
    };
    api.GetDynamicMask = [](uint32_t, uint8_t* pixels, uint32_t capacity, uint32_t*,
                            uint32_t*) -> uint32_t {
        // SD dynamic plane: pixel (0, 0) is in set 0.
        std::vector<uint8_t> zones(128 * 32, 255);
        zones[0] = 0;
        return give(zones, pixels, capacity);
    };
    api.GetFrameSprites = [](uint32_t frame, uint32_t* sprites, uint32_t capacity) -> uint32_t {
        return give(frame == 1 ? std::vector<uint32_t>{0} : std::vector<uint32_t>{}, sprites,
                    capacity);
    };
    api.GetSpritePicture = [](uint32_t sprite, int hd, uint16_t* pixels, uint32_t capacity,
                              uint32_t*, uint32_t*) -> uint32_t {
        return sprite == 0 && !hd
                   ? give(pattern(CDMDPI_SPRITE_WIDTH * CDMDPI_SPRITE_HEIGHT, 0x2000), pixels,
                          capacity)
                   : 0;
    };
    api.GetSpriteMask = [](uint32_t sprite, int hd, uint8_t* pixels, uint32_t capacity,
                           uint32_t*, uint32_t*) -> uint32_t {
        // No HD sprite yet.
        return sprite == 0 && !hd ? give(spriteShape(false), pixels, capacity) : 0;
    };
    return api;
}

void testGathering()
{
    g_fake.frames = {pattern(128 * 32, 0x1000), pattern(128 * 32, 0x3000),
                     pattern(128 * 32, 0x5000)};
    g_fake.background = std::vector<uint16_t>(128 * 32, 0x0f0f);
    const ColorizingDMDEditorAPI api = fakeApi();
    Options options;
    std::vector<SourcePicture> sources = gatherFrames(&api, {0, 1, 2, 7}, options);
    expect(sources.size() == 3 && sources[0].own.empty() && sources[0].write.empty() &&
               sources[0].targetWidth == 256,
           "a frame without background: all its own, written everywhere");
    const SourcePicture& withBackground = sources[1];
    expect(withBackground.own.size() == 128 * 32 && withBackground.own[5] == 1 &&
               withBackground.own[100] == 0 && withBackground.context.size() == 128 * 32 &&
               withBackground.context[100] == 0x0f0f,
           "a frame with background: its own pixels, the background as context");
    expect(withBackground.write.size() == 256 * 64 && withBackground.write[10] == 1 &&
               withBackground.write[200] == 0 && withBackground.write[63 * 256 + 255] == 0,
           "written where its doubled mask shows the frame");
    options.backgroundInInput = false;
    options.skipDynamicZones = true;
    sources = gatherFrames(&api, {1, 2}, options);
    expect(sources[0].context.empty(), "without the background in the input");
    expect(sources[1].write.size() == 256 * 64 && sources[1].write[0] == 0 &&
               sources[1].write[1] == 0 && sources[1].write[256] == 0 &&
               sources[1].write[2] == 1,
           "dynamic zones left out, doubled from the SD dynamic plane");

    const std::vector<SourcePicture> backgrounds =
        gatherBackgrounds(&api, backgroundsOf(&api, {0, 1, 2}));
    expect(backgrounds.size() == 1 && backgrounds[0].kind == CDMDPI_PICTURE_BACKGROUND &&
               backgrounds[0].pixels[0] == 0x0f0f && backgrounds[0].write.empty(),
           "the frames' background");

    const std::vector<SourcePicture> sprites = gatherSprites(&api, spritesOf(&api, {0, 1, 2}));
    expect(sprites.size() == 1 && sprites[0].kind == CDMDPI_PICTURE_SPRITE &&
               sprites[0].width == 3 && sprites[0].height == 2 && sprites[0].offsetX == 20 &&
               sprites[0].offsetY == 8 &&
               sprites[0].pixels[0] == pattern(256 * 64, 0x2000)[4 * 256 + 10],
           "the frames' sprite, the part it covers");
    expect(sprites[0].write.size() == 256 * 64 && sprites[0].write[8 * 256 + 20] == 1 &&
               sprites[0].write[11 * 256 + 25] == 1 && sprites[0].write[12 * 256 + 20] == 0,
           "written where the doubled sprite covers");

    g_fake.hd = false;
    bool refused = false;
    try {
        static_cast<void>(gatherFrames(&api, {0}, options));
    } catch (const PluginError&) {
        refused = true;
    }
    expect(refused, "a project without HD is refused");
    g_fake.hd = true;

    SourcePicture frame;
    frame.kind = CDMDPI_PICTURE_FRAME;
    std::vector<SourcePicture> list;
    for (uint32_t index : {4u, 5u, 6u, 9u, 10u}) {
        frame.index = index;
        list.push_back(frame);
    }
    SourcePicture background;
    background.kind = CDMDPI_PICTURE_BACKGROUND;
    background.index = 11;
    list.push_back(background);
    const auto sequences = sequencesOf(list, 2);
    expect(sequences.size() == 4 && sequences[0] == std::vector<std::size_t>{0, 1} &&
               sequences[1] == std::vector<std::size_t>{2} &&
               sequences[2] == std::vector<std::size_t>{3, 4} &&
               sequences[3] == std::vector<std::size_t>{5} && pictureId(list[5]) == "b11",
           "clips of consecutive frames, at most the run length; a background alone");
}

void testImages()
{
    Image rgb{3, 2, 3, {}};
    for (uint8_t value = 0; value < 18; ++value) {
        rgb.pixels.push_back(static_cast<uint8_t>(value * 13));
    }
    const std::string png = encodePng(rgb);
    expect(png.compare(1, 3, "PNG") == 0 && static_cast<uint8_t>(png[25]) == 2,
           "an RGB picture as PNG");
    const Image back = decodeRgb(png);
    expect(back.width == 3 && back.height == 2 && back.pixels == rgb.pixels,
           "a PNG read back exactly");

    Image rgba{1, 1, 4, {10, 20, 30, 0}};
    const std::string withAlpha = encodePng(rgba);
    expect(static_cast<uint8_t>(withAlpha[25]) == 6 &&
               decodeRgb(withAlpha).pixels == std::vector<uint8_t>{10, 20, 30},
           "an RGBA picture as PNG; its alpha dropped when read");

    bool refused = false;
    try {
        static_cast<void>(decodeRgb("not a picture"));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    expect(refused, "data that is not a picture is refused");

    // Down by a whole multiple: block averages; up: repeated pixels; in between: weighted.
    const Image two{2, 1, 3, {0, 0, 0, 255, 100, 51}};
    expect(areaResize(two, 1, 1).pixels == std::vector<uint8_t>{128, 50, 26},
           "two pixels averaged into one");
    expect(areaResize(two, 4, 2).pixels ==
               std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 255, 100, 51, 255, 100, 51,
                                    0, 0, 0, 0, 0, 0, 255, 100, 51, 255, 100, 51},
           "each pixel repeated upwards");
    const Image three{3, 1, 3, {0, 0, 0, 90, 90, 90, 180, 180, 180}};
    expect(areaResize(three, 2, 1).pixels == std::vector<uint8_t>{30, 30, 30, 150, 150, 150},
           "a pixel split between two");
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

const char* const kWorkflow = R"({
  "1": {"class_type": "LoadImage", "inputs": {"image": "x.png"},
        "_meta": {"title": "CDMD Input"}},
  "2": {"class_type": "ImageScaleBy",
        "inputs": {"image": ["1", 0], "upscale_method": "nearest-exact", "scale_by": 4.0}},
  "3": {"class_type": "SaveImage", "inputs": {"images": ["2", 0], "filename_prefix": "r"},
        "_meta": {"title": "CDMD Output"}},
  "4": {"class_type": "Note", "inputs": {"seed": "{seed}",
        "text": "{width}x{height} from {sdwidth}x{sdheight}, {count}, {other}"}}
})";

bool refusesWith(const std::string& text, const std::string& part)
{
    try {
        static_cast<void>(parseWorkflow(text));
    } catch (const PluginError& error) {
        return std::string(error.what()).find(part) != std::string::npos;
    }
    return false;
}

void testWorkflow()
{
    const Workflow single = parseWorkflow(kWorkflow);
    expect(single.inputNode == "1" && single.outputNode == "3" && !single.takesClips,
           "the titled nodes of a workflow");
    Placeholders placeholders;
    placeholders.seed = 7;
    placeholders.width = 256;
    placeholders.height = 64;
    placeholders.sdWidth = 128;
    placeholders.sdHeight = 32;
    placeholders.count = 1;
    const nlohmann::json prompt = preparePrompt(single, {"cdmd_f1.png"}, placeholders);
    expect(prompt["1"]["inputs"]["image"] == "cdmd_f1.png" && prompt["4"]["inputs"]["seed"] == 7 &&
               prompt["4"]["inputs"]["text"] == "256x64 from 128x32, 1, {other}" &&
               prompt["2"]["inputs"]["scale_by"] == 4.0,
           "the picture's name and the placeholders filled in");
    bool refused = false;
    try {
        static_cast<void>(preparePrompt(single, {"a.png", "b.png"}, placeholders));
    } catch (const PluginError&) {
        refused = true;
    }
    expect(refused, "Load Image takes one picture");

    const Workflow clip =
        parseWorkflow(readFile(std::string(COMFYUI_EXAMPLES_DIR) + "/clip_nearest_4x_api.json"));
    expect(clip.takesClips &&
               preparePrompt(clip, {"a.png", "b.png"}, placeholders)["1"]["inputs"]["images"] ==
                   "a.png\nb.png",
           "the clip node takes the names one a line");
    expect(!parseWorkflow(readFile(std::string(COMFYUI_EXAMPLES_DIR) + "/nearest_4x_api.json"))
                .takesClips,
           "the example workflow");

    expect(refusesWith("{", "not JSON"), "not JSON");
    expect(refusesWith(R"({"nodes": [], "links": []})", "API format"),
           "the editor format instead of the API format");
    expect(refusesWith(R"({"1": {"class_type": "LoadImage", "inputs": {},
                         "_meta": {"title": "CDMD Input"}}})",
                       "CDMD Output"),
           "no output node");
    expect(refusesWith(R"({"1": {"class_type": "VAEDecode", "inputs": {},
                         "_meta": {"title": "CDMD Input"}},
                         "2": {"class_type": "SaveImage", "inputs": {},
                         "_meta": {"title": "CDMD Output"}}})",
                       "VAEDecode"),
           "an input node of another class");
    expect(refusesWith(R"({"1": {"class_type": "LoadImage", "inputs": {},
                         "_meta": {"title": "CDMD Input"}},
                         "2": {"class_type": "LoadImage", "inputs": {},
                         "_meta": {"title": "CDMD Input"}}})",
                       "more than one"),
           "two input nodes");
}

void testAnswers()
{
    const std::string refusal = describeRefusal(R"({
        "error": {"type": "prompt_outputs_failed_validation",
                  "message": "Prompt outputs failed validation", "details": ""},
        "node_errors": {"2": {"class_type": "SeedVR2", "errors": [
            {"type": "value_not_in_list", "message": "Value not in list",
             "details": "model: 'x' not in [...]"}]}}})");
    expect(refusal.find("Prompt outputs failed validation") != std::string::npos &&
               refusal.find("Node 2 (SeedVR2): Value not in list: model: 'x'") !=
                   std::string::npos,
           "ComfyUI's reasons for refusing a prompt");

    expect(!parseHistory("{}", "p1", "3").finished, "a prompt still running has no history");
    const PromptState done = parseHistory(R"({"p1": {"status": {"status_str": "success",
        "completed": true, "messages": []}, "outputs": {"3": {"images": [
        {"filename": "r_00001_.png", "subfolder": "", "type": "output"},
        {"filename": "r_00002_.png", "subfolder": "a", "type": "output"}]},
        "9": {"images": [{"filename": "other.png"}]}}}})",
                                          "p1", "3");
    expect(done.finished && !done.failed && done.images.size() == 2 &&
               done.images[1].subfolder == "a",
           "the output node's pictures, in order");
    const PromptState failed = parseHistory(R"({"p1": {"status": {"status_str": "error",
        "completed": false, "messages": [["execution_start", {}], ["execution_error",
        {"node_id": "2", "node_type": "SeedVR2", "exception_message": "CUDA out of memory"}]]},
        "outputs": {}}})",
                                            "p1", "3");
    expect(failed.finished && failed.failed &&
               failed.error.find("node 2 (SeedVR2)") != std::string::npos &&
               failed.error.find("CUDA out of memory") != std::string::npos,
           "a failed prompt, with the node and its exception");
}

// ---- A stub of ComfyUI's API in this program ---------------------------------------------
// It runs a prompt as the test workflows mean it: the pictures of the input node enlarged by
// the scale_by of the ImageScaleBy node (nearest), one output picture each.

class StubComfy {
public:
    StubComfy()
    {
        m_server.Get("/system_stats", [](const httplib::Request&, httplib::Response& response) {
            response.set_content(R"({"system": {"comfyui_version": "stub"}})",
                                 "application/json");
        });
        m_server.Post("/upload/image", [this](const httplib::Request& request,
                                              httplib::Response& response) {
            const httplib::MultipartFormData file = request.get_file_value("image");
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_inputs[file.filename] = file.content;
            response.set_content(
                nlohmann::json{{"name", file.filename}, {"subfolder", ""}, {"type", "input"}}
                    .dump(),
                "application/json");
        });
        m_server.Post("/prompt", [this](const httplib::Request& request,
                                        httplib::Response& response) {
            const nlohmann::json prompt = nlohmann::json::parse(request.body)["prompt"];
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_prompts.push_back(prompt);
            if (m_refuse) {
                response.status = 400;
                response.set_content(
                    R"({"error": {"message": "Prompt outputs failed validation"},
                        "node_errors": {"2": {"class_type": "ImageScaleBy",
                        "errors": [{"message": "Value too big"}]}}})",
                    "application/json");
                return;
            }
            const std::string id = "p" + std::to_string(m_prompts.size());
            response.set_content(nlohmann::json{{"prompt_id", id}, {"number", 0}}.dump(),
                                 "application/json");
            m_history[id] = run(prompt);
        });
        m_server.Get(R"(/history/(\w+))", [this](const httplib::Request& request,
                                                 httplib::Response& response) {
            const std::lock_guard<std::mutex> lock(m_mutex);
            const std::string id = request.matches[1];
            // The first look finds it still running.
            if (!m_history.contains(id) || m_looked.insert(id).second) {
                response.set_content("{}", "application/json");
                return;
            }
            response.set_content(nlohmann::json{{id, m_history[id]}}.dump(), "application/json");
        });
        m_server.Get("/view", [this](const httplib::Request& request,
                                     httplib::Response& response) {
            const std::lock_guard<std::mutex> lock(m_mutex);
            const auto output = m_outputs.find(request.get_param_value("filename"));
            if (output == m_outputs.end() || request.get_param_value("type") != "output") {
                response.status = 404;
                return;
            }
            response.set_content(output->second, "image/png");
        });
        m_server.Post("/queue", [this](const httplib::Request&, httplib::Response& response) {
            ++m_cancels;
            response.set_content("{}", "application/json");
        });
        m_server.Post("/interrupt", [](const httplib::Request&, httplib::Response& response) {
            response.set_content("{}", "application/json");
        });
        m_port = m_server.bind_to_any_port("127.0.0.1");
        m_thread = std::thread([this]() { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }
    ~StubComfy()
    {
        m_server.stop();
        m_thread.join();
    }

    [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(m_port); }
    [[nodiscard]] std::vector<nlohmann::json> prompts()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_prompts;
    }
    [[nodiscard]] std::string input(const std::string& name)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_inputs[name];
    }
    [[nodiscard]] int cancels() const { return m_cancels; }
    void setRefuse(bool refuse) { m_refuse = refuse; }
    void setFail(bool fail) { m_fail = fail; }
    // Each prompt gives one picture fewer than it gets.
    void setDropOne(bool drop) { m_dropOne = drop; }

private:
    nlohmann::json run(const nlohmann::json& prompt)
    {
        if (m_fail) {
            return {{"status",
                     {{"status_str", "error"},
                      {"completed", false},
                      // An array of [name, data] pairs (braces alone would make an object).
                      {"messages",
                       nlohmann::json::array({nlohmann::json::array(
                           {"execution_error",
                            {{"node_id", "2"},
                             {"node_type", "ImageScaleBy"},
                             {"exception_message", "out of memory"}}})})}}},
                    {"outputs", nlohmann::json::object()}};
        }
        std::vector<std::string> names;
        uint32_t scale = 1;
        std::string output;
        for (auto node = prompt.begin(); node != prompt.end(); ++node) {
            const std::string kind = (*node)["class_type"];
            const nlohmann::json& inputs = (*node)["inputs"];
            if (kind == "LoadImage") {
                names.push_back(inputs["image"]);
            } else if (kind == kClipClass) {
                std::string list = inputs["images"];
                for (std::size_t start = 0; start <= list.size();) {
                    const std::size_t end = std::min(list.find('\n', start), list.size());
                    names.push_back(list.substr(start, end - start));
                    start = end + 1;
                }
            } else if (kind == "ImageScaleBy") {
                scale = static_cast<uint32_t>(inputs["scale_by"].get<double>());
            } else if (kind == "SaveImage") {
                output = node.key();
            }
        }
        if (m_dropOne) {
            names.pop_back();
        }
        nlohmann::json images = nlohmann::json::array();
        for (const std::string& name : names) {
            const Image picture = decodeRgb(m_inputs[name]);
            Image larger{picture.width * scale, picture.height * scale, 3, {}};
            for (uint32_t y = 0; y < larger.height; ++y) {
                for (uint32_t x = 0; x < larger.width; ++x) {
                    const std::size_t at = ((y / scale) * picture.width + x / scale) * 3;
                    larger.pixels.insert(larger.pixels.end(), picture.pixels.begin() + at,
                                         picture.pixels.begin() + at + 3);
                }
            }
            const std::string file = "r_" + std::to_string(m_outputs.size()) + ".png";
            m_outputs[file] = encodePng(larger);
            images.push_back({{"filename", file}, {"subfolder", ""}, {"type", "output"}});
        }
        return {{"status", {{"status_str", "success"}, {"completed", true}}},
                {"outputs", {{output, {{"images", images}}}}}};
    }

    httplib::Server m_server;
    std::thread m_thread;
    int m_port = 0;
    std::mutex m_mutex;
    std::map<std::string, std::string> m_inputs;
    std::map<std::string, std::string> m_outputs;
    std::vector<nlohmann::json> m_prompts;
    std::map<std::string, nlohmann::json> m_history;
    std::set<std::string> m_looked;
    std::atomic<int> m_cancels{0};
    bool m_refuse = false;
    bool m_fail = false;
    bool m_dropOne = false;
};

std::vector<SourcePicture> framesToSend(uint32_t count)
{
    std::vector<SourcePicture> sources;
    for (uint32_t index = 0; index < count; ++index) {
        SourcePicture source;
        source.kind = CDMDPI_PICTURE_FRAME;
        source.index = 10 + index;
        source.width = 128;
        source.height = 32;
        source.pixels = pattern(128 * 32, static_cast<uint16_t>(0x0800 * (index + 1)));
        source.targetWidth = 256;
        source.targetHeight = 64;
        sources.push_back(std::move(source));
    }
    return sources;
}

// Each frame comes back doubled, exactly.
void checkDoubled(const std::vector<SourcePicture>& sources,
                  const std::vector<Proposal>& proposals, const std::string& what)
{
    bool exact = proposals.size() == sources.size();
    for (std::size_t index = 0; exact && index < proposals.size(); ++index) {
        const Proposal& proposal = proposals[index];
        exact = proposal.index == sources[index].index && proposal.pixels.size() == 256 * 64;
        for (uint32_t y = 0; exact && y < 64; ++y) {
            for (uint32_t x = 0; exact && x < 256; ++x) {
                exact = proposal.pixels[y * 256 + x] ==
                            sources[index].pixels[(y / 2) * 128 + x / 2] &&
                        proposal.write[y * 256 + x] == 1;
            }
        }
    }
    expect(exact, what);
}

template <typename Error>
std::string failureOf(const std::function<void()>& work)
{
    try {
        work();
    } catch (const Error& error) {
        return error.what();
    }
    return {};
}

void testRun()
{
    StubComfy comfy;
    const ComfyClient client(comfy.url(), "");
    RunHooks hooks;
    hooks.pollInterval = std::chrono::milliseconds(1);
    uint32_t lastDone = 0;
    hooks.progress = [&lastDone](uint32_t done, uint32_t) { lastDone = done; };
    Options options;
    options.runLength = 3;
    options.seed = 5;

    // Load Image: one prompt a picture; 4x by the workflow, back to 2x.
    const Workflow single = parseWorkflow(kWorkflow);
    const std::vector<SourcePicture> sources = framesToSend(4);
    checkDoubled(sources, runWorkflow(client, single, sources, options, "t1", hooks),
                 "the frames come back doubled, exactly");
    std::vector<nlohmann::json> prompts = comfy.prompts();
    expect(prompts.size() == 4 && lastDone == 4 &&
               prompts[1]["1"]["inputs"]["image"] == "cdmd_t1_f11.png" &&
               prompts[1]["4"]["inputs"]["seed"] == 5,
           "one prompt a picture, with its upload and the seed");

    // The clip node: consecutive frames together, at most the run length.
    const Workflow clip =
        parseWorkflow(readFile(std::string(COMFYUI_EXAMPLES_DIR) + "/clip_nearest_4x_api.json"));
    std::vector<SourcePicture> clipSources = framesToSend(5);
    clipSources[4].index = 20;
    checkDoubled(clipSources, runWorkflow(client, clip, clipSources, options, "t2", hooks),
                 "clips come back doubled, exactly, in order");
    prompts = comfy.prompts();
    expect(prompts.size() == 4 + 3 &&
               prompts[4]["1"]["inputs"]["images"] ==
                   "cdmd_t2_f10.png\ncdmd_t2_f11.png\ncdmd_t2_f12.png" &&
               prompts[5]["1"]["inputs"]["images"] == "cdmd_t2_f13.png" &&
               prompts[6]["1"]["inputs"]["images"] == "cdmd_t2_f20.png",
           "clips of consecutive frames, at most the run length");

    // A sprite goes as RGBA, transparent where it does not cover, and comes back in place.
    SourcePicture sprite;
    sprite.kind = CDMDPI_PICTURE_SPRITE;
    sprite.index = 0;
    sprite.width = 3;
    sprite.height = 2;
    sprite.pixels = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0841, 0x1082};
    sprite.own = {1, 1, 1, 1, 0, 1};
    sprite.targetWidth = 256;
    sprite.targetHeight = 64;
    sprite.offsetX = 20;
    sprite.offsetY = 8;
    const std::vector<Proposal> proposals =
        runWorkflow(client, single, {sprite}, options, "t3", hooks);
    const std::string upload = comfy.input("cdmd_t3_s0.png");
    expect(upload.size() > 26 && static_cast<uint8_t>(upload[25]) == 6,
           "a sprite is uploaded with alpha");
    expect(proposals.size() == 1 && proposals[0].pixels[8 * 256 + 20] == 0xf800 &&
               proposals[0].pixels[11 * 256 + 25] == 0x1082 && proposals[0].write[0] == 0,
           "a sprite comes back where it lies in the HD sprite");

    hooks.cancelled = []() { return true; };
    expect(failureOf<PluginError>([&]() {
               static_cast<void>(runWorkflow(client, single, framesToSend(1), options, "t4",
                                             hooks));
           }).find("cancelled") != std::string::npos &&
               comfy.cancels() == 1,
           "a cancelled run takes its prompt out of ComfyUI's queue");
    hooks.cancelled = nullptr;

    comfy.setDropOne(true);
    expect(failureOf<PluginError>([&]() {
               static_cast<void>(runWorkflow(client, clip, framesToSend(2), options, "t5",
                                             hooks));
           }).find("gave 1 picture(s) for 2") != std::string::npos,
           "a workflow that gives fewer pictures than it gets");
    comfy.setDropOne(false);

    comfy.setFail(true);
    expect(failureOf<ComfyError>([&]() {
               static_cast<void>(runWorkflow(client, single, framesToSend(1), options, "t6",
                                             hooks));
           }).find("out of memory") != std::string::npos,
           "a failed prompt fails the run with ComfyUI's reason");
    comfy.setFail(false);

    comfy.setRefuse(true);
    expect(failureOf<ComfyError>([&]() {
               static_cast<void>(runWorkflow(client, single, framesToSend(1), options, "t7",
                                             hooks));
           }).find("Node 2 (ImageScaleBy): Value too big") != std::string::npos,
           "a refused prompt fails the run with ComfyUI's reasons");
    comfy.setRefuse(false);

    expect(failureOf<ComfyError>([]() { ComfyClient("http://127.0.0.1:1", "", 2).check(); })
                   .find("does not answer") != std::string::npos,
           "ComfyUI not running");
}

}  // namespace

int main()
{
    testColors();
    testInputAndProposals();
    testGathering();
    testImages();
    testWorkflow();
    testAnswers();
    testRun();
    std::cout << "comfyui_tests passed\n";
    return 0;
}
