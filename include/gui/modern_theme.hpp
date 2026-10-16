#pragma once

#include "imgui.h"

namespace clustershift::gui {

// Unix/Linux Desktop (i3, Hyprland, Waybar) & Terminal Design System Tokens
// Inspired by Tokyo Night / Catppuccin Mocha
struct Palette {
    static constexpr ImVec4 AppBg          = ImVec4(0.055f, 0.063f, 0.082f, 1.00f); // #0E1015 (Deep matte dark)
    static constexpr ImVec4 BarBg          = ImVec4(0.071f, 0.078f, 0.110f, 1.00f); // #12141C (Polybar top bar)
    static constexpr ImVec4 PaneBg         = ImVec4(0.086f, 0.094f, 0.133f, 1.00f); // #161822 (Tiling window surface)
    static constexpr ImVec4 PaneHeader     = ImVec4(0.110f, 0.118f, 0.169f, 1.00f); // #1C1E2B (Titlebar background)
    static constexpr ImVec4 Border         = ImVec4(0.157f, 0.165f, 0.227f, 1.00f); // #282A3A (1px sharp border)
    static constexpr ImVec4 BorderSubtle   = ImVec4(0.118f, 0.125f, 0.173f, 1.00f); // #1E202C
    static constexpr ImVec4 BorderActive   = ImVec4(0.133f, 0.827f, 0.933f, 1.00f); // #22D3EE (Cyan-400 focus highlight)

    // Functional Accents
    static constexpr ImVec4 PromptGreen    = ImVec4(0.063f, 0.725f, 0.506f, 1.00f); // #10B981 (Emerald-400, CLI prompt $)
    static constexpr ImVec4 AccentCyan     = ImVec4(0.133f, 0.827f, 0.933f, 1.00f); // #22D3EE (Cyan-400)
    static constexpr ImVec4 AccentPurple   = ImVec4(0.753f, 0.518f, 0.988f, 1.00f); // #C084FC (Purple-400)
    static constexpr ImVec4 AccentDim      = ImVec4(0.133f, 0.827f, 0.933f, 0.12f); // rgba(34, 211, 238, 0.12)
    static constexpr ImVec4 AccentActive   = ImVec4(0.133f, 0.827f, 0.933f, 0.25f);

    // Status Signals
    static constexpr ImVec4 StatusWarn     = ImVec4(0.984f, 0.749f, 0.141f, 1.00f); // #FBBF24 (Amber-400, [WARN]/[BUSY])
    static constexpr ImVec4 StatusError    = ImVec4(0.984f, 0.443f, 0.522f, 1.00f); // #FB7185 (Rose-400, [FAIL]/[KILL])
    static constexpr ImVec4 StatusInfo     = ImVec4(0.376f, 0.647f, 0.980f, 1.00f); // #60A5FA (Blue-400, [INFO])

    // Monospace Text Hierarchy
    static constexpr ImVec4 TextPrimary    = ImVec4(0.953f, 0.957f, 0.965f, 1.00f); // #F3F4F6
    static constexpr ImVec4 TextSecondary  = ImVec4(0.612f, 0.639f, 0.686f, 1.00f); // #9CA3AF
    static constexpr ImVec4 TextMuted      = ImVec4(0.337f, 0.373f, 0.537f, 1.00f); // #565F89 (Tokyo Night muted)
};

inline void apply_modern_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Strict Tiling Window Manager Geometry (i3 / bspwm / Hyprland)
    // Zero bubbly curves, clean 1px borders, compact enterprise density
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 0.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.ItemSpacing       = ImVec2(6.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 8.0f;

    // Terminal & Tiling Palette Mapping
    colors[ImGuiCol_Text]                  = Palette::TextPrimary;
    colors[ImGuiCol_TextDisabled]          = Palette::TextMuted;
    colors[ImGuiCol_WindowBg]              = Palette::AppBg;
    colors[ImGuiCol_ChildBg]               = Palette::PaneBg;
    colors[ImGuiCol_PopupBg]               = Palette::PaneBg;
    colors[ImGuiCol_Border]                = Palette::Border;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]               = Palette::BarBg;
    colors[ImGuiCol_FrameBgHovered]        = Palette::PaneHeader;
    colors[ImGuiCol_FrameBgActive]         = Palette::AccentDim;
    colors[ImGuiCol_TitleBg]               = Palette::BarBg;
    colors[ImGuiCol_TitleBgActive]         = Palette::PaneHeader;
    colors[ImGuiCol_TitleBgCollapsed]      = Palette::BarBg;
    colors[ImGuiCol_MenuBarBg]             = Palette::BarBg;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(1.0f, 1.0f, 1.0f, 0.14f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(1.0f, 1.0f, 1.0f, 0.28f);
    colors[ImGuiCol_ScrollbarGrabActive]   = Palette::AccentCyan;
    colors[ImGuiCol_CheckMark]             = Palette::AccentCyan;
    colors[ImGuiCol_SliderGrab]            = Palette::AccentCyan;
    colors[ImGuiCol_SliderGrabActive]      = Palette::PromptGreen;
    colors[ImGuiCol_Button]                = Palette::BarBg;
    colors[ImGuiCol_ButtonHovered]         = Palette::PaneHeader;
    colors[ImGuiCol_ButtonActive]          = Palette::AccentDim;
    colors[ImGuiCol_Header]                = Palette::PaneHeader;
    colors[ImGuiCol_HeaderHovered]         = Palette::AccentDim;
    colors[ImGuiCol_HeaderActive]          = Palette::AccentActive;
    colors[ImGuiCol_Separator]             = Palette::Border;
    colors[ImGuiCol_SeparatorHovered]      = Palette::AccentCyan;
    colors[ImGuiCol_SeparatorActive]       = Palette::AccentCyan;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered]     = Palette::AccentCyan;
    colors[ImGuiCol_ResizeGripActive]      = Palette::AccentCyan;
    colors[ImGuiCol_Tab]                   = Palette::BarBg;
    colors[ImGuiCol_TabHovered]            = Palette::PaneHeader;
    colors[ImGuiCol_TabSelected]           = Palette::PaneBg;
    colors[ImGuiCol_TabDimmed]             = Palette::BarBg;
    colors[ImGuiCol_TabDimmedSelected]     = Palette::PaneBg;
    colors[ImGuiCol_PlotLines]             = Palette::AccentCyan;
    colors[ImGuiCol_PlotLinesHovered]      = Palette::PromptGreen;
    colors[ImGuiCol_PlotHistogram]         = Palette::AccentCyan;
    colors[ImGuiCol_PlotHistogramHovered]  = Palette::PromptGreen;
    colors[ImGuiCol_TableHeaderBg]         = Palette::BarBg;
    colors[ImGuiCol_TableBorderStrong]     = Palette::Border;
    colors[ImGuiCol_TableBorderLight]      = Palette::BorderSubtle;
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    colors[ImGuiCol_TextSelectedBg]        = Palette::AccentDim;
    colors[ImGuiCol_DragDropTarget]        = Palette::AccentCyan;
    colors[ImGuiCol_NavHighlight]          = Palette::AccentCyan;
    colors[ImGuiCol_NavWindowingHighlight] = Palette::AccentCyan;
}

} // namespace clustershift::gui
