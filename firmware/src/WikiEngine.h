#ifndef WIKI_ENGINE_H
#define WIKI_ENGINE_H

#include <M5Cardputer.h>
#include <SD.h>
#include <vector>

// Keep in sync with converter.py
#define INDEX_RECORD_SIZE 64
#define TITLE_LIMIT 52

struct WikiIndexEntry {
    char title[TITLE_LIMIT];
    uint64_t offset; // 8 bytes
    uint32_t length; // 4 bytes
};

class WikiEngine {
public:
    bool begin();
    
    // Search returns up to 'limit' titles that start with 'query'
    std::vector<String> search(const String& query, int limit = 10);
    
    // Retrieval (Refactored for Memory Safety)
    // Writes directly to buffer, returns length written
    uint32_t loadArticle(const String& title, char* buffer, uint32_t bufferSize);
    
    // Feature: Load a random article
    bool loadRandom(char* buffer, uint32_t bufferSize, String& outTitle);
    
    // Internal loader (exposed for debug/advanced usage)
    uint32_t loadArticleAt(uint64_t offset, uint32_t length, char* buffer, uint32_t bufferSize);

private:
    File _idxFile;
    File _datFile;
    uint32_t _totalEntries = 0;

    // Helper to read an entry at a specific index
    bool readEntry(uint32_t index, WikiIndexEntry* outEntry);
    
    SemaphoreHandle_t _mutex;
};

#endif
