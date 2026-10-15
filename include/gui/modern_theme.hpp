#pragma once

#include "imgui.h"

namespace clustershift::gui {

inline void apply_modern_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Geometry & Rounding
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(16.0f, 16.0f);
    style.FramePadding      = ImVec2(12.0f, 8.0f);
    style.ItemSpacing       = ImVec2(12.0f, 10.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);

    // Deep Obsidian Dark Palette (#0c0d14 / #141622) with Neon Cyan (#00f0ff) & Violet (#7928ca)
    const ImVec4 bg_obsidian      = ImVec4(0.047f, 0.051f, 0.078f, 1.00f);
    const ImVec4 card_dark        = ImVec4(0.078f, 0.086f, 0.133f, 1.00f);
    const ImVec4 card_light       = ImVec4(0.106f, 0.118f, 0.180f, 1.00f);
    const ImVec4 border_dim       = ImVec4(0.137f, 0.153f, 0.227f, 1.00f);
    const ImVec4 neon_cyan        = ImVec4(0.000f, 0.941f, 1.000f, 1.00f);
    const ImVec4 neon_cyan_dim    = ImVec4(0.000f, 0.941f, 1.000f, 0.35f);
    const ImVec4 accent_violet    = ImVec4(0.475f, 0.157f, 0.792f, 1.00f);
    const ImVec4 text_primary     = ImVec4(0.960f, 0.965f, 0.980f, 1.00f);
    const ImVec4 text_muted       = ImVec4(0.550f, 0.580f, 0.650f, 1.00f);

    colors[ImGuiCol_Text]                  = text_primary;
    colors[ImGuiCol_TextDisabled]          = text_muted;
    colors[ImGuiCol_WindowBg]              = bg_obsidian;
    colors[ImGuiCol_ChildBg]               = card_dark;
    colors[ImGuiCol_PopupBg]               = card_dark;
    colors[ImGuiCol_Border]                = border_dim;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]               = card_light;
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.137f, 0.153f, 0.235f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.180f, 0.200f, 0.310f, 1.00f);
    colors[ImGuiCol_TitleBg]               = bg_obsidian;
    colors[ImGuiCol_TitleBgActive]         = bg_obsidian;
    colors[ImGuiCol_TitleBgCollapsed]      = bg_obsidian;
    colors[ImGuiCol_MenuBarBg]             = card_dark;
    colors[ImGuiCol_ScrollbarBg]           = card_dark;
    colors[ImGuiCol_ScrollbarGrab]         = card_light;
    colors[ImGuiCol_ScrollbarGrabHovered]  = neon_cyan_dim;
    colors[ImGuiCol_ScrollbarGrabActive]   = neon_cyan;
    colors[ImGuiCol_CheckMark]             = neon_cyan;
    colors[ImGuiCol_SliderGrab]            = neon_cyan;
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.2f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_Button]                = card_light;
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.000f, 0.941f, 1.000f, 0.20f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.000f, 0.941f, 1.000f, 0.40f);
    colors[ImGuiCol_Header]                = card_light;
    colors[ImGuiCol_HeaderHovered]         = neon_cyan_dim;
    colors[ImGuiCol_HeaderActive]          = neon_cyan;
    colors[ImGuiCol_Separator]             = border_dim;
    colors[ImGuiCol_SeparatorHovered]      = neon_cyan;
    colors[ImGuiCol_SeparatorActive]       = neon_cyan;
    colors[ImGuiCol_ResizeGrip]            = border_dim;
    colors[ImGuiCol_ResizeGripHovered]     = neon_cyan;
    colors[ImGuiCol_ResizeGripActive]      = neon_cyan;
    colors[ImGuiCol_Tab]                   = card_dark;
    colors[ImGuiCol_TabHovered]            = ImVec4(0.137f, 0.153f, 0.235f, 1.00f);
    colors[ImGuiCol_TabSelected]           = card_light;
    colors[ImGuiCol_TabDimmed]             = card_dark;
    colors[ImGuiCol_TabDimmedSelected]     = card_light;
    colors[ImGuiCol_PlotLines]             = neon_cyan;
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    colors[ImGuiCol_PlotHistogram]         = neon_cyan;
    colors[ImGuiCol_PlotHistogramHovered]  = accent_violet;
    colors[ImGuiCol_TableHeaderBg]         = card_dark;
    colors[ImGuiCol_TableBorderStrong]     = border_dim;
    colors[ImGuiCol_TableBorderLight]      = border_dim;
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.059f, 0.063f, 0.098f, 0.50f);
    colors[ImGuiCol_TextSelectedBg]        = neon_cyan_dim;
    colors[ImGuiCol_DragDropTarget]        = neon_cyan;
    colors[ImGuiCol_NavHighlight]          = neon_cyan;
    colors[ImGuiCol_NavWindowingHighlight] = neon_cyan;
}

} // namespace clustershift::gui
