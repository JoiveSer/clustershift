#pragma once

#include "imgui.h"

namespace clustershift::gui {

// Windows XP Luna Blue & Bliss Design System Tokens
struct XpPalette {
    static constexpr ImVec4 WindowBg         = ImVec4(0.925f, 0.914f, 0.847f, 1.00f); // #ECE9D8 (Classic XP Gray)
    static constexpr ImVec4 ContentBg        = ImVec4(1.000f, 1.000f, 1.000f, 1.00f); // #FFFFFF (Clean White)
    static constexpr ImVec4 TitleBlueLeft    = ImVec4(0.000f, 0.333f, 0.918f, 1.00f); // #0055EA (Royal Luna Blue)
    static constexpr ImVec4 TitleBlueMid     = ImVec4(0.220f, 0.553f, 0.965f, 1.00f); // #388DF6
    static constexpr ImVec4 CloseRed         = ImVec4(0.886f, 0.208f, 0.157f, 1.00f); // #E23528 (XP Red Close)
    static constexpr ImVec4 CloseRedHover    = ImVec4(0.960f, 0.350f, 0.300f, 1.00f);
    static constexpr ImVec4 StartGreen       = ImVec4(0.220f, 0.557f, 0.235f, 1.00f); // #388E3C (XP Start Green)
    static constexpr ImVec4 StartGreenHover  = ImVec4(0.300f, 0.680f, 0.320f, 1.00f);
    static constexpr ImVec4 TaskbarBlue      = ImVec4(0.141f, 0.369f, 0.863f, 1.00f); // #245EDC (XP Taskbar)
    static constexpr ImVec4 TrayBlue         = ImVec4(0.059f, 0.267f, 0.722f, 1.00f); // #0F44B8 (XP Tray)
    static constexpr ImVec4 SelectionBlue    = ImVec4(0.192f, 0.416f, 0.773f, 1.00f); // #316AC5 (XP Selection)
    static constexpr ImVec4 BorderBlue       = ImVec4(0.000f, 0.235f, 0.533f, 1.00f); // #003C88 (Dark Blue Border)
    static constexpr ImVec4 TextDark         = ImVec4(0.000f, 0.000f, 0.000f, 1.00f); // #000000
    static constexpr ImVec4 TextMuted        = ImVec4(0.350f, 0.350f, 0.350f, 1.00f); // #595959
    static constexpr ImVec4 TextBlueHeader   = ImVec4(0.000f, 0.200f, 0.600f, 1.00f); // #003399 (XP Section Header)
    static constexpr ImVec4 GoldCoin         = ImVec4(1.000f, 0.843f, 0.000f, 1.00f); // #FFD700
    static constexpr ImVec4 GoldCoinBg       = ImVec4(0.180f, 0.140f, 0.040f, 0.90f);
};

// Aliases for compatibility
using Palette = XpPalette;

inline void apply_modern_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Windows XP Luna Geometry
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 3.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 4.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(7.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 14.0f;

    // Authentic Windows XP Luna Color Scheme
    colors[ImGuiCol_Text]                  = ImVec4(0.00f, 0.00f, 0.00f, 1.00f); // Black text
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]              = XpPalette::WindowBg;                  // #ECE9D8
    colors[ImGuiCol_ChildBg]               = XpPalette::ContentBg;                 // #FFFFFF
    colors[ImGuiCol_PopupBg]               = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_Border]                = XpPalette::BorderBlue;                // #003C88
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.95f, 0.96f, 1.00f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.90f, 0.93f, 1.00f, 1.00f);
    colors[ImGuiCol_TitleBg]               = XpPalette::TitleBlueLeft;             // #0055EA
    colors[ImGuiCol_TitleBgActive]         = XpPalette::TitleBlueLeft;
    colors[ImGuiCol_TitleBgCollapsed]      = XpPalette::TitleBlueLeft;
    colors[ImGuiCol_MenuBarBg]             = XpPalette::WindowBg;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.78f, 0.82f, 0.90f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.68f, 0.74f, 0.85f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = XpPalette::SelectionBlue;
    colors[ImGuiCol_CheckMark]             = ImVec4(0.13f, 0.55f, 0.13f, 1.00f); // Green check
    colors[ImGuiCol_SliderGrab]            = XpPalette::TitleBlueLeft;
    colors[ImGuiCol_SliderGrabActive]      = XpPalette::SelectionBlue;
    colors[ImGuiCol_Button]                = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.98f, 0.88f, 0.58f, 1.00f); // XP Orange button hover!
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.95f, 0.78f, 0.40f, 1.00f);
    colors[ImGuiCol_Header]                = XpPalette::SelectionBlue;            // #316AC5
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.25f, 0.50f, 0.85f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.15f, 0.35f, 0.70f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = XpPalette::SelectionBlue;
    colors[ImGuiCol_SeparatorActive]       = XpPalette::SelectionBlue;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripHovered]     = XpPalette::SelectionBlue;
    colors[ImGuiCol_ResizeGripActive]      = XpPalette::SelectionBlue;
    colors[ImGuiCol_Tab]                   = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.95f, 0.90f, 0.75f, 1.00f);
    colors[ImGuiCol_TabSelected]           = XpPalette::WindowBg;
    colors[ImGuiCol_TabDimmed]             = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]     = XpPalette::WindowBg;
    colors[ImGuiCol_PlotLines]             = XpPalette::TitleBlueLeft;
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.88f, 0.20f, 0.15f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = XpPalette::TitleBlueLeft;
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.88f, 0.20f, 0.15f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.70f, 0.75f, 0.85f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.85f, 0.88f, 0.94f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.96f, 0.97f, 1.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]        = XpPalette::SelectionBlue;
    colors[ImGuiCol_DragDropTarget]        = XpPalette::SelectionBlue;
    colors[ImGuiCol_NavHighlight]          = XpPalette::SelectionBlue;
    colors[ImGuiCol_NavWindowingHighlight] = XpPalette::SelectionBlue;
}

} // namespace clustershift::gui
