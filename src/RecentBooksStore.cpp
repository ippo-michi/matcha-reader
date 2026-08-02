#include "RecentBooksStore.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <iterator>

namespace {
constexpr size_t LIBRARY_CACHE_MAX_BOOKS = 2048;
}

void RecentBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : recentBooks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }
}

bool RecentBooksStore::fromJson(JsonVariantConst doc) { return fromJson(doc, MAX_RECENT_BOOKS); }

bool RecentBooksStore::fromJson(JsonVariantConst doc, const size_t maxBooks) {
  // Tolerate a missing/invalid 'books' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  recentBooks.clear();
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  recentBooks.reserve(std::min(arr.size(), maxBooks));
  for (JsonObjectConst obj : arr) {
    if (recentBooks.size() >= maxBooks) break;
    RecentBook book;
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
    recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", getCount());
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  // Remove existing entry if present
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    recentBooks.erase(it);
  }

  // Add to front
  recentBooks.insert(recentBooks.begin(), {path, title, author, coverBmpPath});

  // Trim to max size
  if (recentBooks.size() > MAX_RECENT_BOOKS) {
    recentBooks.resize(MAX_RECENT_BOOKS);
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  recentBooks.erase(it);
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  }
  return true;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return;
  }
  it->path = newPath;
  if (!oldCachePath.empty() && !it->coverBmpPath.empty() && it->coverBmpPath.rfind(oldCachePath, 0) == 0) {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  saveToFile();
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}

RecentBook RecentBooksStore::getDataFromBook(std::string path) const {
  std::string lastBookFileName = "";
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash != std::string::npos) {
    lastBookFileName = path.substr(lastSlash + 1);
  }

  LOG_DBG("RBS", "Loading recent book: %s", path.c_str());

  // If epub, try to load the metadata for title/author and cover.
  // Use buildIfMissing=false to avoid heavy epub loading on boot; getTitle()/getAuthor() may be
  // blank until the book is opened, and entries with missing title are omitted from recent list.
  if (FsHelpers::hasEpubExtension(lastBookFileName)) {
    Epub epub(path, "/.crosspoint");
    epub.load(false, true);
    return RecentBook{path, epub.getTitle(), epub.getAuthor(), epub.getThumbBmpPath()};
  } else if (FsHelpers::hasXtcExtension(lastBookFileName)) {
    // Handle XTC file
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      return RecentBook{path, xtc.getTitle(), xtc.getAuthor(), xtc.getThumbBmpPath()};
    }
  } else if (FsHelpers::hasTxtExtension(lastBookFileName) || FsHelpers::hasMarkdownExtension(lastBookFileName)) {
    return RecentBook{path, lastBookFileName, "", ""};
  }
  return RecentBook{path, "", "", ""};
}

bool RecentBooksStore::saveToPath(const char* path) const {
  return saveBooksToPath(recentBooks, path);
}

bool RecentBooksStore::saveBooksToPath(const std::vector<RecentBook>& books, const char* path) {
  Storage.mkdir("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("RBS", path, file)) return false;

  constexpr char PREFIX[] = "{\"books\":[";
  if (file.write(PREFIX, sizeof(PREFIX) - 1) != sizeof(PREFIX) - 1) {
    LOG_ERR("RBS", "Failed to write library cache prefix");
    return false;
  }

  bool first = true;
  JsonDocument record;
  for (const auto& book : books) {
    if (!first && file.write(static_cast<uint8_t>(',')) != 1) {
      LOG_ERR("RBS", "Failed to write library cache separator");
      return false;
    }
    first = false;

    // Keep peak memory proportional to one record. The generic PersistableStore path builds
    // both the complete JsonDocument and a complete serialized String; a 100+ book Library can
    // consume the X3's remaining heap twice over and make the next STL allocation abort.
    record.clear();
    record["path"] = book.path.c_str();
    record["title"] = book.title.c_str();
    record["author"] = book.author.c_str();
    record["coverBmpPath"] = book.coverBmpPath.c_str();
    if (serializeJson(record, file) == 0) {
      LOG_ERR("RBS", "Failed to serialize library cache record");
      return false;
    }
  }

  constexpr char SUFFIX[] = "]}";
  if (file.write(SUFFIX, sizeof(SUFFIX) - 1) != sizeof(SUFFIX) - 1) {
    LOG_ERR("RBS", "Failed to write library cache suffix");
    return false;
  }
  file.flush();
  return true;
}

bool RecentBooksStore::loadFromPath(const char* path) {
  if (!Storage.exists(path)) return false;
  HalFile file;
  if (!Storage.openFileForRead("RBS", path, file)) return false;

  // Parse from the file stream instead of first copying the complete cache into an Arduino
  // String. The JsonDocument is still required for backward-compatible loading, but the raw
  // JSON no longer occupies a second equally large allocation beside it.
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  if (error) {
    LOG_ERR("RBS", "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return fromJson(doc.as<JsonVariantConst>(), LIBRARY_CACHE_MAX_BOOKS);
}
