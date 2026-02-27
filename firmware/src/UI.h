#ifndef UI_H
#define UI_H

#include <M5Cardputer.h>

enum AppState {
    STATE_SPLASH,
    STATE_MAIN_MENU,
    STATE_SEARCH,
    STATE_RESULTS,
    STATE_READING,
    STATE_ABOUT
};

class UI {
public:
    UI();
    void begin();
    void update();
    void draw(bool fullRedraw = true);

    void setStatusMessage(String msg);
    
    // State management
    void setState(AppState newState);
    AppState getState();

    // Data passing
    void setSearchQuery(String query);
    String getSearchQuery();
    void setResults(std::vector<String> results);
    int getSelectedResultIndex();
    String getResult(int index); // New helper
    
    // Uses pointer to shared buffer or internal static
    // Just needs to trigger redraw
    void setArticleText(const char* text); // No longer needed per se if engine writes to buffer?
    // Actually, Engine -> Buffer. UI needs access to buffer.
    // Let's expose UI's buffer getter? Or pass buffer to Engine.
    
    char* getArticleBuffer(); // Allow engine to write here
    uint32_t getArticleBufferSize();
    
    void setArticleTitle(String title);
    
    // Input handling helpers
    void moveSelection(int delta);
    void scrollReader(int delta);
    void handleInput(Keyboard_Class::KeysState status);

private:
    AppState _currentState;
    String _searchQuery;
    std::vector<String> _searchResults;
    int _selectedResultIndex;
    String _articleTitle;
    
    // Increased to 144KB for large articles (Shenzhen etc)
    // Allocated dynamically in begin() to fit in heap
    static const int VIEW_BUF_SIZE = 147456;
    char* _articleBuffer = nullptr;
    uint32_t _articleLen; // Track actual length
    
    SemaphoreHandle_t _uiMutex;
    
    int _scrollPosition;
    String _statusMsg;
    
    // Animation vars
    unsigned long _lastRefesh;
    int _animFrame;

    // Drawing methods
    void drawSplash();
    void drawMainMenu();
    void drawSearch(bool fullRedraw);
    void drawResults();
    void drawReader();
    void drawAbout();
    void drawStatusBar();
};

#endif
