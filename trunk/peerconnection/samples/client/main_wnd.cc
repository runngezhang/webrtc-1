/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "peerconnection/samples/client/main_wnd.h"

#include <math.h>

#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/base/stringutils.h"

bool aec_on,agc_on,anc_on;
ATOM MainWnd::wnd_class_ = 0;
const wchar_t MainWnd::kClassName[] = L"WebRTC_MainWnd";

// TODO(tommi): declare in header:
std::string GetDefaultServerName();

namespace {

const char kConnecting[] = "Connecting... ";
const char kNoVideoStreams[] = "(no video streams either way)";
const char kNoIncomingStream[] = "(no incoming video)";

void CalculateWindowSizeForText(HWND wnd, const wchar_t* text,
                                size_t* width, size_t* height) {
  HDC dc = ::GetDC(wnd);
  RECT text_rc = {0};
  ::DrawText(dc, text, -1, &text_rc, DT_CALCRECT | DT_SINGLELINE);
  ::ReleaseDC(wnd, dc);
  RECT client, window;
  ::GetClientRect(wnd, &client);
  ::GetWindowRect(wnd, &window);

  *width = text_rc.right - text_rc.left;
  *width += (window.right - window.left) -
            (client.right - client.left);
  *height = text_rc.bottom - text_rc.top;
  *height += (window.bottom - window.top) -
             (client.bottom - client.top);
}

HFONT GetDefaultFont() {
  static HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  return font;
}

std::string GetWindowText(HWND wnd) {
  char text[MAX_PATH] = {0};
  ::GetWindowTextA(wnd, &text[0], ARRAYSIZE(text));
  return text;
}

void AddListBoxItem(HWND listbox, const std::string& str, LPARAM item_data) {
  LRESULT index = ::SendMessageA(listbox, LB_ADDSTRING, 0,
      reinterpret_cast<LPARAM>(str.c_str()));
  ::SendMessageA(listbox, LB_SETITEMDATA, index, item_data);
}

void DrawWhiteText(HDC dc, const RECT& rect, const char* text, int flags) {
  HGDIOBJ old_font = ::SelectObject(dc, GetDefaultFont());
  ::SetTextColor(dc, RGB(0xff, 0xff, 0xff));
  ::SetBkMode(dc, TRANSPARENT);
  RECT rc = rect;
  ::DrawTextA(dc, text, -1, &rc, flags);
  ::SelectObject(dc, old_font);
}

}  // namespace

MainWnd::MainWnd()
  : ui_(CONNECT_TO_SERVER), wnd_(NULL), edit1_(NULL), edit2_(NULL),
    label1_(NULL), label2_(NULL), button_(NULL), listbox_(NULL),
    destroyed_(false), callback_(NULL), nested_msg_(NULL),aec_(NULL),anc_(NULL),agc_(NULL),label_agc(NULL),label_anc(NULL),label_aec(NULL) {
}

MainWnd::~MainWnd() {
  ASSERT(!IsWindow());
}

bool MainWnd::Create() {
  ASSERT(wnd_ == NULL);
  if (!RegisterWindowClass())
    return false;

  ui_thread_id_ = ::GetCurrentThreadId();
  wnd_ = ::CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, kClassName, L"WebRTC",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      NULL, NULL, GetModuleHandle(NULL), this);

  ::SendMessage(wnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetDefaultFont()),
                TRUE);

  CreateChildWindows();
  SwitchToConnectUI();

  return wnd_ != NULL;
}

bool MainWnd::Destroy() {
  BOOL ret = FALSE;
  if (IsWindow()) {
    ret = ::DestroyWindow(wnd_);
  }

  return ret != FALSE;
}

void MainWnd::RegisterObserver(MainWndCallback* callback) {
  callback_ = callback;
}

bool MainWnd::IsWindow() {
  return wnd_ && ::IsWindow(wnd_) != FALSE;
}

bool MainWnd::PreTranslateMessage(MSG* msg) {
  bool ret = false;
  if (msg->message == WM_CHAR) {
    if (msg->wParam == VK_TAB) {
      HandleTabbing();
      ret = true;
    } else if (msg->wParam == VK_RETURN) {
      OnDefaultAction();
      ret = true;
    } else if (msg->wParam == VK_ESCAPE) {
      if (callback_) {
        if (ui_ == STREAMING) {
          callback_->DisconnectFromCurrentPeer();
        } else {
          callback_->DisconnectFromServer();
        }
      }
    }
  } else if (msg->hwnd == NULL && msg->message == UI_THREAD_CALLBACK) {
    callback_->UIThreadCallback(static_cast<int>(msg->wParam),
                                reinterpret_cast<void*>(msg->lParam));
    ret = true;
  }
  return ret;
}

void MainWnd::SwitchToConnectUI() {
  ASSERT(IsWindow());
  LayoutPeerListUI(false);
  ui_ = CONNECT_TO_SERVER;
  LayoutConnectUI(true);
  ::SetFocus(edit1_);
}

void MainWnd::SwitchToPeerList(const Peers& peers) {
  remote_video_.reset();
  local_video_.reset();

  LayoutConnectUI(false);

  ::SendMessage(listbox_, LB_RESETCONTENT, 0, 0);

  AddListBoxItem(listbox_, "List of currently connected peers:", -1);
  Peers::const_iterator i = peers.begin();
  for (; i != peers.end(); ++i)
    AddListBoxItem(listbox_, i->second.c_str(), i->first);

  ui_ = LIST_PEERS;
  LayoutPeerListUI(true);
  ::SetFocus(listbox_);
}

void MainWnd::SwitchToStreamingUI() {
  LayoutConnectUI(false);
  LayoutPeerListUI(false);
  ui_ = STREAMING;
}

void MainWnd::MessageBox(const char* caption, const char* text, bool is_error) {
  DWORD flags = MB_OK;
  if (is_error)
    flags |= MB_ICONERROR;

  ::MessageBoxA(handle(), text, caption, flags);
}

cricket::VideoRenderer* MainWnd::local_renderer() {
  if (!local_video_.get())
    local_video_.reset(new VideoRenderer(handle(), 1, 1));
  return local_video_.get();
}

cricket::VideoRenderer* MainWnd::remote_renderer() {
  if (!remote_video_.get())
    remote_video_.reset(new VideoRenderer(handle(), 1, 1));
  return remote_video_.get();
}

void MainWnd::QueueUIThreadCallback(int msg_id, void* data) {
  ::PostThreadMessage(ui_thread_id_, UI_THREAD_CALLBACK,
      static_cast<WPARAM>(msg_id), reinterpret_cast<LPARAM>(data));
}

void MainWnd::OnPaint() {
  PAINTSTRUCT ps;
  ::BeginPaint(handle(), &ps);

  RECT rc;
  ::GetClientRect(handle(), &rc);

  if (ui_ == STREAMING && remote_video_.get() && local_video_.get()) {
    AutoLock<VideoRenderer> local_lock(local_video_.get());
    AutoLock<VideoRenderer> remote_lock(remote_video_.get());

    const BITMAPINFO& bmi = remote_video_->bmi();
    int height = abs(bmi.bmiHeader.biHeight);
    int width = bmi.bmiHeader.biWidth;

    const uint8* image = remote_video_->image();
    if (image != NULL) {
      HDC dc_mem = ::CreateCompatibleDC(ps.hdc);
      ::SetStretchBltMode(dc_mem, HALFTONE);

      // Set the map mode so that the ratio will be maintained for us.
      HDC all_dc[] = { ps.hdc, dc_mem };
      for (int i = 0; i < ARRAY_SIZE(all_dc); ++i) {
        SetMapMode(all_dc[i], MM_ISOTROPIC);
        SetWindowExtEx(all_dc[i], width, height, NULL);
        SetViewportExtEx(all_dc[i], rc.right, rc.bottom, NULL);
      }

      HBITMAP bmp_mem = ::CreateCompatibleBitmap(ps.hdc, rc.right, rc.bottom);
      HGDIOBJ bmp_old = ::SelectObject(dc_mem, bmp_mem);

      POINT logical_area = { rc.right, rc.bottom };
      DPtoLP(ps.hdc, &logical_area, 1);

      HBRUSH brush = ::CreateSolidBrush(RGB(0, 0, 0));
      RECT logical_rect = {0, 0, logical_area.x, logical_area.y };
      ::FillRect(dc_mem, &logical_rect, brush);
      ::DeleteObject(brush);

      int max_unit = std::max(width, height);
      int x = (logical_area.x / 2) - (width / 2);
      int y = (logical_area.y / 2) - (height / 2);

      StretchDIBits(dc_mem, x, y, width, height,
                    0, 0, width, height, image, &bmi, DIB_RGB_COLORS, SRCCOPY);

      if ((rc.right - rc.left) > 200 && (rc.bottom - rc.top) > 200) {
        const BITMAPINFO& bmi = local_video_->bmi();
        image = local_video_->image();
        int thumb_width = bmi.bmiHeader.biWidth / 4;
        int thumb_height = abs(bmi.bmiHeader.biHeight) / 4;
        StretchDIBits(dc_mem,
            logical_area.x - thumb_width - 10,
            logical_area.y - thumb_height - 10,
            thumb_width, thumb_height,
            0, 0, bmi.bmiHeader.biWidth, -bmi.bmiHeader.biHeight,
            image, &bmi, DIB_RGB_COLORS, SRCCOPY);
      }

      BitBlt(ps.hdc, 0, 0, logical_area.x, logical_area.y,
             dc_mem, 0, 0, SRCCOPY);

      // Print the resolutions.
      char buffer[1024];
      talk_base::sprintfn(buffer, sizeof(buffer),
          "Remote: %u x %u.  Local: %u x %u.",
          remote_video_->frame_height(),
          remote_video_->frame_width(),
          local_video_->frame_height(),
          local_video_->frame_width());

      SetMapMode(ps.hdc, MM_TEXT);
      DrawWhiteText(ps.hdc, rc, buffer, DT_SINGLELINE);

      // Cleanup.
      ::SelectObject(dc_mem, bmp_old);
      ::DeleteObject(bmp_mem);
      ::DeleteDC(dc_mem);
    } else {
      // We're still waiting for the video stream to be initialized.
      HBRUSH brush = ::CreateSolidBrush(RGB(0, 0, 0));
      ::FillRect(ps.hdc, &rc, brush);
      ::DeleteObject(brush);

      std::string text(kConnecting);
      if (!local_video_->image()) {
        text += kNoVideoStreams;
      } else {
        text += kNoIncomingStream;
      }

      DrawWhiteText(ps.hdc, rc, text.c_str(),
                    DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    }
  } else {
    HBRUSH brush = ::CreateSolidBrush(::GetSysColor(COLOR_WINDOW));
    ::FillRect(ps.hdc, &rc, brush);
    ::DeleteObject(brush);
  }

  ::EndPaint(handle(), &ps);
}

void MainWnd::OnDestroyed() {
  PostQuitMessage(0);
}

void MainWnd::OnDefaultAction() {
  if (!callback_)
    return;
  
  std::string aec_string(GetWindowText(aec_));
  std::string agc_string(GetWindowText(agc_));
  std::string anc_string(GetWindowText(anc_));
  aec_on=aec_string.length()? atoi(aec_string.c_str()) : 0;
  agc_on=agc_string.length()? atoi(agc_string.c_str()) : 0;
  anc_on=anc_string.length()? atoi(anc_string.c_str()) : 0;
  if (ui_ == CONNECT_TO_SERVER) {
    std::string server(GetWindowText(edit1_));
    std::string port_str(GetWindowText(edit2_));
    int port = port_str.length() ? atoi(port_str.c_str()) : 0;
    callback_->StartLogin(server, port);
  } else if (ui_ == LIST_PEERS) {
    LRESULT sel = ::SendMessage(listbox_, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR) {
      LRESULT peer_id = ::SendMessage(listbox_, LB_GETITEMDATA, sel, 0);
      if (peer_id != -1 && callback_) {
        callback_->ConnectToPeer(peer_id);
      }
    }
  } else {
    MessageBoxA(wnd_, "OK!", "Yeah", MB_OK);
  }
}

bool MainWnd::OnMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) {
  switch (msg) {
    case WM_ERASEBKGND:
      *result = TRUE;
      return true;

    case WM_PAINT:
      OnPaint();
      return true;

    case WM_SETFOCUS:
      if (ui_ == CONNECT_TO_SERVER) {
        SetFocus(edit1_);
      } else if (ui_ == LIST_PEERS) {
        SetFocus(listbox_);
      }
      return true;

    case WM_SIZE:
      if (ui_ == CONNECT_TO_SERVER) {
        LayoutConnectUI(true);
      } else if (ui_ == LIST_PEERS) {
        LayoutPeerListUI(true);
      }
      break;

    case WM_CTLCOLORSTATIC:
      *result = reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
      return true;

    case WM_COMMAND:
      if (button_ == reinterpret_cast<HWND>(lp)) {
        if (BN_CLICKED == HIWORD(wp))
          OnDefaultAction();
      } else if (listbox_ == reinterpret_cast<HWND>(lp)) {
        if (LBN_DBLCLK == HIWORD(wp)) {
          OnDefaultAction();
        }
      }
      return true;

    case WM_CLOSE:
      if (callback_)
        callback_->Close();
      break;
  }
  return false;
}

// static
LRESULT CALLBACK MainWnd::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  MainWnd* me = reinterpret_cast<MainWnd*>(
      ::GetWindowLongPtr(hwnd, GWL_USERDATA));
  if (!me && WM_CREATE == msg) {
    CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
    me = reinterpret_cast<MainWnd*>(cs->lpCreateParams);
    me->wnd_ = hwnd;
    ::SetWindowLongPtr(hwnd, GWL_USERDATA, reinterpret_cast<LONG_PTR>(me));
  }

  LRESULT result = 0;
  if (me) {
    void* prev_nested_msg = me->nested_msg_;
    me->nested_msg_ = &msg;

    bool handled = me->OnMessage(msg, wp, lp, &result);
    if (WM_NCDESTROY == msg) {
      me->destroyed_ = true;
    } else if (!handled) {
      result = ::DefWindowProc(hwnd, msg, wp, lp);
    }

    if (me->destroyed_ && prev_nested_msg == NULL) {
      me->OnDestroyed();
      me->wnd_ = NULL;
      me->destroyed_ = false;
    }

    me->nested_msg_ = prev_nested_msg;
  } else {
    result = ::DefWindowProc(hwnd, msg, wp, lp);
  }

  return result;
}

// static
bool MainWnd::RegisterWindowClass() {
  if (wnd_class_)
    return true;

  WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
  wcex.style = CS_DBLCLKS;
  wcex.hInstance = GetModuleHandle(NULL);
  wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wcex.hCursor = ::LoadCursor(NULL, IDC_ARROW);
  wcex.lpfnWndProc = &WndProc;
  wcex.lpszClassName = kClassName;
  wnd_class_ = ::RegisterClassEx(&wcex);
  ASSERT(wnd_class_ != 0);
  return wnd_class_ != 0;
}

void MainWnd::CreateChildWindow(HWND* wnd, MainWnd::ChildWindowID id,
                                const wchar_t* class_name, DWORD control_style,
                                DWORD ex_style) {
  if (::IsWindow(*wnd))
    return;

  // Child windows are invisible at first, and shown after being resized.
  DWORD style = WS_CHILD | control_style;
  *wnd = ::CreateWindowEx(ex_style, class_name, L"", style,
                          100, 100, 100, 100, wnd_,
                          reinterpret_cast<HMENU>(id),
                          GetModuleHandle(NULL), NULL);
  ASSERT(::IsWindow(*wnd) != FALSE);
  ::SendMessage(*wnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetDefaultFont()),
                TRUE);
}

void MainWnd::CreateChildWindows() {
  // Create the child windows in tab order.
  CreateChildWindow(&label1_, LABEL1_ID, L"Static", ES_CENTER | ES_READONLY, 0);
  CreateChildWindow(&edit1_, EDIT_ID, L"Edit",
                    ES_LEFT | ES_NOHIDESEL | WS_TABSTOP, WS_EX_CLIENTEDGE);
  CreateChildWindow(&label2_, LABEL2_ID, L"Static", ES_CENTER | ES_READONLY, 0);
  CreateChildWindow(&edit2_, EDIT_ID, L"Edit",
                    ES_LEFT | ES_NOHIDESEL | WS_TABSTOP, WS_EX_CLIENTEDGE);
  CreateChildWindow(&button_, BUTTON_ID, L"Button", BS_CENTER | WS_TABSTOP, 0);

  CreateChildWindow(&listbox_, LISTBOX_ID, L"ListBox",
                    LBS_HASSTRINGS | LBS_NOTIFY, WS_EX_CLIENTEDGE);

  CreateChildWindow(&aec_, AEC_ID, L"Edit", ES_LEFT | ES_NOHIDESEL | WS_TABSTOP, WS_EX_CLIENTEDGE);
  CreateChildWindow(&label_aec, LABEL1_ID, L"Static", ES_CENTER | ES_READONLY, 0);
  CreateChildWindow(&agc_, AGC_ID, L"Edit", ES_LEFT | ES_NOHIDESEL | WS_TABSTOP, WS_EX_CLIENTEDGE);
  CreateChildWindow(&label_agc, LABEL1_ID, L"Static", ES_CENTER | ES_READONLY, 0);
  CreateChildWindow(&anc_, ANC_ID, L"Edit", ES_LEFT | ES_NOHIDESEL | WS_TABSTOP, WS_EX_CLIENTEDGE);
  CreateChildWindow(&label_anc, LABEL1_ID, L"Static", ES_CENTER | ES_READONLY, 0);

  ::SetWindowTextA(edit1_, GetDefaultServerName().c_str());
  ::SetWindowTextA(edit2_, "8888");
}

void MainWnd::LayoutConnectUI(bool show) {
  struct Windows {
    HWND wnd;
    const wchar_t* text;
    size_t width;
    size_t height;
  } windows[] = {
    { label1_, L"Server" },
    { edit1_, L"XXXyyyYYYgggXXXyyyYYYggg" },
    { label2_, L":" },
    { edit2_, L"XyXyX" },
    { button_, L"Connect" },
	{ aec_, L"X" },
	{ anc_, L"X" },
	{ agc_, L"X" },
	{ label_aec, L"aec"},
	{ label_anc, L"anc"},
	{ label_agc, L"agc"},
  };

  if (show) {
    const size_t kSeparator = 5;
    size_t total_width = (ARRAYSIZE(windows) - 1) * kSeparator;

    for (size_t i = 0; i < ARRAYSIZE(windows); ++i) {
      CalculateWindowSizeForText(windows[i].wnd, windows[i].text,
                                 &windows[i].width, &windows[i].height);
      total_width += windows[i].width;
    }

    RECT rc;
    ::GetClientRect(wnd_, &rc);
    size_t x = (rc.right / 2) - (total_width / 2);
    size_t y = rc.bottom / 2;
    for (size_t i = 0; i < ARRAYSIZE(windows); ++i) {
      size_t top = y - (windows[i].height / 2);
      ::MoveWindow(windows[i].wnd, x, top, windows[i].width, windows[i].height,
                   TRUE);
      x += kSeparator + windows[i].width;
      if (windows[i].text[0] != 'X')
        ::SetWindowText(windows[i].wnd, windows[i].text);
      ::ShowWindow(windows[i].wnd, SW_SHOWNA);
    }
  } else {
    for (size_t i = 0; i < ARRAYSIZE(windows); ++i) {
      ::ShowWindow(windows[i].wnd, SW_HIDE);
    }
  }
}

void MainWnd::LayoutPeerListUI(bool show) {
  if (show) {
    RECT rc;
    ::GetClientRect(wnd_, &rc);
    ::MoveWindow(listbox_, 0, 0, rc.right, rc.bottom, TRUE);
    ::ShowWindow(listbox_, SW_SHOWNA);
  } else {
    ::ShowWindow(listbox_, SW_HIDE);
    InvalidateRect(wnd_, NULL, TRUE);
  }
}

void MainWnd::HandleTabbing() {
  bool shift = ((::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
  UINT next_cmd = shift ? GW_HWNDPREV : GW_HWNDNEXT;
  UINT loop_around_cmd = shift ? GW_HWNDLAST : GW_HWNDFIRST;
  HWND focus = GetFocus(), next;
  do {
    next = ::GetWindow(focus, next_cmd);
    if (IsWindowVisible(next) &&
        (GetWindowLong(next, GWL_STYLE) & WS_TABSTOP)) {
      break;
    }

    if (!next) {
      next = ::GetWindow(focus, loop_around_cmd);
      if (IsWindowVisible(next) &&
          (GetWindowLong(next, GWL_STYLE) & WS_TABSTOP)) {
        break;
      }
    }
    focus = next;
  } while (true);
  ::SetFocus(next);
}

//
// MainWnd::VideoRenderer
//

MainWnd::VideoRenderer::VideoRenderer(HWND wnd, int width, int height)
    : wnd_(wnd), frame_width_(0), frame_height_(0) {
  ::InitializeCriticalSection(&buffer_lock_);
  ZeroMemory(&bmi_, sizeof(bmi_));
  bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi_.bmiHeader.biPlanes = 1;
  bmi_.bmiHeader.biBitCount = 32;
  bmi_.bmiHeader.biCompression = BI_RGB;
  bmi_.bmiHeader.biWidth = width;
  bmi_.bmiHeader.biHeight = -height;
  bmi_.bmiHeader.biSizeImage = width * height *
                              (bmi_.bmiHeader.biBitCount >> 3);
}

MainWnd::VideoRenderer::~VideoRenderer() {
  ::DeleteCriticalSection(&buffer_lock_);
}

bool MainWnd::VideoRenderer::SetSize(int width, int height, int reserved) {
  AutoLock<VideoRenderer> lock(this);

  bmi_.bmiHeader.biWidth = width;
  bmi_.bmiHeader.biHeight = -height;
  bmi_.bmiHeader.biSizeImage = width * height *
                               (bmi_.bmiHeader.biBitCount >> 3);
  image_.reset(new uint8[bmi_.bmiHeader.biSizeImage]);

  return true;
}

bool MainWnd::VideoRenderer::RenderFrame(const cricket::VideoFrame* frame) {
  if (!frame)
    return false;

  {
    AutoLock<VideoRenderer> lock(this);

    frame_height_ = frame->GetHeight();
    frame_width_ = frame->GetWidth();

    ASSERT(image_.get() != NULL);
    frame->ConvertToRgbBuffer(cricket::FOURCC_ARGB, image_.get(),
        bmi_.bmiHeader.biSizeImage,
        bmi_.bmiHeader.biWidth *
        (bmi_.bmiHeader.biBitCount >> 3));
  }

  InvalidateRect(wnd_, NULL, TRUE);

  return true;
}
