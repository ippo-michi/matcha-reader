// WebExtra routes for sentence mining features.
//
// This file is replaced at build time by a project-specific implementation
// if the project needs additional routes. Excluding this source file from the
// build causes the linker to pull in this default empty body, which is a no-op.

#include "WebExtra.h"
#include "CrossPointWebServer.h"
#include "SentenceMining.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>

namespace webextra {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

static std::string getCacheDir() {
    // Use the same cache directory as bookmarks
    const char* env_cache = std::getenv("MATCHA_CACHE_DIR");
    if (env_cache && env_cache[0]) {
        return std::string(env_cache) + "/sentence_mining";
    }
    // Default: ~/.cache/matcha-reader/sentence_mining
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");  // Windows fallback
    if (home) {
        return std::string(home) + "/.cache/matcha-reader/sentence_mining";
    }
    return "sentence_mining";
}

static std::string getSentencesPath() {
    return getCacheDir() + "/sentences.json";
}

// ---------------------------------------------------------------------------
// Route: GET /api/sentences/export
// Returns the mined sentences as a CSV file download.
// ---------------------------------------------------------------------------

static void handleExportSentences(CrossPointWebServer::RequestPtr req,
                                   CrossPointWebServer::ResponsePtr res) {
    auto& mining = sentence_mining::SentenceMining::instance();
    
    // Load if not already loaded
    if (mining.isEmpty()) {
        mining.load(getSentencesPath());
    }
    
    if (mining.isEmpty()) {
        res->setStatusCode(404);
        res->setContent("No saved sentences yet.");
        return;
    }
    
    std::string csv = mining.exportToCSV();
    
    res->setStatusCode(200);
    res->addHeader("Content-Type", "text/csv; charset=utf-8");
    res->addHeader("Content-Disposition", "attachment; filename=\"sentences.csv\"");
    res->setContent(csv);
}

// ---------------------------------------------------------------------------
// Route: POST /api/sentences/save
// Save the current word lookup result as a mined sentence.
// Body: { "word": "...", "definition": "...", "sentence": "...",
//         "book_title": "...", "chapter": "...", "cfi": "...", "page_number": N }
// ---------------------------------------------------------------------------

static void handleSaveSentence(CrossPointWebServer::RequestPtr req,
                                CrossPointWebServer::ResponsePtr res) {
    auto& mining = sentence_mining::SentenceMining::instance();
    
    // Load if not already loaded
    if (mining.isEmpty()) {
        mining.load(getSentencesPath());
    }
    
    // Parse request body
    std::string body(req->getBody().begin(), req->getBody().end());
    
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (...) {
        res->setStatusCode(400);
        res->setContent("Invalid JSON body.");
        return;
    }
    
    sentence_mining::SavedSentence s;
    s.word = json.value("word", "");
    s.definition = json.value("definition", "");
    s.sentence = json.value("sentence", "");
    s.book_title = json.value("book_title", "");
    s.chapter = json.value("chapter", "");
    s.cfi = json.value("cfi", "");
    s.page_number = json.value("page_number", 0);
    s.timestamp = getTimestamp();
    
    mining.addSentence(s);
    mining.save(getSentencesPath());
    
    // Return success with count
    nlohmann::json response;
    response["success"] = true;
    response["count"] = mining.count();
    res->setStatusCode(200);
    res->addHeader("Content-Type", "application/json");
    res->setContent(response.dump());
}

// ---------------------------------------------------------------------------
// Route: GET /api/sentences/list
// Returns the list of mined sentences as JSON.
// ---------------------------------------------------------------------------

static void handleListSentences(CrossPointWebServer::RequestPtr req,
                                 CrossPointWebServer::ResponsePtr res) {
    auto& mining = sentence_mining::SentenceMining::instance();
    
    // Load if not already loaded
    if (mining.isEmpty()) {
        mining.load(getSentencesPath());
    }
    
    nlohmann::json json = nlohmann::json::array();
    for (const auto& s : mining.getSentenceList()) {
        nlohmann::json item;
        item["word"] = s.word;
        item["definition"] = s.definition;
        item["sentence"] = s.sentence;
        item["book_title"] = s.book_title;
        item["chapter"] = s.chapter;
        item["cfi"] = s.cfi;
        item["page_number"] = s.page_number;
        item["timestamp"] = s.timestamp;
        json.push_back(item);
    }
    
    res->setStatusCode(200);
    res->addHeader("Content-Type", "application/json");
    res->setContent(json.dump(2));
}

// ---------------------------------------------------------------------------
// Route: DELETE /api/sentences/:index
// Remove a sentence by its index in the list.
// ---------------------------------------------------------------------------

static void handleDeleteSentence(CrossPointWebServer::RequestPtr req,
                                  CrossPointWebServer::ResponsePtr res) {
    auto& mining = sentence_mining::SentenceMining::instance();
    
    // Load if not already loaded
    if (mining.isEmpty()) {
        mining.load(getSentencesPath());
    }
    
    // Extract index from path: /api/sentences/:index
    std::string path = req->getPath();
    size_t pos = path.rfind('/');
    if (pos == std::string::npos || pos == path.size() - 1) {
        res->setStatusCode(400);
        res->setContent("Invalid request.");
        return;
    }
    
    std::string index_str = path.substr(pos + 1);
    int index;
    try {
        index = std::stoi(index_str);
    } catch (...) {
        res->setStatusCode(400);
        res->setContent("Invalid index.");
        return;
    }
    
    auto& list = mining.getSentenceList();
    if (index < 0 || index >= static_cast<int>(list.size())) {
        res->setStatusCode(404);
        res->setContent("Sentence not found.");
        return;
    }
    
    mining.removeSentence(list[index].cfi, list[index].page_number);
    mining.save(getSentencesPath());
    
    nlohmann::json response;
    response["success"] = true;
    response["count"] = mining.count();
    res->setStatusCode(200);
    res->addHeader("Content-Type", "application/json");
    res->setContent(response.dump());
}

// ---------------------------------------------------------------------------
// Route: GET /api/sentences/export/markdown
// Returns the mined sentences as a Markdown file download (Anki-compatible).
// ---------------------------------------------------------------------------

static void handleExportSentencesMarkdown(CrossPointWebServer::RequestPtr req,
                                           CrossPointWebServer::ResponsePtr res) {
    auto& mining = sentence_mining::SentenceMining::instance();
    
    // Load if not already loaded
    if (mining.isEmpty()) {
        mining.load(getSentencesPath());
    }
    
    if (mining.isEmpty()) {
        res->setStatusCode(404);
        res->setContent("No saved sentences yet.");
        return;
    }
    
    std::string md = mining.exportToMarkdown();
    
    res->setStatusCode(200);
    res->addHeader("Content-Type", "text/markdown; charset=utf-8");
    res->addHeader("Content-Disposition", "attachment; filename=\\\"sentences.md\\\"");
    res->setContent(md);
}

// ---------------------------------------------------------------------------
// Route: POST /api/sentences/save-markdown
// Save a sentence entry. Body: { "word": "...", "definition": "...", "sentence": "...",
//         "book_title": "...", "chapter": "...", "cfi": "...", "page_number": N }
// ---------------------------------------------------------------------------

static void handleSaveSentenceMarkdown(CrossPointWebServer::RequestPtr req,
                                        CrossPointWebServer::ResponsePtr res) {
    auto& mining = sentence_mining::SentenceMining::instance();
    
    // Load if not already loaded
    if (mining.isEmpty()) {
        mining.load(getSentencesPath());
    }
    
    // Parse request body
    std::string body(req->getBody().begin(), req->getBody().end());
    
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (...) {
        res->setStatusCode(400);
        res->setContent("Invalid JSON body.");
        return;
    }
    
    sentence_mining::SavedSentence s;
    s.word = json.value("word", "");
    s.definition = json.value("definition", "");
    s.sentence = json.value("sentence", "");
    s.book_title = json.value("book_title", "");
    s.chapter = json.value("chapter", "");
    s.cfi = json.value("cfi", "");
    s.page_number = json.value("page_number", 0);
    s.timestamp = getTimestamp();
    
    mining.addSentence(s);
    mining.save(getSentencesPath());
    
    // Return success with count
    nlohmann::json response;
    response["success"] = true;
    response["count"] = mining.count();
    res->setStatusCode(200);
    res->addHeader("Content-Type", "application/json");
    res->setContent(response.dump());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerRoutes(WebServer* server) {
    if (!server) return;
    
    // GET /api/sentences/export - download CSV
    server->addRoute("GET", "/api/sentences/export", handleExportSentences);
    
    // GET /api/sentences/export/markdown - download Markdown (Anki-compatible)
    server->addRoute("GET", "/api/sentences/export/markdown", handleExportSentencesMarkdown);
    
    // POST /api/sentences/save - save a sentence
    server->addRoute("POST", "/api/sentences/save", handleSaveSentence);
    
    // POST /api/sentences/save-markdown - save a sentence entry
    server->addRoute("POST", "/api/sentences/save-markdown", handleSaveSentenceMarkdown);
    
    // GET /api/sentences/list - list all mined sentences
    server->addRoute("GET", "/api/sentences/list", handleListSentences);
    
    // DELETE /api/sentences/:index - delete a sentence
    server->addRoute("DELETE", "/api/sentences/:index", handleDeleteSentence);
}

}  // namespace webextra
