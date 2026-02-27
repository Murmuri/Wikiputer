#include <M5Cardputer.h>
#include "WikiEngine.h"
#include "UI.h"

WikiEngine engine;
UI ui;

// Async Search Globals
QueueHandle_t searchQ;
volatile bool resultsReady = false;

struct SearchReq {
    char query[64];
};

void searchWorkerTask(void* pv) {
    SearchReq req;
    while (true) {
        if (xQueueReceive(searchQ, &req, portMAX_DELAY)) {
             String q = String(req.query);
             std::vector<String> res = engine.search(q, 100);
             
             ui.setResults(res); 
             resultsReady = true;
        }
        vTaskDelay(10); // CRITICAL: Prevent Starvation / Watchdog
    }
}

void cleanWikiText(char* buf) {
    if (!buf) return;
    
    char* src = buf;
    char* dst = buf;
    
    // We strictly skip blocks. 
    // nesting is critical.
    
// ... (Start of function)
    while (*src) {
        // 1. HTML Comments <!-- ... -->
        if (src[0] == '<' && src[1] == '!' && src[2] == '-' && src[3] == '-') {
             src += 4;
             while (*src) {
                 if (src[0] == '-' && src[1] == '-' && src[2] == '>') {
                     src += 3;
                     break;
                 }
                 src++;
             }
             continue;
        }

        // 2. Templates {{ ... }} - Recursive
        if (src[0] == '{' && src[1] == '{') {
            int depth = 1;
             char* end = strstr(src, "-->");
             if (end) src = end + 3;
             else break; // Safety
             continue;
        }

        // 2. Refs <ref ...> ... </ref> (Strip CONTENT)
        if (strncmp(src, "<ref", 4) == 0) {
             src += 4;
             // Handle simple self-close <ref name="x" />
             // We need to find closer. heuristic: look for /> or </ref>
             // But content inside might contain anything.
             // Simplest robust way: Scan for </ref>. If not found, maybe it was self closing.
             char* closer = strstr(src, "</ref>");
             char* selfCloser = strstr(src, "/>");
             
             // Pick earliest
             if (closer && (!selfCloser || closer < selfCloser)) {
                 src = closer + 6;
             } else if (selfCloser) {
                 src = selfCloser + 2;
             } else {
                 // Broken ref, just skip tag
                 while (*src && *src != '>') src++;
                 if (*src) src++;
             }
             continue;
        }

// ...
        // 3. Generic Tags & Specific Block Tags
        if (*src == '<') {
             // A. Scripts/Styles/Galleries/Tables (Strip Content)
             // Check for <table, <gallery, <script, <style
             const char* skipTags[] = {"table", "gallery", "script", "style", "div"}; // Maybe div is too aggressive? Infoboxes often use divs.
             // Let's stick to table/gallery/script/style for now.
             bool strictSkip = false;
             
             // Check if it's a closing tag </... (Ignore, handled by loop)
             if (src[1] == '/') {
                 // Just a loose closing tag? Strip it.
                 char* end = strchr(src, '>');
                 if (end) { src = end+1; continue; }
             }
             
             for (const char* tag : skipTags) {
                 size_t len = strlen(tag);
                 // Check <TAG or <TAG> or <TAG (space)
                 if (strncmp(src+1, tag, len) == 0 && (src[1+len] == '>' || src[1+len] == ' ')) {
                     strictSkip = true;
                     // Find closing tag </TAG>
                     // Simple scan? Nested tables?
                     // Wiki HTML is usually well formed.
                     // Let's simple scan for </TAG> for now to avoid stack complexity.
                     // Construct closer </TAG>
                     char closer[16];
                     snprintf(closer, 16, "</%s>", tag);
                     
                     // Find it
                     // We must handle case-insensitivity roughly? Wiki is lowercase standard.
                     char* closePtr = strstr(src, closer);
                     if (closePtr) {
                         src = closePtr + strlen(closer);
                     } else {
                         // Unclosed? Strip tag only
                         char* end = strchr(src, '>');
                         if (end) src = end + 1;
                     }
                     break; 
                 }
             }
             if (strictSkip) continue;
             
             // B. Generic Tags (Strip Tag, Keep Content)
             // e.g. <small>, <b>, <span>
             char* end = strchr(src, '>');
             if (end && (end - src < 64)) { 
                 src = end + 1;
                 continue;
             }
        }
// ...

        // 4. Recursive Blocks: {{...}} and {|...|}
        if ((src[0] == '{' && src[1] == '{') || (src[0] == '{' && src[1] == '|')) {
            char open1 = src[0];
            char open2 = src[1]; // '{' or '|'
            // Determine close signature
            // {{ -> }}
            // {| -> |}
            char close1 = (open2 == '|') ? '|' : '}';
            char close2 = '}';
            
            int depth = 1;
            src += 2;
            while (*src && depth > 0) {
                // Check Open
                if (src[0] == open1 && src[1] == open2) { depth++; src+=2; }
                // Check Close
                else if (src[0] == close1 && src[1] == close2) { depth--; src+=2; }
                else src++;
            }
            continue;
        }

        // 5. Links & Files: [[ ... ]]
        if (src[0] == '[' && src[1] == '[') {
            // Check for Meta-Prefixes to Skip Block
            bool isMeta = false;
            if (strncmp(src+2, "File:", 5) == 0) isMeta = true;
            else if (strncmp(src+2, "Image:", 6) == 0) isMeta = true;
            else if (strncmp(src+2, "Category:", 9) == 0) isMeta = true;
            
            if (isMeta) {
                // Recursive Skip
                int depth = 1;
                src += 2;
                while (*src && depth > 0) {
                    if (src[0] == '[' && src[1] == '[') { depth++; src+=2; }
                    else if (src[0] == ']' && src[1] == ']') { depth--; src+=2; }
                    else src++;
                }
                continue;
            }
            
            // Standard Link: [[Target|Label]] -> Keep Label
            // Standard Link: [[Target]] -> Keep Target
            // Heuristic: Scan for '|' or ']]'
            // We do NOT handle nested links here (rare in standard links).
            char* ptr = src + 2;
            char* pipe = nullptr;
            char* end = nullptr;
            
            while (*ptr) {
                if (*ptr == ']' && *(ptr+1) == ']') { end = ptr; break; }
                if (*ptr == '|' && !pipe) pipe = ptr;
                if (*ptr == '\n' || *ptr == '{') break; // Safety break
                ptr++;
            }
            
            if (end) {
                char* textStart = pipe ? pipe + 1 : src + 2;
                int len = end - textStart;
                if (len > 0) {
                    // Copy text to dst
                    memmove(dst, textStart, len); // memmove in case of overlap? src>dst always.
                    // Actually we are writing to dst which is same buffer. Manual copy safest.
                    for (int k=0; k<len; k++) *dst++ = textStart[k];
                }
                src = end + 2;
            } else {
                // Broken or complex link, just strip brackets?
                src += 2; 
            }
            continue;
        }
        
        // 6. Formatting: ''' or '' -> Skip
        if (src[0] == '\'' && src[1] == '\'') {
            src += 2;
            if (*src == '\'') src++;
            continue;
        }

        // 7. Magic Words: __NOTOC__
        if (src[0] == '_' && src[1] == '_') {
            if (strncmp(src, "__NOTOC__", 9) == 0) { src += 9; continue; }
            if (strncmp(src, "__TOC__", 7) == 0) { src += 7; continue; }
            if (strncmp(src, "__NOEDITSECTION__", 17) == 0) { src += 17; continue; }
        }

        *dst++ = *src++;
    }
    *dst = 0;
    
    // Pass 2: Whitespace Compaction (Existing Logic)
    // Pass 2: Whitespace Compaction (Existing Logic)
    src = buf;
    dst = buf;
    int newlineCount = 0;
    bool leading = true; 
    
    while (*src) {
        char c = *src;
        if (c == '\r') { src++; continue; }
        
        if (c == '\n') {
            if (leading) { src++; continue; }
            newlineCount++;
            if (newlineCount <= 2) *dst++ = '\n';
        } else if (c == ' ' || c == '\t') {
           if (leading || newlineCount > 0) {
               // strip
           } else {
               *dst++ = ' '; // Normalize spaces
           }
        } else {
            leading = false;
            newlineCount = 0; 
            *dst++ = c;
        }
        src++;
    }
    *dst = 0;
}

bool isRussianLayout = true;

String russianCharToUTF8(char latinKey) {
    switch (latinKey) {
        case 'f': return "а";
        case ',': return "б";
        case 'd': return "в";
        case 'u': return "г";
        case 'l': return "д";
        case 't': return "е";
        case '`': return "ё";
        case ';': return "ж";
        case 'p': return "з";
        case 'b': return "и";
        case 'q': return "й";
        case 'r': return "к";
        case 'k': return "л";
        case 'v': return "м";
        case 'y': return "н";
        case 'j': return "о";
        case 'g': return "п";
        case 'h': return "р";
        case 'c': return "с";
        case 'n': return "т";
        case 'e': return "у";
        case 'a': return "ф";
        case '[': return "х";
        case 'w': return "ц";
        case 'x': return "ч";
        case 'i': return "ш";
        case 'o': return "щ";
        case ']': return "ъ";
        case 's': return "ы";
        case 'm': return "ь";
        case '\'': return "э";
        case '.': return "ю";
        case 'z': return "я";
        case 'F': return "А";
        case '<': return "Б";
        case 'D': return "В";
        case 'U': return "Г";
        case 'L': return "Д";
        case 'T': return "Е";
        case '~': return "Ё";
        case ':': return "Ж";
        case 'P': return "З";
        case 'B': return "И";
        case 'Q': return "Й";
        case 'R': return "К";
        case 'K': return "Л";
        case 'V': return "М";
        case 'Y': return "Н";
        case 'J': return "О";
        case 'G': return "П";
        case 'H': return "Р";
        case 'C': return "С";
        case 'N': return "Т";
        case 'E': return "У";
        case 'A': return "Ф";
        case '{': return "Х";
        case 'W': return "Ц";
        case 'X': return "Ч";
        case 'I': return "Ш";
        case 'O': return "Щ";
        case '}': return "Ъ";
        case 'S': return "Ы";
        case 'M': return "Ь";
        case '"': return "Э";
        case '>': return "Ю";
        case 'Z': return "Я";
        
        default: return String(latinKey);
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    
    ui.begin();
    
    // SD Pins (Standard for Original & ADV): SCK=40, MISO=39, MOSI=14, CS=12
    SPI.begin(40, 39, 14, 12);
    if (!SD.begin(12, SPI, 25000000)) {
        M5Cardputer.Display.println("SD Init Failed!");
        M5Cardputer.Display.println("Check SD Card & Reset");
        while(1) delay(100);
    }
    
    // Splash screen animation loop
    unsigned long splashStart = millis();
    while (millis() - splashStart < 2500) {
        ui.update();
        ui.draw();
        delay(50);
    }

    if (!engine.begin()) {
        M5Cardputer.Display.fillScreen(BLACK);
        // ... Error Display ...
        M5Cardputer.Display.setCursor(10, 10);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.println("WikiEngine Fail");
        while(1) delay(100);
    }

    // INIT ASYNC SEARCH (Increased Stack to 16KB for stability)
    searchQ = xQueueCreate(1, sizeof(SearchReq)); 
    xTaskCreate(searchWorkerTask, "search", 16384, NULL, 1, NULL); 

    ui.setState(STATE_SEARCH); 
}

void removeLastUTF8Char(String &q) {
    if (q.length() == 0) return;
    
    int len = q.length();
    int i = len - 1;
    
    while (i >= 0) {
        char c = q[i];

        if ((c & 0x80) == 0 || (c & 0xC0) == 0xC0) {
            break;
        }

        i--;
    }
    
    if (i >= 0) {
        q.remove(i, len - i);
    } else {
        q.remove(len - 1);
    }
}

void loop() {
    M5Cardputer.update();
    
    // Global Draw Update (cursors blinking etc)
    if (ui.getState() == STATE_SEARCH) {
        static unsigned long lastBlinkTime = 0;
        if (millis() - lastBlinkTime > 500) {
            ui.draw(false); // Partial redraw (cursor only)
            lastBlinkTime = millis();
        }
        
        // CHECK FOR ASYNC RESULTS
        if (resultsReady) {
            resultsReady = false;
            ui.draw(); // Redraw with new results (This draws list)
        }
    }

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
        AppState state = ui.getState();

        if (M5Cardputer.Keyboard.isKeyPressed(KEY_LEFT_CTRL)) {
            isRussianLayout = !isRussianLayout;
        }

        // ESC Key: Return to Search Instantly (Check ASCII 27 or ` key)
        if (M5Cardputer.Keyboard.isKeyPressed((char)27) || M5Cardputer.Keyboard.isKeyPressed('`')) {
             ui.setState(STATE_SEARCH);
             return; 
        }

        if (state == STATE_SEARCH) {
            bool updateQuery = false;
            String q = ui.getSearchQuery();
    
            for (auto i : status.word) {
                if (!isRussianLayout) {
                    q += (char)i;
                } else {
                    q += russianCharToUTF8(i);
                }
                    
                updateQuery = true;
            } 

            if (status.del && q.length() > 0) {
                removeLastUTF8Char(q);
                updateQuery = true;
            }

            if (updateQuery) {
                ui.setSearchQuery(q);
                
                // INSTANT DRAW (User sees typing immediately)
                ui.draw(); 
                
                // QUEUE ASYNC SEARCH
                String searchQStr = q;
                if (searchQStr.length() > 0) {
                    SearchReq req;
                    strncpy(req.query, searchQStr.c_str(), 63);
                    req.query[63] = 0;
                    xQueueOverwrite(searchQ, &req); // Non-blocking overwrite
                } else {
                     // Empty query -> Clear results immediately
                     std::vector<String> empty;
                     ui.setResults(empty);
                     ui.draw();
                }
            }

            if (status.enter) {
                if (q.length() > 0) {
                     if (ui.getResult(0).length() > 0) {
                        ui.setState(STATE_RESULTS);
                     }
                } else {
                     // EMPTY QUERY + ENTER = RANDOM ARTICLE
                     String title;
                     if (engine.loadRandom(ui.getArticleBuffer(), ui.getArticleBufferSize(), title)) {
                        // Use Helper
                        cleanWikiText(ui.getArticleBuffer());

                        ui.setArticleTitle(title);
                        ui.setArticleText(ui.getArticleBuffer());
                        ui.setState(STATE_READING);
                     }
                }
            }
        }
        else if (state == STATE_RESULTS) {
             // ... (Keep existing logic) ...
            if (status.enter) {
                String title = ui.getResult(ui.getSelectedResultIndex());
                if (title.length() > 0) {
                    engine.loadArticle(title, ui.getArticleBuffer(), ui.getArticleBufferSize());
                    
                    // Use Helper
                    cleanWikiText(ui.getArticleBuffer());
                    
                    ui.setArticleTitle(title);
                    ui.setArticleText(ui.getArticleBuffer());
                    ui.setState(STATE_READING);
                }
            }
            else if (status.del) { ui.setState(STATE_SEARCH); }
            else if (status.tab) { ui.moveSelection(1); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { ui.moveSelection(1); }
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { ui.moveSelection(-1); }
        }
        else if (state == STATE_READING) {
            if (status.del && M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE)) { ui.setState(STATE_RESULTS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.') || status.tab) { ui.scrollReader(150); }
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { ui.scrollReader(-150); }
        }
    }
    
    delay(10);
}
