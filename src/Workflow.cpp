#include "Workflow.h"

#include <map>

namespace comfy {

namespace {

using nlohmann::json;

std::string titleOf(const json& node)
{
    const auto meta = node.find("_meta");
    if (meta == node.end() || !meta->is_object()) {
        return {};
    }
    const auto title = meta->find("title");
    return title != meta->end() && title->is_string() ? title->get<std::string>() : std::string();
}

// The ID of the one node with the title; throws when there is none or more than one.
std::string nodeTitled(const json& prompt, const std::string& title)
{
    std::string found;
    for (auto node = prompt.begin(); node != prompt.end(); ++node) {
        if (node->is_object() && titleOf(*node) == title) {
            if (!found.empty()) {
                throw PluginError("The workflow has more than one node titled \"" + title +
                                  "\"; give the title to one only.");
            }
            found = node.key();
        }
    }
    if (found.empty()) {
        throw PluginError("The workflow has no node titled \"" + title +
                          "\". Give the node its title in ComfyUI (right click, Title), then "
                          "export the workflow again with Export (API).");
    }
    return found;
}

void fill(json& value, const std::map<std::string, int64_t>& numbers)
{
    if (value.is_object() || value.is_array()) {
        for (json& item : value) {
            fill(item, numbers);
        }
        return;
    }
    if (!value.is_string()) {
        return;
    }
    std::string text = value.get<std::string>();
    for (const auto& [name, number] : numbers) {
        const std::string placeholder = "{" + name + "}";
        if (text == placeholder) {
            value = number;
            return;
        }
        for (std::size_t at = text.find(placeholder); at != std::string::npos;
             at = text.find(placeholder, at)) {
            const std::string digits = std::to_string(number);
            text.replace(at, placeholder.size(), digits);
            at += digits.size();
        }
    }
    value = text;
}

}  // namespace

Workflow parseWorkflow(const std::string& text)
{
    json prompt;
    try {
        prompt = json::parse(text);
    } catch (const json::exception&) {
        throw PluginError("The workflow file is not JSON. Export the workflow in ComfyUI with "
                          "Export (API).");
    }
    if (!prompt.is_object() || prompt.contains("nodes") || prompt.contains("links")) {
        throw PluginError("The workflow file is not in the API format. Export the workflow in "
                          "ComfyUI with Export (API), not with Save or Export.");
    }
    for (auto node = prompt.begin(); node != prompt.end(); ++node) {
        if (!node->is_object() || !node->contains("class_type") ||
            !(*node)["class_type"].is_string() ||
            (node->contains("inputs") && !(*node)["inputs"].is_object())) {
            throw PluginError("The workflow file is not in the API format: node " + node.key() +
                              " has no class. Export the workflow with Export (API).");
        }
    }
    Workflow workflow;
    workflow.inputNode = nodeTitled(prompt, kInputTitle);
    workflow.outputNode = nodeTitled(prompt, kOutputTitle);
    const std::string inputClass = prompt[workflow.inputNode]["class_type"];
    if (inputClass == kClipClass) {
        workflow.takesClips = true;
    } else if (inputClass != "LoadImage") {
        throw PluginError("The node titled \"" + std::string(kInputTitle) + "\" is a " +
                          inputClass + "; it must be a Load Image node, or the ColorizingDMD "
                          "Load Clip node for several frames at once.");
    }
    workflow.prompt = std::move(prompt);
    return workflow;
}

nlohmann::json preparePrompt(const Workflow& workflow,
                             const std::vector<std::string>& names,
                             const Placeholders& placeholders)
{
    if (names.empty() || (!workflow.takesClips && names.size() > 1)) {
        throw PluginError("The workflow takes one picture at a time.");
    }
    json prompt = workflow.prompt;
    fill(prompt, {{"seed", placeholders.seed},
                  {"width", placeholders.width},
                  {"height", placeholders.height},
                  {"sdwidth", placeholders.sdWidth},
                  {"sdheight", placeholders.sdHeight},
                  {"count", placeholders.count}});
    json& inputs = prompt[workflow.inputNode]["inputs"];
    if (workflow.takesClips) {
        std::string list;
        for (const std::string& name : names) {
            list += (list.empty() ? "" : "\n") + name;
        }
        inputs["images"] = list;
    } else {
        inputs["image"] = names.front();
    }
    return prompt;
}

}  // namespace comfy
