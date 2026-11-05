// The plugin's entry points: loaded by the ColorizingDMD editor from its plugin folder (plugin
// id "ComfyUI", see plugin.cfg). The menu entries read the workflow and gather the pictures on
// the editor's main thread; a worker thread runs them through ComfyUI and hands the results to
// the editor's review window.

#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ColorizingDMDPlugin.h"
#include "plugins/LoggingPlugin.h"
#include "plugins/MsgPlugin.h"

#include "Run.h"

namespace {

const char* const kName = "ComfyUI";
const char* const kDefaultUrl = "http://127.0.0.1:8188";

const MsgPluginAPI* g_msgApi = nullptr;
uint32_t g_endpoint = 0;
const ColorizingDMDEditorAPI* g_editorApi = nullptr;
LoggingPluginAPI* g_logging = nullptr;

// The run in progress, if any.
std::thread g_worker;
std::atomic<bool> g_running{false};
std::atomic<bool> g_cancel{false};

// Calls waiting for the main thread, and whether the plugin is unloading (see unload).
std::mutex g_jobsMutex;
int g_jobsInFlight = 0;
bool g_stopping = false;

void log(unsigned int level, const std::string& message)
{
    if (g_logging) {
        g_logging->Log(kName, "", 0, level, message.c_str());
    }
}

struct Job {
    const std::function<void()>* work;
    std::exception_ptr error;
};

void MSGPIAPI runJob(void* userData)
{
    Job* job = static_cast<Job*>(userData);
    try {
        (*job->work)();
    } catch (...) {
        job->error = std::current_exception();
    }
}

// On the worker thread: runs the work on the editor's main thread and waits for it.
void runOnMainThread(const std::function<void()>& work)
{
    {
        const std::lock_guard<std::mutex> lock(g_jobsMutex);
        if (g_stopping) {
            throw std::runtime_error("the plugin is unloading");
        }
        ++g_jobsInFlight;
    }
    Job job{&work, nullptr};
    g_msgApi->RunOnMainThread(g_endpoint, -1.0, &runJob, &job);
    {
        const std::lock_guard<std::mutex> lock(g_jobsMutex);
        --g_jobsInFlight;
    }
    if (job.error) {
        std::rethrow_exception(job.error);
    }
}

std::string setting(const char* key, const std::string& fallback = {})
{
    const uint32_t size = g_editorApi->GetSetting(g_endpoint, key, nullptr, 0);
    if (size == 0) {
        return fallback;
    }
    std::string value(size, '\0');
    g_editorApi->GetSetting(g_endpoint, key, value.data(), size);
    value.resize(size - 1);
    return value.empty() ? fallback : value;
}

long long numberSetting(const char* key, long long fallback)
{
    try {
        return std::stoll(setting(key, std::to_string(fallback)));
    } catch (const std::exception&) {
        return fallback;
    }
}

comfy::Options options()
{
    comfy::Options result;
    result.seed = numberSetting("seed", result.seed);
    result.colors = comfy::colorModeFromName(
        setting("colors", comfy::colorModeName(result.colors)));
    result.backgroundInInput = setting("background", "1") == "1";
    result.skipDynamicZones = setting("dynamic", "0") == "1";
    const long long run = numberSetting("run", result.runLength);
    result.runLength = static_cast<uint32_t>(run < 1 ? 1 : (run > 256 ? 256 : run));
    return result;
}

// The workflow file of the settings. Throws comfy::PluginError.
comfy::Workflow workflow()
{
    const std::string path = setting("workflow");
    if (path.empty()) {
        throw comfy::PluginError("No workflow is set. Export the workflow in ComfyUI with "
                                 "Export (API) and enter the file in Plugins -> ComfyUI: "
                                 "Settings.");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw comfy::PluginError("The workflow file " + path + " cannot be read.");
    }
    return comfy::parseWorkflow(
        std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()));
}

void showMessage(const std::string& text)
{
    g_editorApi->ShowMessage(g_endpoint, kName, text.c_str());
}

void showStatus(const std::string& text)
{
    g_editorApi->ShowStatus(g_endpoint, text.c_str());
}

void MSGPIAPI onReviewed(void*, uint32_t applied)
{
    if (g_editorApi) {
        showStatus(applied > 0 ? "ComfyUI: " + std::to_string(applied) + " picture(s) applied"
                               : "ComfyUI: nothing applied");
    }
}

// A name for the run's uploads, different for each run.
std::string runName()
{
    static uint32_t count = 0;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    return std::to_string(now.count()) + "_" + std::to_string(++count);
}

// On the worker thread.
void run(comfy::Workflow flow, std::vector<comfy::SourcePicture> sources,
         comfy::Options settings, std::string url, std::string token)
{
    try {
        const comfy::ComfyClient client(url, token);
        comfy::RunHooks hooks;
        hooks.cancelled = []() { return g_cancel.load(); };
        hooks.progress = [](uint32_t done, uint32_t total) {
            const std::string text = "ComfyUI: " + std::to_string(done) + " of " +
                                     std::to_string(total) + " pictures done";
            runOnMainThread([&text]() { showStatus(text); });
        };
        const std::vector<comfy::Proposal> proposals =
            comfy::runWorkflow(client, flow, sources, settings, runName(), hooks);
        runOnMainThread([&proposals]() {
            std::vector<ColorizingDMDPicture> pictures;
            for (const comfy::Proposal& proposal : proposals) {
                pictures.push_back(ColorizingDMDPicture{
                    proposal.kind, proposal.index, 1, proposal.pixels.data(),
                    proposal.write.data(), static_cast<uint32_t>(proposal.pixels.size()),
                    proposal.label.c_str()});
            }
            if (!g_editorApi->ReviewPictures(g_endpoint, "ComfyUI results",
                                             "Apply ComfyUI Results", pictures.data(),
                                             static_cast<uint32_t>(pictures.size()),
                                             &onReviewed, nullptr)) {
                showMessage("The editor did not take the results: the project was closed, or "
                            "a review of ComfyUI results is still open.");
            }
        });
    } catch (const std::exception& error) {
        const std::string text = error.what();
        log(LPI_LVL_ERROR, text);
        try {
            runOnMainThread([&text]() { showMessage(text); });
        } catch (const std::exception&) {
            // Unloading: nobody to tell.
        }
    }
    g_running = false;
}

// On the main thread: the workflow and what to send, then the run on the worker thread.
void start(const std::function<std::vector<comfy::SourcePicture>(const comfy::Options&)>& gather)
{
    if (g_running) {
        showMessage("A ComfyUI run is still going on; cancel it first "
                    "(Plugins -> ComfyUI: Cancel Run).");
        return;
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
    const comfy::Options settings = options();
    comfy::Workflow flow;
    std::vector<comfy::SourcePicture> sources;
    try {
        flow = workflow();
        sources = gather(settings);
    } catch (const comfy::PluginError& error) {
        showMessage(error.what());
        return;
    }
    if (sources.empty()) {
        showMessage("There is nothing to send to ComfyUI.");
        return;
    }
    g_cancel = false;
    g_running = true;
    showStatus("ComfyUI: sending " + std::to_string(sources.size()) + " pictures");
    g_worker = std::thread(&run, std::move(flow), std::move(sources), settings,
                           setting("url", kDefaultUrl), setting("token"));
}

std::vector<uint32_t> selectedFrames()
{
    std::vector<uint32_t> frames(g_editorApi->GetSelectedFrames(nullptr, 0));
    if (!frames.empty()) {
        g_editorApi->GetSelectedFrames(frames.data(), static_cast<uint32_t>(frames.size()));
    }
    return frames;
}

void MSGPIAPI onFrames(void*)
{
    start([](const comfy::Options& settings) {
        return comfy::gatherFrames(g_editorApi, selectedFrames(), settings);
    });
}

void MSGPIAPI onBackgrounds(void*)
{
    start([](const comfy::Options&) {
        return comfy::gatherBackgrounds(g_editorApi,
                                        comfy::backgroundsOf(g_editorApi, selectedFrames()));
    });
}

void MSGPIAPI onSprites(void*)
{
    start([](const comfy::Options&) {
        return comfy::gatherSprites(g_editorApi, comfy::spritesOf(g_editorApi, selectedFrames()));
    });
}

void MSGPIAPI onCurrentBackground(void*)
{
    start([](const comfy::Options&) {
        const uint32_t background = g_editorApi->GetCurrentBackground();
        if (background == CDMDPI_NONE) {
            throw comfy::PluginError("No background is chosen in the background panel.");
        }
        return comfy::gatherBackgrounds(g_editorApi, {background});
    });
}

void MSGPIAPI onCurrentSprite(void*)
{
    start([](const comfy::Options&) {
        const uint32_t sprite = g_editorApi->GetCurrentSprite();
        if (sprite == CDMDPI_NONE) {
            throw comfy::PluginError("No sprite is chosen in the sprite panel.");
        }
        return comfy::gatherSprites(g_editorApi, {sprite});
    });
}

void MSGPIAPI onCancel(void*)
{
    if (g_running) {
        g_cancel = true;
        showStatus("ComfyUI: cancelling");
    } else {
        showStatus("ComfyUI: no run is going on");
    }
}

void MSGPIAPI onSettingsSaved(void*, int saved)
{
    if (saved && g_editorApi) {
        showStatus("ComfyUI: settings saved");
    }
}

void MSGPIAPI onSettings(void*)
{
    static const ColorizingDMDSettingField kFields[] = {
        {"url", "ComfyUI address", CDMDPI_FIELD_TEXT, kDefaultUrl, nullptr,
         "Where ComfyUI runs, as in the browser's address bar."},
        {"token", "Token", CDMDPI_FIELD_PASSWORD, nullptr, nullptr,
         "Sent as a bearer token, for ComfyUI behind a proxy that asks for one."},
        {"workflow", "Workflow file", CDMDPI_FIELD_TEXT, nullptr, nullptr,
         "The workflow exported in ComfyUI with Export (API), with a node titled \"CDMD Input\" "
         "and one titled \"CDMD Output\"."},
        {"seed", "Seed", CDMDPI_FIELD_TEXT, "42", nullptr,
         "Put into the workflow where it says {seed}."},
        {"colors", "Colors", CDMDPI_FIELD_CHOICE, "neighbourhood",
         "neighbourhood=Colors of the pixels around\npicture=Colors of the picture\n"
         "any=Any color",
         "Which colors a pixel of the result may take: the colors of the source pixels around "
         "it keep edges crisp and invent none."},
        {"background", "Send frames with their background", CDMDPI_FIELD_CHECKBOX, "1", nullptr,
         "Where the background shows through, the workflow sees it; the result is written only "
         "where the frame shows its own pixels."},
        {"dynamic", "Leave dynamic zones as they are", CDMDPI_FIELD_CHECKBOX, "0", nullptr,
         nullptr},
        {"run", "Frames per clip", CDMDPI_FIELD_TEXT, "16", nullptr,
         "With the ColorizingDMD Load Clip node, consecutive frames go through the workflow "
         "together, for results that stay steady from frame to frame."},
    };
    if (!g_editorApi->EditSettings(g_endpoint, "ComfyUI settings", kFields,
                                   sizeof(kFields) / sizeof(kFields[0]), &onSettingsSaved,
                                   nullptr)) {
        showStatus("ComfyUI: the settings are open already");
    }
}

}  // namespace

MSGPI_EXPORT void MSGPIAPI ComfyUIPluginLoad(const uint32_t endpointId, const MsgPluginAPI* api)
{
    g_msgApi = api;
    g_endpoint = endpointId;
    g_stopping = false;

    const unsigned int getLogging = api->GetMsgID(LOGPI_NAMESPACE, LOGPI_MSG_GET_API);
    api->BroadcastMsg(endpointId, getLogging, &g_logging);
    api->ReleaseMsgID(getLogging);

    const unsigned int getEditor = api->GetMsgID(CDMDPI_NAMESPACE, CDMDPI_MSG_GET_API);
    api->BroadcastMsg(endpointId, getEditor, &g_editorApi);
    api->ReleaseMsgID(getEditor);
    if (!g_editorApi || g_editorApi->version < 3) {
        log(LPI_LVL_ERROR, "Needs the ColorizingDMD editor with editor API version 3 or later; "
                           "nothing to do.");
        g_editorApi = nullptr;
        return;
    }

    g_editorApi->AddMenuEntry(endpointId, "ComfyUI: Run Workflow on Selected Frames...",
                              &onFrames, nullptr);
    g_editorApi->AddMenuEntry(endpointId,
                              "ComfyUI: Run Workflow on Backgrounds of Selected Frames...",
                              &onBackgrounds, nullptr);
    g_editorApi->AddMenuEntry(endpointId,
                              "ComfyUI: Run Workflow on Sprites of Selected Frames...",
                              &onSprites, nullptr);
    g_editorApi->AddMenuEntry(endpointId, "ComfyUI: Run Workflow on Current Background...",
                              &onCurrentBackground, nullptr);
    g_editorApi->AddMenuEntry(endpointId, "ComfyUI: Run Workflow on Current Sprite...",
                              &onCurrentSprite, nullptr);
    g_editorApi->AddMenuEntry(endpointId, "ComfyUI: Cancel Run", &onCancel, nullptr);
    g_editorApi->AddMenuEntry(endpointId, "ComfyUI: Settings...", &onSettings, nullptr);
    log(LPI_LVL_INFO, "Running workflows on ComfyUI at " + setting("url", kDefaultUrl));
}

MSGPI_EXPORT void MSGPIAPI ComfyUIPluginUnload()
{
    if (g_editorApi) {
        g_editorApi->RemoveMenuEntries(g_endpoint);
    }
    {
        const std::lock_guard<std::mutex> lock(g_jobsMutex);
        g_stopping = true;
    }
    g_cancel = true;
    // The worker may wait for the main thread, this one: run what is queued until it ends.
    while (g_running) {
        g_msgApi->FlushPendingCallbacks(g_endpoint);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_editorApi = nullptr;
    g_logging = nullptr;
    g_msgApi = nullptr;
}
