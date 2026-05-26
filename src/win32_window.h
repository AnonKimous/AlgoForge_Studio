#pragma once

#include "math_types.h"

#include <windows.h>

#include <string>

struct InputState {
  int mouse_x{};
  int mouse_y{};
  bool left_down{};
  bool left_pressed{};
  bool left_released{};
};

class Win32Window {
 public:
  Win32Window(HINSTANCE instance, int width, int height);
  ~Win32Window();

  bool ProcessMessages();
  HWND hwnd() const { return hwnd_; }
  HINSTANCE hinstance() const { return hinstance_; }
  int width() const { return width_; }
  int height() const { return height_; }
  const InputState& input() const { return input_; }
  Vec2 MousePosition() const;
  void SetTitle(const std::wstring& title);

 private:
  static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

  HINSTANCE hinstance_{};
  HWND hwnd_{};
  int width_{};
  int height_{};
  InputState input_{};
};
