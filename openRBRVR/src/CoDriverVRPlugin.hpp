#pragma once

#include "IPlugin.h"

#include <functional>
#include <string>
#include <vector>

class CoDriverVRPlugin : public IPlugin {
    struct Entry {
        std::function<std::string()> text;
        std::vector<std::string> help;
        std::function<void()> select;
        std::function<void()> left;
        std::function<void()> right;
    };

    IRBRGame* game;
    std::vector<Entry> entries;
    int selected = 0;

    void draw_entries();
    void clamp_selection();

public:
    explicit CoDriverVRPlugin(IRBRGame* game);
    virtual ~CoDriverVRPlugin(void) {}
    virtual const char* GetName(void) { return "CoDriverVR Passenger Room"; }
    virtual void DrawFrontEndPage(void);
    virtual void DrawResultsUI(void) {}
    virtual void HandleFrontEndEvents(char txtKeyboard, bool bUp, bool bDown, bool bLeft, bool bRight, bool bSelect);
    virtual void TickFrontEndPage(float fTimeDelta) {}
    virtual void StageStarted(int iMap, const char* ptxtPlayerName, bool bWasFalseStart) {}
    virtual void HandleResults(float fCheckPoint1, float fCheckPoint2, float fFinishTime, const char* ptxtPlayerName) {}
    virtual void CheckPoint(float fCheckPointTime, int iCheckPointID, const char* ptxtPlayerName) {}
};
