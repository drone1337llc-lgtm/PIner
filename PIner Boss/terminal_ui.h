#ifndef TERMINAL_UI_H
#define TERMINAL_UI_H

#include <string>
#include <cstdint>
#include <vector>
#include "common.h"

class TerminalUI {
public:
    TerminalUI();
    ~TerminalUI();
    
    void init();
    void cleanup();
    void update(const DisplayStats& stats);
    void hideCursor();
    void showCursor();
    void addLogMessage(const std::string& msg);
    
private:
    bool m_initialized;
    int m_screen_height;
    
    std::string drawHeader();
    std::string drawStats(const DisplayStats& stats);
    std::string drawWorkers(const DisplayStats& stats);
    std::string drawPool(const DisplayStats& stats);
    std::string drawFooter();
    std::string drawBar(double percent, int width);
    std::string fmtRate(double rate);
    std::string fmtTime(uint64_t sec);
};

// Global UI instance declaration
extern TerminalUI g_ui;

#endif // TERMINAL_UI_H
