#define IMGUI_DEFINE_MATH_OPERATORS
#include "ImGuiDefault.hpp"
#include <Gui/ImGui/ImGuizmoIncludes.hpp>

namespace SF::Engine
{
    namespace
    {
        // 0xRRGGBB -> Vec4
        constexpr Vec4 Hex(uint32_t rgb, float a = 1.0f)
        {
            return Vec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
        }
    } // namespace

    void ImGuiDefaultStyle::SetStyle()
    {
        ImGuiStyle &style = ImGui::GetStyle();
        Vec4 *colors      = style.Colors;

        // Geometry & Layout
        style.WindowRounding    = 0.0f; // Unreal panels are square, docked
        style.ChildRounding     = 0.0f;
        style.FrameRounding     = 3.0f; // rounded search box / fields
        style.PopupRounding     = 2.0f;
        style.ScrollbarRounding = 4.0f;
        style.GrabRounding      = 3.0f;
        style.TabRounding       = 0.0f;

        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize  = 0.0f;
        style.PopupBorderSize  = 1.0f;
        style.FrameBorderSize  = 1.0f;
        style.TabBorderSize    = 0.0f;

        style.WindowPadding    = Vec2(6.0f, 6.0f);
        style.FramePadding     = Vec2(6.0f, 3.0f);
        style.ItemSpacing      = Vec2(6.0f, 3.0f);
        style.ItemInnerSpacing = Vec2(4.0f, 3.0f);
        style.CellPadding      = Vec2(6.0f, 4.0f);
        style.IndentSpacing    = 14.0f;
        style.ScrollbarSize    = 10.0f;
        style.GrabMinSize      = 8.0f;
        style.WindowTitleAlign = Vec2(0.0f, 0.5f);

        // Palette (sampled from Unreal's editor)
        constexpr Vec4 bgRecessed   = Hex(0x0F0F0F); // inputs / search box / scrollbar track
        constexpr Vec4 bgTitle      = Hex(0x141414); // title bars / unselected tabs
        constexpr Vec4 bgWindow     = Hex(0x1A1A1A); // panel surface
        constexpr Vec4 bgPopup      = Hex(0x1F1F1F);
        constexpr Vec4 bgControl    = Hex(0x2C2C2C); // buttons / headers
        constexpr Vec4 bgHover      = Hex(0x383838);
        constexpr Vec4 bgActive     = Hex(0x454545);
        constexpr Vec4 borderSubtle = Hex(0x2A2A2A);
        constexpr Vec4 borderStrong = Hex(0x3A3A3A);
        constexpr Vec4 accent       = Hex(0x0070E0); // Unreal selection blue
        constexpr Vec4 accentHover  = Hex(0x2A8CF0);
        constexpr Vec4 textPrimary  = Hex(0xC0C0C0);
        constexpr Vec4 textDisabled = Hex(0x6E6E6E);

        // Text
        colors[ImGuiCol_Text]            = textPrimary;
        colors[ImGuiCol_TextDisabled]    = textDisabled;
        colors[ImGuiCol_TextLink]        = accentHover;
        colors[ImGuiCol_TextSelectedBg]  = Hex(0x0070E0, 0.40f);
        colors[ImGuiCol_InputTextCursor] = textPrimary;

        // Windows & Popups
        colors[ImGuiCol_WindowBg]  = bgWindow;
        colors[ImGuiCol_ChildBg]   = bgWindow;
        colors[ImGuiCol_PopupBg]   = bgPopup;
        colors[ImGuiCol_MenuBarBg] = bgTitle;

        // Borders & Separators
        colors[ImGuiCol_Border]           = borderSubtle;
        colors[ImGuiCol_BorderShadow]     = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_Separator]        = borderSubtle;
        colors[ImGuiCol_SeparatorHovered] = accentHover;
        colors[ImGuiCol_SeparatorActive]  = accent;

        // Title Bars
        colors[ImGuiCol_TitleBg]          = bgTitle;
        colors[ImGuiCol_TitleBgActive]    = bgPopup;
        colors[ImGuiCol_TitleBgCollapsed] = bgTitle;

        // Frames (inputs, checkboxes, sliders): very dark, bordered fields
        colors[ImGuiCol_FrameBg]        = bgRecessed;
        colors[ImGuiCol_FrameBgHovered] = Hex(0x181818);
        colors[ImGuiCol_FrameBgActive]  = Hex(0x202020);

        // Checkboxes & Sliders
        colors[ImGuiCol_CheckMark]          = Hex(0xFFFFFF);
        colors[ImGuiCol_CheckboxSelectedBg] = accent;
        colors[ImGuiCol_SliderGrab]         = Hex(0x5A5A5A);
        colors[ImGuiCol_SliderGrabActive]   = accent;

        // Buttons
        colors[ImGuiCol_Button]        = bgControl;
        colors[ImGuiCol_ButtonHovered] = bgHover;
        colors[ImGuiCol_ButtonActive]  = bgActive;

        // Headers (CollapsingHeader, TreeNode, Selectable, MenuItem)
        colors[ImGuiCol_Header]        = Hex(0x2C2C2C);
        colors[ImGuiCol_HeaderHovered] = bgHover;
        colors[ImGuiCol_HeaderActive]  = Hex(0x0070E0, 0.80f);

        // Scrollbars
        colors[ImGuiCol_ScrollbarBg]          = bgTitle;
        colors[ImGuiCol_ScrollbarGrab]        = borderStrong;
        colors[ImGuiCol_ScrollbarGrabHovered] = Hex(0x4A4A4A);
        colors[ImGuiCol_ScrollbarGrabActive]  = Hex(0x5A5A5A);

        // Resize Grips
        colors[ImGuiCol_ResizeGrip]        = Hex(0x3A3A3A, 0.50f);
        colors[ImGuiCol_ResizeGripHovered] = accentHover;
        colors[ImGuiCol_ResizeGripActive]  = accent;

        // Tabs
        colors[ImGuiCol_Tab]                       = bgTitle;
        colors[ImGuiCol_TabHovered]                = bgHover;
        colors[ImGuiCol_TabSelected]               = bgWindow;
        colors[ImGuiCol_TabSelectedOverline]       = accent;
        colors[ImGuiCol_TabDimmed]                 = bgTitle;
        colors[ImGuiCol_TabDimmedSelected]         = Hex(0x181818);
        colors[ImGuiCol_TabDimmedSelectedOverline] = borderStrong;

        // Docking
        colors[ImGuiCol_DockingPreview] = Hex(0x0070E0, 0.35f);
        colors[ImGuiCol_DockingEmptyBg] = bgRecessed;

        // Plots
        colors[ImGuiCol_PlotLines]            = Hex(0x8A8A8A);
        colors[ImGuiCol_PlotLinesHovered]     = accentHover;
        colors[ImGuiCol_PlotHistogram]        = accent;
        colors[ImGuiCol_PlotHistogramHovered] = accentHover;

        // Tables (Outliner / Details): visible rows + borders
        colors[ImGuiCol_TableHeaderBg]     = Hex(0x2B2B2B);
        colors[ImGuiCol_TableBorderStrong] = borderStrong;
        colors[ImGuiCol_TableBorderLight]  = borderSubtle;
        colors[ImGuiCol_TableRowBg]        = Hex(0x1C1C1C);
        colors[ImGuiCol_TableRowBgAlt]     = Hex(0x232323);

        // Hierarchy, Drag & Drop, Navigation & Modals
        colors[ImGuiCol_TreeLines]             = borderStrong;
        colors[ImGuiCol_DragDropTarget]        = accentHover;
        colors[ImGuiCol_DragDropTargetBg]      = Hex(0x0070E0, 0.20f);
        colors[ImGuiCol_UnsavedMarker]         = accentHover;
        colors[ImGuiCol_NavCursor]             = accent;
        colors[ImGuiCol_NavWindowingHighlight] = Hex(0xFFFFFF, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]     = Hex(0x000000, 0.60f);
        colors[ImGuiCol_ModalWindowDimBg]      = Hex(0x000000, 0.65f);
    }
} // namespace SF::Engine
