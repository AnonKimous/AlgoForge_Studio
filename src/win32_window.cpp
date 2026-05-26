#include "win32_window.h"

#include <imgui_impl_win32.h>
#include <windowsx.h>

#include <string>
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace {

std::string LastErrorMessage(const char* prefix) {
  DWORD error = GetLastError();
  return std::string(prefix) + " failed, GetLastError=" + std::to_string(error);
}

}  // namespace

Win32Window::Win32Window(HINSTANCE instance, int width, int height)
    : hinstance_(instance), width_(width), height_(height) {
  const wchar_t* class_name = L"MeshEditorWindowClass";

  WNDCLASSW wc{};
  wc.lpfnWndProc = StaticWndProc;
  wc.hInstance = hinstance_;
  wc.lpszClassName = class_name;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw std::runtime_error("RegisterClassW failed");
  }

  RECT rect{0, 0, width_, height_};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

  hwnd_ = CreateWindowExW(
    0, class_name, L"Vulkan Mesh Vertex Editor", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
    nullptr, nullptr, hinstance_, this
  );
  if (!hwnd_) {
    throw std::runtime_error(LastErrorMessage("CreateWindowExW"));
  }
}

Win32Window::~Win32Window() {
  if (hwnd_) DestroyWindow(hwnd_);
}

bool Win32Window::ProcessMessages() {
  input_.left_pressed = false;
  input_.left_released = false;

  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) return false;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return true;
}

Vec2 Win32Window::MousePosition() const {
  return Vec2{static_cast<float>(input_.mouse_x), static_cast<float>(input_.mouse_y)};
}

void Win32Window::SetTitle(const std::wstring& title) {
  SetWindowTextW(hwnd_, title.c_str());
}

LRESULT CALLBACK Win32Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  Win32Window* window = nullptr;
  if (msg == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    window = reinterpret_cast<Win32Window*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
  } else {
    window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (window) return window->WndProc(hwnd, msg, wparam, lparam);
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
    return TRUE;
  }

  switch (msg) {
    case WM_NCCREATE:
      hwnd_ = hwnd;
      return TRUE;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_MOUSEMOVE:
      input_.mouse_x = GET_X_LPARAM(lparam);
      input_.mouse_y = GET_Y_LPARAM(lparam);
      return 0;
    case WM_SIZE:
      width_ = LOWORD(lparam);
      height_ = HIWORD(lparam);
      return 0;
    case WM_LBUTTONDOWN:
      input_.left_down = true;
      input_.left_pressed = true;
      SetCapture(hwnd);
      return 0;
    case WM_LBUTTONUP:
      input_.left_down = false;
      input_.left_released = true;
      ReleaseCapture();
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}
