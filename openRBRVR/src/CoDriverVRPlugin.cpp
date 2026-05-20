#include "CoDriverVRPlugin.hpp"
#include "DriverRoomControl.hpp"

#include <algorithm>
#include <format>

namespace {
auto text(const char* value)
{
    return [value] { return std::string(value); };
}

const char* mode_name(const std::string& mode)
{
    if (mode == "secure-lan") {
        return "HTTPS LAN";
    }
    return mode == "lan" ? "LAN" : "Public internet";
}
}

CoDriverVRPlugin::CoDriverVRPlugin(IRBRGame* game)
    : game(game)
{
    entries = {
        {
            .text = [] {
                const auto& status = driver_room::status();
                return std::format("Status: {}", status.summary);
            },
            .help = {
                "Shows the current local streamer and passenger-room state.",
                "The streamer must be running on localhost port 7790.",
            },
        },
        {
            .text = text("Refresh status"),
            .help = { "Refreshes the cached streamer status." },
            .select = [] { driver_room::refresh(true); },
        },
        {
            .text = [] {
                const auto& status = driver_room::status();
                return std::format("Mode: {}", mode_name(status.mode));
            },
            .help = {
                "Switches between LAN invite and HTTPS LAN invite.",
                "HTTPS LAN uses Cloudflare Tunnel for the page and signaling.",
                "Video still tries to connect directly over LAN.",
                "If a room is already running, switching mode recreates the room.",
            },
            .select = [] { driver_room::toggle_mode(); },
            .left = [] { driver_room::toggle_mode(); },
            .right = [] { driver_room::toggle_mode(); },
        },
        {
            .text = text("Start room"),
            .help = { "Starts the driver host, captures the RBR companion window, and creates an invite." },
            .select = [] { driver_room::start(); },
        },
        {
            .text = text("Stop room"),
            .help = { "Stops the current passenger room and closes the direct WebRTC peer." },
            .select = [] { driver_room::stop(); },
        },
        {
            .text = text("Recreate invite"),
            .help = { "Creates a fresh private invite link for the current mode." },
            .select = [] { driver_room::recreate(); },
        },
        {
            .text = [] {
                const auto& status = driver_room::status();
                return std::format("Driver mic: {}", status.mic_enabled ? "ON" : "OFF");
            },
            .help = { "Toggles the driver microphone in the passenger room." },
            .select = [] { driver_room::toggle_mic(); },
            .left = [] { driver_room::toggle_mic(); },
            .right = [] { driver_room::toggle_mic(); },
        },
        {
            .text = text("Open share page on Quest"),
            .help = { "Opens the invite sharing page on the driver's Quest using ADB when available." },
            .select = [] { driver_room::open_share_on_quest(); },
        },
        {
            .text = [] {
                const auto& status = driver_room::status();
                return std::format("Passenger: {}", status.passenger_joined ? "JOINED" : "waiting");
            },
            .help = { "Passenger join state from the room server." },
        },
        {
            .text = [] {
                const auto& status = driver_room::status();
                return std::format("Capture: {}", status.capture_source);
            },
            .help = { "The window or screen currently captured by the Electron driver host." },
        },
        {
            .text = [] {
                const auto& status = driver_room::status();
                if (status.direct_connected) {
                    return std::string("Direct WebRTC: connected");
                }
                if (status.direct_failed) {
                    return std::string("Direct WebRTC: failed");
                }
                return std::format("Direct WebRTC: {}", status.peer_state);
            },
            .help = { "Direct peer connection state. Relay/TURN is not enabled in the free build." },
        },
    };
}

void CoDriverVRPlugin::clamp_selection()
{
    if (entries.empty()) {
        selected = 0;
        return;
    }
    if (selected < 0) {
        selected = static_cast<int>(entries.size()) - 1;
    } else if (selected >= static_cast<int>(entries.size())) {
        selected = 0;
    }
}

void CoDriverVRPlugin::HandleFrontEndEvents(char txtKeyboard, bool bUp, bool bDown, bool bLeft, bool bRight, bool bSelect)
{
    if (bUp) {
        selected--;
        clamp_selection();
    }
    if (bDown) {
        selected++;
        clamp_selection();
    }
    if (bLeft && entries[selected].left) {
        entries[selected].left();
    }
    if (bRight && entries[selected].right) {
        entries[selected].right();
    }
    if (bSelect && entries[selected].select) {
        entries[selected].select();
    }
}

void CoDriverVRPlugin::draw_entries()
{
    constexpr auto row_height = 21.0f;
    constexpr auto x = 65.0f;
    constexpr auto y_start = 70.0f;

    game->SetFont(IRBRGame::EFonts::FONT_BIG);
    game->SetMenuColor(IRBRGame::EMenuColors::MENU_HEADING);
    game->WriteText(x, 49.0f, "CoDriverVR Passenger Room");

    game->SetMenuColor(IRBRGame::EMenuColors::MENU_TEXT);
    for (size_t i = 0; i < entries.size(); ++i) {
        game->WriteText(x, y_start + (static_cast<float>(i) * row_height), entries[i].text().c_str());
    }

    const auto help_top = y_start + (static_cast<float>(entries.size()) * row_height) + 18.0f;
    game->SetMenuColor(IRBRGame::EMenuColors::MENU_TEXT);
    for (size_t i = 0; i < entries[selected].help.size(); ++i) {
        game->WriteText(x, help_top + (static_cast<float>(i) * row_height), entries[selected].help[i].c_str());
    }
}

void CoDriverVRPlugin::DrawFrontEndPage()
{
    if (entries.empty()) {
        return;
    }

    constexpr auto row_height = 21.0f;
    constexpr auto y_start = 70.0f;
    const auto list_bottom = y_start + (static_cast<float>(entries.size()) * row_height);

    game->DrawBlackOut(0.0f, list_bottom, 800.0f, 10.0f);
    game->DrawSelection(0.0f, y_start - 2.0f + (static_cast<float>(selected) * row_height), 440.0f);
    draw_entries();
}
