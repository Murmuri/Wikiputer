#include "WikiEngine.h"
#include <M5Cardputer.h> // Debug
#include <lgfx/utility/lgfx_miniz.h>

char32_t WikiEngine::decodeUTF8Char(const char*& ptr, const char* end) const {
    if (ptr >= end) return 0;
    
    unsigned char first = static_cast<unsigned char>(*ptr);
    
    if (first < 0x80) {
        return static_cast<char32_t>(*ptr++);
    }
    else if ((first & 0xE0) == 0xC0) {
        if (ptr + 1 >= end) {
            ptr++;
            return 0xFFFD;
        }
        char32_t cp = ((first & 0x1F) << 6) |
                      (static_cast<unsigned char>(ptr[1]) & 0x3F);
        ptr += 2;
        return cp;
    }
    else if ((first & 0xF0) == 0xE0) {
        if (ptr + 2 >= end) {
            ptr++;
            return 0xFFFD;
        }
        char32_t cp = ((first & 0x0F) << 12) |
                      ((static_cast<unsigned char>(ptr[1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(ptr[2]) & 0x3F);
        ptr += 3;
        return cp;
    }
    else if ((first & 0xF8) == 0xF0) {
        if (ptr + 3 >= end) {
            ptr++;
            return 0xFFFD;
        }
        char32_t cp = ((first & 0x07) << 18) |
                      ((static_cast<unsigned char>(ptr[1]) & 0x3F) << 12) |
                      ((static_cast<unsigned char>(ptr[2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(ptr[3]) & 0x3F);
        ptr += 4;
        return cp;
    }
    else {
        ptr++;
        return 0xFFFD;
    }
}

int WikiEngine::compareUTF8(const char* s1, size_t len1, const char* s2, size_t len2) const {
    const char* p1 = s1;
    const char* p2 = s2;
    const char* end1 = s1 + len1;
    const char* end2 = s2 + len2;
    
    while (p1 < end1 && p2 < end2) {
        unsigned char c1 = static_cast<unsigned char>(*p1);
        unsigned char c2 = static_cast<unsigned char>(*p2);
        
        if (c1 < 0x80 && c2 < 0x80) {
            if (c1 != c2) {
                return (c1 < c2) ? -1 : 1;
            }
            p1++;
            p2++;
            continue;
        }
        
        break;
    }
    
    while (p1 < end1 && p2 < end2) {
        char32_t cp1 = decodeUTF8Char(p1, end1);
        char32_t cp2 = decodeUTF8Char(p2, end2);
        
        if (cp1 != cp2) {
            return (cp1 < cp2) ? -1 : 1;
        }
    }
    
    if (p1 < end1) return 1;
    if (p2 < end2) return -1;
    
    return 0;
}

int WikiEngine::compareUTF8(const String& s1, const String& s2) const {
    return compareUTF8(s1.c_str(), s1.length(), s2.c_str(), s2.length());
}

bool WikiEngine::begin() {
    _mutex = xSemaphoreCreateMutex();
    // Try opening directly (skipping SD.exists which can be flaky)
    _idxFile = SD.open("/wiki.idx", FILE_READ);
    
    if (!_idxFile) {
        // Try alternate paths/casings
        _idxFile = SD.open("wiki.idx", FILE_READ);
    }
    if (!_idxFile) {
        _idxFile = SD.open("/WIKI.IDX", FILE_READ);
    }
    
    if (!_idxFile) {
        return false;
    }
    
    // Optional: Check if empty
    if (_idxFile.size() == 0) {
        _idxFile.close();
        return false;
    }

    uint64_t fileSize = _idxFile.size();
    if (fileSize % INDEX_RECORD_SIZE != 0) {
        // Corrupt or wrong format
        return false;
    }
    _totalEntries = fileSize / INDEX_RECORD_SIZE;

    return true;
}

bool WikiEngine::readEntry(uint32_t index, WikiIndexEntry* outEntry) {
    if (index >= _totalEntries) return false;
    
    _idxFile.seek(index * INDEX_RECORD_SIZE);
    
    // Read Title (52 bytes)
    _idxFile.read((uint8_t*)outEntry->title, TITLE_LIMIT);
    // Ensure null termination safely
    outEntry->title[TITLE_LIMIT - 1] = 0; 

    // Read Offset (8 bytes)
    _idxFile.read((uint8_t*)&outEntry->offset, 8);
    
    // Read Length (4 bytes)
    _idxFile.read((uint8_t*)&outEntry->length, 4);

    return true;
}



std::vector<String> WikiEngine::search(const String& query, int limit) {
    std::vector<String> results;
    // Mutex Lock
    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    if (_totalEntries == 0) {
        xSemaphoreGive(_mutex);
        return results;
    }

    // Binary Search to find the first occurrence >= query
    uint32_t low = 0;
    uint32_t high = _totalEntries - 1;
    uint32_t best_match = _totalEntries;

    while (low <= high) {
        uint32_t mid = low + (high - low) / 2;
        WikiIndexEntry entry;
        readEntry(mid, &entry);
        
        // Compare
        int cmp = compareUTF8(entry.title, strlen(entry.title), 
                              query.c_str(), query.length());
        
        if (cmp >= 0) {
            best_match = mid; // Potential candidate
            if (mid == 0) break;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    // Now 'best_match' is the index of the first item >= query
    // Collect up to 'limit' items
    for (uint32_t i = best_match; i < best_match + limit && i < _totalEntries; i++) {
        WikiIndexEntry entry;
        readEntry(i, &entry);
        results.push_back(String(entry.title));
    }

    xSemaphoreGive(_mutex);
    return results;
}

// Helper to load random 
bool WikiEngine::loadRandom(char* buffer, uint32_t bufferSize, String& outTitle) {
     xSemaphoreTake(_mutex, portMAX_DELAY);
     if (_totalEntries == 0) {
         xSemaphoreGive(_mutex);
         return false;
     }

     // Retry up to 20 times to find a "Main Namespace" article
     for (int i=0; i<20; i++) {
        uint32_t randIdx = random(0, _totalEntries);
        
        WikiIndexEntry entry;
        readEntry(randIdx, &entry);
        String t = String(entry.title);
        
        // Filter out meta-pages
        if (t.startsWith("Wikipedia:") || 
            t.startsWith("Template:") || 
            t.startsWith("Category:") || 
            t.startsWith("File:") || 
            t.startsWith("Help:") || 
            t.startsWith("Portal:") ||
            t.startsWith("Draft:") ||
            t.startsWith("MediaWiki:") ||
            t.startsWith("User:")) {
            continue; // Try again
        }

        // Found a good one!
        outTitle = t;
        // Assume loadArticleAt does NOT self-lock
        bool res = loadArticleAt(entry.offset, entry.length, buffer, bufferSize) > 0;
        xSemaphoreGive(_mutex);
        return res;
     }
     
     // Fallback: If we failed 20 times, just define an error or return the last one (but we released mutex).
     // Let's just return false.
     xSemaphoreGive(_mutex);
     return false;
}

uint32_t WikiEngine::loadArticle(const String& title, char* buffer, uint32_t bufferSize) {
    if (!buffer || bufferSize == 0) return 0;
    
    xSemaphoreTake(_mutex, portMAX_DELAY);
    uint32_t low = 0;
    uint32_t high = _totalEntries - 1;

    // Default error message
    const char* errMsg = "Article not found.";
    strncpy(buffer, errMsg, bufferSize);
    
    // Binary Search
    while (low <= high) {
        uint32_t mid = low + (high - low) / 2;
        WikiIndexEntry entry;
        readEntry(mid, &entry);
        
        int cmp = compareUTF8(entry.title, strlen(entry.title), 
                              title.c_str(), title.length());
        
        if (cmp == 0) {
            uint32_t res = loadArticleAt(entry.offset, entry.length, buffer, bufferSize);
            xSemaphoreGive(_mutex);
            return res;
        } else if (cmp > 0) {
            if (mid == 0) break;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    xSemaphoreGive(_mutex);
    return strlen(buffer);
}

// Decompression Task Params
struct DecompParams {
    uint8_t* src;
    size_t srcLen;
    char* dst;
    size_t dstLen;
    size_t result;
    volatile bool done;
};

// Worker Task
void decompressTask(void* pv) {
    DecompParams* params = (DecompParams*)pv;
    
    // Debug: Print stack size inside new task
    // M5Cardputer.Display.printf("Task Stack: %u\n", uxTaskGetStackHighWaterMark(NULL));
    // delay(500); // Visual confirm

    // Decompress RAW
    params->result = lgfx_tinfl_decompress_mem_to_mem(
        (uint8_t*)params->dst, 
        params->dstLen, 
        params->src, 
        params->srcLen, 
        0
    );
    
    params->done = true;
    vTaskDelete(NULL);
}

uint32_t WikiEngine::loadArticleAt(uint64_t offset, uint32_t length, char* buffer, uint32_t bufferSize) {
    if (!buffer || bufferSize == 0) return 0;

    // Decode Packed Offset
    uint32_t fileIndex = (uint32_t)(offset >> 32);
    uint32_t localOffset = (uint32_t)(offset & 0xFFFFFFFF);
    
    // POLISHED LOADING SCREEN
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(CYAN);
    M5Cardputer.Display.setCursor(65, 55);
    M5Cardputer.Display.print("Loading...");
    
    // PROGRESS BAR
    M5Cardputer.Display.drawRect(40, 80, 160, 10, DARKGREY);
    M5Cardputer.Display.fillRect(42, 82, 10, 6, WHITE); // Start

    char fileName[32];
    sprintf(fileName, "/wiki.dat.%03u", fileIndex);
    
    // Close previous
    if (_datFile) _datFile.close();
    
    _datFile = SD.open(fileName, FILE_READ);
    if (!_datFile) {
        snprintf(buffer, bufferSize, "Error: Open %s failed.", fileName);
        return strlen(buffer);
    }
    M5Cardputer.Display.fillRect(42, 82, 40, 6, WHITE); // Update
    
    if (!_datFile.seek(localOffset)) {
        snprintf(buffer, bufferSize, "Error: Seek failed.");
        return strlen(buffer);
    }
    M5Cardputer.Display.fillRect(42, 82, 80, 6, WHITE); // Update
    
    // Cap input size was here, removed.
    // if (length > 30000) length = 30000; 
    
    uint8_t* compressed = (uint8_t*)malloc(length);
    if (!compressed) {
        snprintf(buffer, bufferSize, "Error: OOM waiting for input buffer (%u)", length);
        return strlen(buffer);
    }
    
    size_t bytesRead = _datFile.read(compressed, length);
    if (bytesRead != length) {
        free(compressed);
        snprintf(buffer, bufferSize, "Error: Read %u / %u bytes.", bytesRead, length);
        return strlen(buffer);
    }
    M5Cardputer.Display.fillRect(42, 82, 120, 6, WHITE); // Update
    
    // Check for ZLIB header
    size_t headerOffset = 0;
    if (length > 6 && compressed[0] == 0x78 && 
       (compressed[1] == 0x01 || compressed[1] == 0x9C || compressed[1] == 0xDA)) {
         headerOffset = 2;
    }

    // ALIGNMENT FIX: memmove to ensure 32-bit alignment
    size_t sourceLen = length - headerOffset;
    if (headerOffset > 0) {
        memmove(compressed, compressed + headerOffset, sourceLen);
    }
    // Strip footer
    if (headerOffset > 0 && sourceLen > 4) {
        sourceLen -= 4; // Strip Adler32
    }
    
    // START NEW TASK 
    DecompParams params;
    params.src = compressed;
    params.srcLen = sourceLen;
    params.dst = buffer;
    params.dstLen = bufferSize - 1;
    params.done = false;
    params.result = (size_t)-1;

    // Create Task (32KB stack task)
    xTaskCreate(decompressTask, "unzip", 32768, &params, 1, NULL);

    int timeout = 1000;
    while (!params.done && timeout > 0) {
        delay(10);
        timeout--;
        // Animate Spinner (Fill bar progressively)
         if (timeout % 10 == 0) M5Cardputer.Display.fillRect(42, 82, 120 + ((1000-timeout)/20), 6, WHITE);
    }
    
    if (timeout == 0) {
        free(compressed);
        snprintf(buffer, bufferSize, "Error: Task Timeout");
        return strlen(buffer);
    }
    
    size_t status = params.result;
    
    if (status != (size_t)-1) {
        buffer[status] = 0; 
        free(compressed);
        return status;
    } 

    free(compressed);
    snprintf(buffer, bufferSize, "Error: Depack Fail %d (L:%d)", status, sourceLen);
    // Only show error delay on failure
    delay(2000); 
    return strlen(buffer);
}
