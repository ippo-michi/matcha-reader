#pragma once

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace sentence_mining {

struct SavedSentence {
  std::string word;
  std::string definition;
  std::string sentence;
  std::string book_title;
  std::string chapter;
  std::string cfi;
  int page_number = 0;
  std::string timestamp;
};

class SentenceMining {
 public:
  static SentenceMining& instance();

  void load(const std::string& path);
  void save(const std::string& path) const;

  void addSentence(const SavedSentence& sentence);
  void removeSentence(const std::string& cfi, int page_number);

  int count() const { return static_cast<int>(sentences_.size()); }
  bool isEmpty() const { return sentences_.empty(); }

  // Export to CSV format (Anki-compatible)
  std::string exportToCSV() const;

  // Export to Markdown format (Anki-compatible)
  std::string exportToMarkdown() const;

  // Save a single sentence entry and persist to disk
  bool saveSentence(const SavedSentence& sentence, const std::string& path);

  // Save all sentences as a Markdown file
  bool saveMarkdownFile(const std::string& path);

 private:
  SentenceMining() = default;

  std::vector<SavedSentence> sentences_;
};

}  // namespace sentence_mining
