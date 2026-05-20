#include "CoDriverVRPlugin.hpp"
#include "IPlugin.h"

namespace {
CoDriverVRPlugin* plugin = nullptr;
}

extern "C" __declspec(dllexport) IPlugin* RBR_CreatePlugin(IRBRGame* game)
{
    if (!plugin) {
        plugin = new CoDriverVRPlugin(game);
    }
    return plugin;
}
