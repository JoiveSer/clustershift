#pragma once

#include "imgui.h"

namespace clustershift::gui {

// Linear / Raycast / Fluent Design System Tokens
struct Palette {
    static constexpr ImVec4 AppBg          = ImVec4(0.043f, 0.051f, 0.075f, 1.00f); // #0B0D13
    static constexpr ImVec4 SidebarBg      = ImVec4(0.067f, 0.078f, 0.114f, 1.00f); // #11141D
    static constexpr ImVec4 CardBg         = ImVec4(0.090f, 0.106f, 0.149f, 1.00f); // #171B26
    static constexpr ImVec4 CardHover      = ImVec4(0.110f, 0.130f, 0.180f, 1.00f); // #1C212E
    static constexpr ImVec4 Border         = ImVec4(1.000f, 1.000f, 1.000f, 0.07f); // rgba(255,255,255,0.07)
    static constexpr ImVec4 BorderSubtle   = ImVec4(1.000f, 1.000f, 1.000f, 0.04f); // rgba(255,255,255,0.04)

    static constexpr ImVec4 Accent         = ImVec4(0.055f, 0.647f, 0.914f, 1.00f); // #0EA5E9 (Sky Blue)
    static constexpr ImVec4 AccentLight    = ImVec4(0.220f, 0.741f, 0.973f, 1.00f); // #38BDF8
    static constexpr ImVec4 AccentDim      = ImVec4(0.055f, 0.647f, 0.914f, 0.15f); // rgba(14, 165, 233, 0.15)
    static constexpr ImVec4 AccentActive   = ImVec4(0.055f, 0.647f, 0.914f, 0.30f);

    static constexpr ImVec4 SuccessEmerald = ImVec4(0.063f, 0.725f, 0.506f, 1.00f); // #10B981 (Emerald)
    static constexpr ImVec4 WarningAmber   = ImVec4(0.961f, 0.620f, 0.043f, 1.00f); // #F59E0B (Amber)
    static constexpr ImVec4 ErrorRose      = ImVec4(0.957f, 0.267f, 0.365f, 1.00f); // #F43F5E

    static constexpr ImVec4 TextPrimary    = ImVec4(0.953f, 0.957f, 0.965f, 1.00f); // #F3F4F6
    static constexpr ImVec4 TextSecondary  = ImVec4(0.612f, 0.639f, 0.686f, 1.00f); // #9CA3AF
    static constexpr ImVec4 TextMuted      = ImVec4(0.420f, 0.447f, 0.502f, 1.00f); // #6B7280
};

inline void apply_modern_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Component Geometry & Proportions
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 999.0f; // Sleek pill scrollbar
    style.GrabRounding      = 999.0f;
    style.TabRounding       = 6.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(18.0f, 18.0f);
    style.FramePadding      = ImVec2(12.0f, 8.0f);
    style.ItemSpacing       = ImVec2(12.0f, 12.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize     = 8.0f;

    // Layered Dark Slate Palette
    colors[ImGuiCol_Text]                  = Palette::TextPrimary;
    colors[ImGuiCol_TextDisabled]          = Palette::TextMuted;
    colors[ImGuiCol_WindowBg]              = Palette::AppBg;
    colors[ImGuiCol_ChildBg]               = Palette::CardBg;
    colors[ImGuiCol_PopupBg]               = Palette::CardBg;
    colors[ImGuiCol_Border]                = Palette::Border;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]               = Palette::SidebarBg;
    colors[ImGuiCol_FrameBgHovered]        = Palette::CardHover;
    colors[ImGuiCol_FrameBgActive]         = Palette::AccentDim;
    colors[ImGuiCol_TitleBg]               = Palette::AppBg;
    colors[ImGuiCol_TitleBgActive]         = Palette::AppBg;
    colors[ImGuiCol_TitleBgCollapsed]      = Palette::AppBg;
    colors[ImGuiCol_MenuBarBg]             = Palette::SidebarBg;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(1.0f, 1.0f, 1.0f, 0.25f);
    colors[ImGuiCol_ScrollbarGrabActive]   = Palette::Accent;
    colors[ImGuiCol_CheckMark]             = Palette::AccentLight;
    colors[ImGuiCol_SliderGrab]            = Palette::AccentLight;
    colors[ImGuiCol_SliderGrabActive]      = Palette::Accent;
    colors[ImGuiCol_Button]                = Palette::SidebarBg;
    colors[ImGuiCol_ButtonHovered]         = Palette::CardHover;
    colors[ImGuiCol_ButtonActive]          = Palette::AccentDim;
    colors[ImGuiCol_Header]                = Palette::SidebarBg;
    colors[ImGuiCol_HeaderHovered]         = Palette::CardHover;
    colors[ImGuiCol_HeaderActive]          = Palette::AccentDim;
    colors[ImGuiCol_Separator]             = Palette::Border;
    colors[ImGuiCol_SeparatorHovered]      = Palette::Accent;
    colors[ImGuiCol_SeparatorActive]       = Palette::Accent;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_ResizeGripHovered]     = Palette::Accent;
    colors[ImGuiCol_ResizeGripActive]      = Palette::Accent;
    colors[ImGuiCol_Tab]                   = Palette::SidebarBg;
    colors[ImGuiCol_TabHovered]            = Palette::CardHover;
    colors[ImGuiCol_TabSelected]           = Palette::CardBg;
    colors[ImGuiCol_TabDimmed]             = Palette::SidebarBg;
    colors[ImGuiCol_TabDimmedSelected]     = Palette::CardBg;
    colors[ImGuiCol_PlotLines]             = Palette::AccentLight;
    colors[ImGuiCol_PlotLinesHovered]      = Palette::TextPrimary;
    colors[ImGuiCol_PlotHistogram]         = Palette::Accent;
    colors[ImGuiCol_PlotHistogramHovered]  = Palette::AccentLight;
    colors[ImGuiCol_TableHeaderBg]         = Palette::SidebarBg;
    colors[ImGuiCol_TableBorderStrong]     = Palette::Border;
    colors[ImGuiCol_TableBorderLight]      = Palette::BorderSubtle;
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    colors[ImGuiCol_TextSelectedBg]        = Palette::AccentDim;
    colors[ImGuiCol_DragDropTarget]        = Palette::Accent;
    colors[ImGuiCol_NavHighlight]          = Palette::Accent;
    colors[ImGuiCol_NavWindowingHighlight] = Palette::Accent;
}

} // namespace clustershift::gui
