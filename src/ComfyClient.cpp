#include "ComfyClient.h"

#include <utility>

#include "httplib.h"

namespace comfy {

namespace {

using nlohmann::json;

std::string textOf(const json& object, const char* key)
{
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>()
                                                       : std::string();
}

std::string joined(const std::string& first, const std::string& second)
{
    if (first.empty() || second.empty() || first == second) {
        return first.empty() ? second : first;
    }
    return first + ": " + second;
}

std::string failure(const httplib::Result& result)
{
    if (!result) {
        return "ComfyUI does not answer (" + httplib::to_string(result.error()) +
               "). Is it running at the address in the settings?";
    }
    return "ComfyUI answered HTTP " + std::to_string(result->status);
}

json parseBody(const std::string& body)
{
    try {
        return json::parse(body);
    } catch (const std::exception&) {
        throw ComfyError("ComfyUI's answer is not JSON; is the address in the settings "
                         "ComfyUI's?");
    }
}

}  // namespace

std::string describeRefusal(const std::string& body)
{
    json answer;
    try {
        answer = json::parse(body);
    } catch (const std::exception&) {
        return "ComfyUI refused the workflow.";
    }
    std::string text = "ComfyUI refused the workflow";
    if (answer.contains("error") && answer["error"].is_object()) {
        const std::string reason =
            joined(textOf(answer["error"], "message"), textOf(answer["error"], "details"));
        if (!reason.empty()) {
            text += ": " + reason;
        }
    }
    if (answer.contains("node_errors") && answer["node_errors"].is_object()) {
        for (auto node = answer["node_errors"].begin(); node != answer["node_errors"].end();
             ++node) {
            const std::string kind = node->is_object() ? textOf(*node, "class_type") : "";
            if (!node->is_object() || !node->contains("errors")) {
                continue;
            }
            for (const json& error : (*node)["errors"]) {
                if (error.is_object()) {
                    text += "\nNode " + node.key() + (kind.empty() ? "" : " (" + kind + ")") +
                            ": " + joined(textOf(error, "message"), textOf(error, "details"));
                }
            }
        }
    }
    return text;
}

PromptState parseHistory(const std::string& body,
                         const std::string& promptId,
                         const std::string& outputNode)
{
    PromptState state;
    const json history = parseBody(body);
    // Empty while the prompt waits or runs.
    if (!history.is_object() || !history.contains(promptId)) {
        return state;
    }
    const json& entry = history[promptId];
    const json status = entry.value("status", json::object());
    const std::string result = status.is_object() ? textOf(status, "status_str") : "";
    state.finished = !status.is_object() || status.value("completed", false) ||
                     result == "error";
    if (result == "error") {
        state.failed = true;
        state.error = "the workflow failed in ComfyUI";
        for (const json& message : status.value("messages", json::array())) {
            if (!message.is_array() || message.size() < 2 || !message[1].is_object()) {
                continue;
            }
            if (message[0] == "execution_error") {
                const json& data = message[1];
                state.error = "node " + textOf(data, "node_id") + " (" +
                              textOf(data, "node_type") + ") failed in ComfyUI: " +
                              textOf(data, "exception_message");
            } else if (message[0] == "execution_interrupted") {
                state.error = "the workflow was interrupted in ComfyUI";
            }
        }
        return state;
    }
    const json outputs = entry.value("outputs", json::object());
    if (outputs.is_object() && outputs.contains(outputNode)) {
        for (const json& image : outputs[outputNode].value("images", json::array())) {
            if (image.is_object()) {
                state.images.push_back(OutputImage{textOf(image, "filename"),
                                                   textOf(image, "subfolder"),
                                                   textOf(image, "type")});
            }
        }
    }
    return state;
}

ComfyClient::ComfyClient(std::string url, std::string token, int timeoutSeconds)
    : m_url(std::move(url))
    , m_token(std::move(token))
    , m_timeout(timeoutSeconds)
{
    while (!m_url.empty() && m_url.back() == '/') {
        m_url.pop_back();
    }
}

namespace {

httplib::Client clientFor(const std::string& url, const std::string& token, int timeout)
{
    httplib::Client client(url);
    client.set_connection_timeout(timeout);
    client.set_read_timeout(timeout);
    client.set_write_timeout(timeout);
    if (!token.empty()) {
        client.set_bearer_token_auth(token);
    }
    return client;
}

}  // namespace

void ComfyClient::check() const
{
    httplib::Client client = clientFor(m_url, m_token, m_timeout);
    const httplib::Result result = client.Get("/system_stats");
    if (!result || result->status != 200) {
        throw ComfyError(failure(result));
    }
    static_cast<void>(parseBody(result->body));
}

std::string ComfyClient::upload(const std::string& name, const std::string& png) const
{
    httplib::Client client = clientFor(m_url, m_token, m_timeout);
    const httplib::MultipartFormDataItems items = {
        {"image", png, name, "image/png"},
        {"type", "input", "", ""},
        {"overwrite", "true", "", ""},
    };
    const httplib::Result result = client.Post("/upload/image", items);
    if (!result || result->status != 200) {
        throw ComfyError("The picture could not be uploaded to ComfyUI: " + failure(result));
    }
    const json answer = parseBody(result->body);
    const std::string stored = answer.is_object() ? textOf(answer, "name") : "";
    const std::string subfolder = answer.is_object() ? textOf(answer, "subfolder") : "";
    if (stored.empty()) {
        throw ComfyError("ComfyUI did not say where it stored the uploaded picture.");
    }
    return subfolder.empty() ? stored : subfolder + "/" + stored;
}

std::string ComfyClient::queue(const nlohmann::json& prompt, const std::string& clientId) const
{
    httplib::Client client = clientFor(m_url, m_token, m_timeout);
    const httplib::Result result =
        client.Post("/prompt", json{{"prompt", prompt}, {"client_id", clientId}}.dump(),
                    "application/json");
    if (!result) {
        throw ComfyError(failure(result));
    }
    if (result->status != 200) {
        throw ComfyError(describeRefusal(result->body));
    }
    const json answer = parseBody(result->body);
    const std::string id = answer.is_object() ? textOf(answer, "prompt_id") : "";
    if (id.empty()) {
        throw ComfyError(describeRefusal(result->body));
    }
    return id;
}

PromptState ComfyClient::state(const std::string& promptId, const std::string& outputNode) const
{
    httplib::Client client = clientFor(m_url, m_token, m_timeout);
    const httplib::Result result = client.Get("/history/" + promptId);
    if (!result || result->status != 200) {
        throw ComfyError(failure(result));
    }
    return parseHistory(result->body, promptId, outputNode);
}

std::string ComfyClient::download(const OutputImage& image) const
{
    httplib::Client client = clientFor(m_url, m_token, m_timeout);
    const httplib::Params params = {{"filename", image.filename},
                                    {"subfolder", image.subfolder},
                                    {"type", image.type.empty() ? "output" : image.type}};
    const httplib::Result result = client.Get("/view", params, httplib::Headers{});
    if (!result || result->status != 200) {
        throw ComfyError("The result " + image.filename + " could not be downloaded from "
                         "ComfyUI: " + failure(result));
    }
    return result->body;
}

void ComfyClient::cancel(const std::string& promptId) const
{
    httplib::Client client = clientFor(m_url, m_token, m_timeout);
    // Out of the queue when it waits; interrupted when it runs (older ComfyUI versions
    // interrupt whatever runs).
    static_cast<void>(client.Post("/queue", json{{"delete", {promptId}}}.dump(),
                                  "application/json"));
    static_cast<void>(client.Post("/interrupt", json{{"prompt_id", promptId}}.dump(),
                                  "application/json"));
}

}  // namespace comfy
