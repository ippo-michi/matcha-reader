#include "SentenceMining.h"

namespace sentence_mining {

SentenceMining& SentenceMining::instance() {
    static SentenceMining instance;
    return instance;
}

void SentenceMining::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    if (content.empty()) return;

    try {
        nlohmann::json json = nlohmann::json::parse(content);
        sentences_.clear();
        
        for (const auto& item : json) {
            SavedSentence s;
            s.word = item.value("word", "");
            s.definition = item.value("definition", "");
            s.sentence = item.value("sentence", "");
            s.book_title = item.value("book_title", "");
            s.chapter = item.value("chapter", "");
            s.cfi = item.value("cfi", "");
            s.page_number = item.value("page_number", 0);
            s.timestamp = item.value("timestamp", "");
            sentences_.push_back(s);
        }
    } catch (...) {
        // Invalid JSON, ignore
    }
}

void SentenceMining::save(const std::string& path) const {
    nlohmann::json json = nlohmann::json::array();
    
    for (const auto& s : sentences_) {
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
    
    std::ofstream file(path);
    if (file.is_open()) {
        file << json.dump(2);
        file.close();
    }
}

void SentenceMining::addSentence(const SavedSentence& sentence) {
    sentences_.push_back(sentence);
}

void SentenceMining::removeSentence(const std::string& cfi, int page_number) {
    for (auto it = sentences_.begin(); it != sentences_.end(); ++it) {
        if (it->cfi == cfi && it->page_number == page_number) {
            sentences_.erase(it);
            return;
        }
    }
}

std::string SentenceMining::exportToCSV() const {
    std::string csv = "word,definition,sentence,book_title,chapter,cfi,page_number,timestamp\n";
    
    for (const auto& s : sentences_) {
        // Escape commas and quotes in fields
        auto escape = [](const std::string& str) -> std::string {
            std::string result = "\"";
            for (char c : str) {
                if (c == '"') result += "\"\"";
                result += c;
            }
            result += "\"";
            return result;
        };
        
        csv += escape(s.word) + ",";
        csv += escape(s.definition) + ",";
        csv += escape(s.sentence) + ",";
        csv += escape(s.book_title) + ",";
        csv += escape(s.chapter) + ",";
        csv += escape(s.cfi) + ",";
        csv += std::to_string(s.page_number) + ",";
        csv += escape(s.timestamp) + "\n";
    }
    
    return csv;
}

std::string SentenceMining::exportToMarkdown() const {
    std::string md;
    md += "# Sentence Mining Export\n\n";
    md += "> Exported from matcha-reader\n\n";
    md += "---\n\n";
    
    for (const auto& s : sentences_) {
        md += "## ";
        md += s.word;
        md += "\n\n";
        
        if (!s.definition.empty()) {
            md += "**Definition:**\n\n";
            md += s.definition;
            md += "\n\n";
        }
        
        if (!s.sentence.empty()) {
            md += "**Sentence:**\n\n";
            md += "> ";
            md += s.sentence;
            md += "\n\n";
        }
        
        if (!s.book_title.empty()) {
            md += "**Book:** ";
            md += s.book_title;
            if (!s.chapter.empty()) {
                md += " - ";
                md += s.chapter;
            }
            md += "\n\n";
        }
        
        md += "---\n\n";
    }
    
    return md;
}

bool SentenceMining::saveSentence(const SavedSentence& sentence, const std::string& path) {
    sentences_.push_back(sentence);
    save(path);
    return true;
}

bool SentenceMining::saveMarkdownFile(const std::string& path) {
    std::string md = exportToMarkdown();
    std::ofstream file(path);
    if (file.is_open()) {
        file << md;
        file.close();
        return true;
    }
    return false;
}

}  // namespace sentence_mining
