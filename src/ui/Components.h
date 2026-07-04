#pragma once

#include <imgui.h>
#include <string>
#include <functional>

namespace UI {

// --- Windows ---

inline bool Begin(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0) {
    return ImGui::Begin(name, p_open, flags);
}

inline void End() { ImGui::End(); }

// --- Layout ---

inline void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f) {
    ImGui::SameLine(offset_from_start_x, spacing);
}

inline void Separator() { ImGui::Separator(); }
inline void Spacing() { ImGui::Spacing(); }
inline void Indent(float indent_w = 0.0f) { ImGui::Indent(indent_w); }
inline void Unindent(float indent_w = 0.0f) { ImGui::Unindent(indent_w); }
inline void NewLine() { ImGui::NewLine(); }

// --- Basic widgets ---

inline void Text(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

inline void TextColored(ImVec4 col, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextColoredV(col, fmt, args);
    va_end(args);
}

inline bool Button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    return ImGui::Button(label, size);
}

inline bool SmallButton(const char* label) {
    return ImGui::SmallButton(label);
}

inline bool InvisibleButton(const char* id, ImVec2 size) {
    return ImGui::InvisibleButton(id, size);
}

inline void Image(ImTextureID tex, ImVec2 size, ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1)) {
    ImGui::Image(tex, size, uv0, uv1);
}

// --- Checkbox / Radio ---

inline bool Checkbox(const char* label, bool* v) {
    return ImGui::Checkbox(label, v);
}

inline bool RadioButton(const char* label, bool active) {
    return ImGui::RadioButton(label, active);
}

inline bool RadioButton(const char* label, int* v, int v_button) {
    return ImGui::RadioButton(label, v, v_button);
}

// --- Sliders ---

inline bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::SliderFloat(label, v, v_min, v_max, format, flags);
}

inline bool SliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::SliderFloat2(label, v, v_min, v_max, format, flags);
}

inline bool SliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::SliderFloat3(label, v, v_min, v_max, format, flags);
}

inline bool SliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::SliderFloat4(label, v, v_min, v_max, format, flags);
}

inline bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0) {
    return ImGui::SliderInt(label, v, v_min, v_max, format, flags);
}

inline bool SliderAngle(const char* label, float* v_rad, float v_degrees_min = -360.0f, float v_degrees_max = +360.0f, const char* format = "%.0f deg", ImGuiSliderFlags flags = 0) {
    return ImGui::SliderAngle(label, v_rad, v_degrees_min, v_degrees_max, format, flags);
}

// --- Drag ---

inline bool DragFloat(const char* label, float* v, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::DragFloat(label, v, v_speed, v_min, v_max, format, flags);
}

inline bool DragFloat2(const char* label, float v[2], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::DragFloat2(label, v, v_speed, v_min, v_max, format, flags);
}

inline bool DragFloat3(const char* label, float v[3], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::DragFloat3(label, v, v_speed, v_min, v_max, format, flags);
}

inline bool DragFloat4(const char* label, float v[4], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    return ImGui::DragFloat4(label, v, v_speed, v_min, v_max, format, flags);
}

inline bool DragInt(const char* label, int* v, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0) {
    return ImGui::DragInt(label, v, v_speed, v_min, v_max, format, flags);
}

// --- Input ---

inline bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr) {
    return ImGui::InputText(label, buf, buf_size, flags, callback, user_data);
}

inline bool InputTextMultiline(const char* label, char* buf, size_t buf_size, ImVec2 size = ImVec2(0, 0), ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = nullptr, void* user_data = nullptr) {
    return ImGui::InputTextMultiline(label, buf, buf_size, size, flags, callback, user_data);
}

inline bool InputFloat(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", ImGuiInputTextFlags flags = 0) {
    return ImGui::InputFloat(label, v, step, step_fast, format, flags);
}

inline bool InputFloat2(const char* label, float v[2], const char* format = "%.3f", ImGuiInputTextFlags flags = 0) {
    return ImGui::InputFloat2(label, v, format, flags);
}

inline bool InputFloat3(const char* label, float v[3], const char* format = "%.3f", ImGuiInputTextFlags flags = 0) {
    return ImGui::InputFloat3(label, v, format, flags);
}

inline bool InputInt(const char* label, int* v, int step = 1, int step_fast = 100, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputInt(label, v, step, step_fast, flags);
}

// --- Color ---

inline bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0) {
    return ImGui::ColorEdit3(label, col, flags);
}

inline bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0) {
    return ImGui::ColorEdit4(label, col, flags);
}

inline bool ColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags = 0) {
    return ImGui::ColorPicker3(label, col, flags);
}

inline bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags = 0) {
    return ImGui::ColorPicker4(label, col, flags);
}

inline bool ColorButton(const char* desc_id, const ImVec4& col, ImGuiColorEditFlags flags = 0, ImVec2 size = ImVec2(0, 0)) {
    return ImGui::ColorButton(desc_id, col, flags, size);
}

// --- Combo / Select ---

inline bool Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1) {
    return ImGui::Combo(label, current_item, items_separated_by_zeros, popup_max_height_in_items);
}

inline bool Combo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items = -1) {
    return ImGui::Combo(label, current_item, items, items_count, popup_max_height_in_items);
}

// --- Tree ---

inline bool TreeNode(const char* label) { return ImGui::TreeNode(label); }
inline bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags = 0) { return ImGui::TreeNodeEx(label, flags); }
inline void TreePop() { ImGui::TreePop(); }
inline bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) { return ImGui::CollapsingHeader(label, flags); }
inline bool CollapsingHeader(const char* label, bool* p_visible, ImGuiTreeNodeFlags flags = 0) { return ImGui::CollapsingHeader(label, p_visible, flags); }

// --- Tabs ---

inline bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags = 0) { return ImGui::BeginTabBar(str_id, flags); }
inline void EndTabBar() { ImGui::EndTabBar(); }
inline bool BeginTabItem(const char* label, bool* p_open = nullptr, ImGuiTabItemFlags flags = 0) { return ImGui::BeginTabItem(label, p_open, flags); }
inline void EndTabItem() { ImGui::EndTabItem(); }

// --- Menu ---

inline bool BeginMenuBar() { return ImGui::BeginMenuBar(); }
inline void EndMenuBar() { ImGui::EndMenuBar(); }
inline bool BeginMenu(const char* label, bool enabled = true) { return ImGui::BeginMenu(label, enabled); }
inline void EndMenu() { ImGui::EndMenu(); }
inline bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true) {
    return ImGui::MenuItem(label, shortcut, selected, enabled);
}

// --- Tooltip ---

inline void BeginTooltip() { ImGui::BeginTooltip(); }
inline void EndTooltip() { ImGui::EndTooltip(); }
inline void SetTooltip(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::SetTooltipV(fmt, args);
    va_end(args);
}

// --- Popup ---

inline bool BeginPopup(const char* str_id, ImGuiWindowFlags flags = 0) { return ImGui::BeginPopup(str_id, flags); }
inline void EndPopup() { ImGui::EndPopup(); }
inline void OpenPopup(const char* str_id, ImGuiPopupFlags popup_flags = 0) { ImGui::OpenPopup(str_id, popup_flags); }

// --- Progress ---

inline void ProgressBar(float fraction, const ImVec2& size_arg = ImVec2(-FLT_MIN, 0), const char* overlay = nullptr) {
    ImGui::ProgressBar(fraction, size_arg, overlay);
}

// --- Separator Text (ImGui 1.90+) ---

inline void SeparatorText(const char* label) { ImGui::SeparatorText(label); }

// --- Columns (legacy but useful) ---

inline void Columns(int count = 1, const char* id = nullptr, bool border = true) { ImGui::Columns(count, id, border); }
inline void NextColumn() { ImGui::NextColumn(); }
inline int GetColumnIndex() { return ImGui::GetColumnIndex(); }
inline float GetColumnWidth(int column_index = -1) { return ImGui::GetColumnWidth(column_index); }

// --- Helpers ---

inline bool IsItemHovered(ImGuiHoveredFlags flags = 0) { return ImGui::IsItemHovered(flags); }
inline bool IsItemClicked(ImGuiMouseButton mouse_button = 0) { return ImGui::IsItemClicked(mouse_button); }
inline bool IsItemActive() { return ImGui::IsItemActive(); }
inline bool IsItemDeactivated() { return ImGui::IsItemDeactivated(); }
inline void SetNextItemWidth(float item_width) { ImGui::SetNextItemWidth(item_width); }
inline void SetNextItemOpen(bool is_open, ImGuiCond cond = 0) { ImGui::SetNextItemOpen(is_open, cond); }

} // namespace UI
