#include "UI.h"
#include "Arial.h"

// ASCII Art for Splash (Simpler, cleaner font)
const char* ascii_art[] = {
    " _       _ _    _ ",
    "| |     | (_)  (_)",
    "| |  _  | |_| | ___ ",
    "| | | | | | |/ /| |",
    "| |_| |_| |   < | |",
    " \\___/(_)_|_|\\_\\|_|"
};

const char* logo_art[] = {
    " __      __  _   _     _ ",
    " \\ \\    / / (_) | |   (_)",
    "  \\ \\/\\/ /   _  | | __ _ ",
    "   \\    /   | | | |/ /| |",
    "    \\/\\/    |_| |_|\\_\\|_|"
};

UI::UI() {
    _currentState = STATE_SPLASH;
    _searchQuery = "";
    _selectedResultIndex = 0;
    _scrollPosition = 0;
    _animFrame = 0;
}

void UI::begin() {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE); 
    // Init buffer (Heap)
    if (!_articleBuffer) {
        _articleBuffer = (char*)malloc(VIEW_BUF_SIZE);
        if (!_articleBuffer) {
            M5Cardputer.Display.fillScreen(RED);
            M5Cardputer.Display.setCursor(10, 10);
            M5Cardputer.Display.println("OOM: UI Buffer");
            while(1);
        }
    }
    
    _articleBuffer[0] = 0; 
    _articleLen = 0;
    
    _uiMutex = xSemaphoreCreateMutex();
}

void UI::setState(AppState newState) {
    _currentState = newState;
    M5Cardputer.Display.fillScreen(BLACK); // Clear on state change
    M5Cardputer.Display.setFont(&Arial6pt16b);

    if (newState == STATE_SEARCH) {
        // Keep query? 
    } else if (newState == STATE_RESULTS) {
        _selectedResultIndex = 0;
    } else if (newState == STATE_READING) {
        _scrollPosition = 0;
    }
    
    draw(true); // Immediate redraw
}

AppState UI::getState() {
    return _currentState;
}

void UI::setSearchQuery(String query) {
    _searchQuery = query;
}

String UI::getSearchQuery() {
    return _searchQuery;
}

void UI::setResults(std::vector<String> results) {
    if (_uiMutex) xSemaphoreTake(_uiMutex, portMAX_DELAY);
    _searchResults = results;
    if (_uiMutex) xSemaphoreGive(_uiMutex);
}

int UI::getSelectedResultIndex() {
    int idx;
    if (_uiMutex) xSemaphoreTake(_uiMutex, portMAX_DELAY);
    idx = _selectedResultIndex;
    if (_uiMutex) xSemaphoreGive(_uiMutex);
    return idx;
}

String UI::getResult(int index) {
    String res = "";
    if (_uiMutex) xSemaphoreTake(_uiMutex, portMAX_DELAY);
    if (index >= 0 && index < _searchResults.size()) {
        res = _searchResults[index];
    }
    if (_uiMutex) xSemaphoreGive(_uiMutex);
    return res;
}

char* UI::getArticleBuffer() {
    return _articleBuffer;
}

uint32_t UI::getArticleBufferSize() {
    return VIEW_BUF_SIZE;
}

void UI::setArticleText(const char* text) {
    if (_articleBuffer) {
        strncpy(_articleBuffer, text, VIEW_BUF_SIZE - 1);
        _articleBuffer[VIEW_BUF_SIZE - 1] = 0;
        _articleLen = strlen(_articleBuffer);
    }
}

void UI::setArticleTitle(String title) {
    _articleTitle = title;
}

void UI::moveSelection(int delta) {
    if (_currentState == STATE_RESULTS) {
        if (_uiMutex) xSemaphoreTake(_uiMutex, portMAX_DELAY);
        int newIndex = _selectedResultIndex + delta;
        if (newIndex >= 0 && newIndex < _searchResults.size()) {
            _selectedResultIndex = newIndex;
        }
        if (_uiMutex) xSemaphoreGive(_uiMutex);
        draw(false); 
    }
}

void UI::scrollReader(int delta) {
    if (_currentState == STATE_READING) {
        _scrollPosition += delta;
        if (_scrollPosition < 0) _scrollPosition = 0;
        draw(false);
    }
}

void UI::update() {
    // Animation updates
    if (_currentState == STATE_SPLASH) {
        _animFrame++; 
        // Simple loop for shimmer or type-on
        if (_animFrame > 100) _animFrame = 0; 
    }
}

void UI::draw(bool fullRedraw) {
    switch (_currentState) {
        case STATE_SPLASH: drawSplash(); break;
        case STATE_MAIN_MENU: drawMainMenu(); break;
        case STATE_SEARCH: drawSearch(fullRedraw); break;
        case STATE_RESULTS: drawResults(); break;
        case STATE_READING: drawReader(); break;
        case STATE_ABOUT: drawAbout(); break;
    }
    drawStatusBar();
}

void UI::drawStatusBar() {
    // Optional
}

void UI::drawSplash() {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextSize(1); 
    M5Cardputer.Display.setFont(NULL);

    int y = 30;
    for (int i=0; i<5; i++) {
        M5Cardputer.Display.setCursor(40, y + (i*10));
        
        int scanRow = (_animFrame / 2) % 8; 
        if (i == scanRow || i == scanRow-1) {
            M5Cardputer.Display.setTextColor(GREEN);
        } else {
            M5Cardputer.Display.setTextColor(DARKGREY);
        }
        
        if (_animFrame > i*5) {
            M5Cardputer.Display.println(logo_art[i]); 
        }
    }
    
    if (_animFrame > 10) {
        M5Cardputer.Display.setTextSize(2); 
        M5Cardputer.Display.setCursor(90, 85); 
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.println("puter");
        
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(75, 120); 
        M5Cardputer.Display.setTextColor(LIGHTGREY);
        M5Cardputer.Display.print("AR://WEAVEFRONT");
        
        M5Cardputer.Display.setCursor(185, 120);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.print("v1.2(CYR)");
    }

    M5Cardputer.Display.setFont(&Arial6pt16b);
}

void UI::drawMainMenu() {
    M5Cardputer.Display.fillScreen(BLACK);
    // Start at search
}

void UI::drawSearch(bool fullRedraw) {
    if (fullRedraw) {
         M5Cardputer.Display.fillScreen(BLACK);
         M5Cardputer.Display.fillRect(0, 0, 240, 30, BLUE);
         M5Cardputer.Display.setTextColor(WHITE);
         M5Cardputer.Display.setTextSize(1);
         M5Cardputer.Display.setCursor(10, 6);
         M5Cardputer.Display.print("Search Wiki");
         M5Cardputer.Display.drawRect(10, 50, 220, 40, WHITE);
         M5Cardputer.Display.fillRect(0, 90, 240, 150, BLACK);
    }
    
    M5Cardputer.Display.fillRect(11, 51, 218, 38, BLACK);
    M5Cardputer.Display.setCursor(20, 60);
    M5Cardputer.Display.setTextSize(1); 
    M5Cardputer.Display.setTextColor(GREEN); 
    M5Cardputer.Display.print(_searchQuery);
    if (millis() % 1000 < 500) M5Cardputer.Display.print("_"); 
    
    if (fullRedraw) {
        if (_uiMutex) xSemaphoreTake(_uiMutex, portMAX_DELAY);
        
        if (_searchQuery.length() > 0 && _searchResults.size() > 0) {
            int listY = 90;
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(10, listY - 10);
            M5Cardputer.Display.setTextColor(YELLOW);
            
            int maxItems = 6;
            for (int i=0; i < maxItems && i < _searchResults.size(); i++) {
                 M5Cardputer.Display.setCursor(15, listY + (i * 15));
                 M5Cardputer.Display.setTextColor(LIGHTGREY);
                 M5Cardputer.Display.print(_searchResults[i]);
            }
        } else {
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(10, 100);
            M5Cardputer.Display.setTextColor(LIGHTGREY);
            M5Cardputer.Display.print("Type query... Results appear here.");
        }
        
        if (_uiMutex) xSemaphoreGive(_uiMutex);
    }
}

void UI::drawResults() {
    M5Cardputer.Display.fillScreen(BLACK);
    
    M5Cardputer.Display.fillRect(0, 0, 240, 25, ORANGE);
    M5Cardputer.Display.setTextColor(BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, 4);
    M5Cardputer.Display.print("Results");
    
    int startY = 35;
    int lineHeight = 15; 
    M5Cardputer.Display.setTextSize(1);
    
    if (_uiMutex) xSemaphoreTake(_uiMutex, portMAX_DELAY);

    int maxItems = 6;
    int startIdx = 0; 
    
    if (_selectedResultIndex >= maxItems) {
        startIdx = _selectedResultIndex - maxItems + 1;
    }
    
    for (int i = 0; i < maxItems && (startIdx + i) < _searchResults.size(); i++) {
        int idx = startIdx + i;
        int y = startY + (i * lineHeight);
        
        if (idx == _selectedResultIndex) {
            M5Cardputer.Display.fillRect(0, y-2, 240, lineHeight, WHITE);
            M5Cardputer.Display.setTextColor(BLACK);
        } else {
            M5Cardputer.Display.setTextColor(WHITE);
        }
        
        M5Cardputer.Display.setCursor(10, y);
        M5Cardputer.Display.print(_searchResults[idx]);
    }
    
    if (_uiMutex) xSemaphoreGive(_uiMutex);
}

void UI::drawReader() {
    M5Cardputer.Display.fillScreen(BLACK);

    M5Cardputer.Display.fillRect(0, 0, 240, 25, DARKGREY); 
    M5Cardputer.Display.setCursor(5, 5);
    M5Cardputer.Display.setTextSize(1); 
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.print(_articleTitle.substring(0, 18));
    
    int contentY = 30; 
    M5Cardputer.Display.setCursor(0, contentY); 
    
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE);
    
    if (_articleBuffer && _scrollPosition < strlen(_articleBuffer)) {
        M5Cardputer.Display.print(_articleBuffer + _scrollPosition);
    }
    
    if (_articleBuffer) {
        int totalLen = strlen(_articleBuffer);
        if (totalLen > 0) {
            int viewH = 135 - contentY;
            int barH = viewH * 300 / totalLen; 
            
            if (barH < 5) barH = 5;
            if (barH > viewH) barH = viewH;
            
            int barY = contentY + ((viewH - barH) * _scrollPosition / totalLen);
            
            M5Cardputer.Display.fillRect(235, barY, 5, barH, LIGHTGREY);
        }
    }
}

void UI::drawAbout() {
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setCursor(10, 10);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(CYAN);
    M5Cardputer.Display.println("WikiPuter");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.println("\nOffline Wikipedia Reader\nFor M5Cardputer Adv.\n\nCreated with Gemini.");
    M5Cardputer.Display.println("\nPress Enter/Del to Return");
}
