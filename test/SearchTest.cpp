#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <CommCtrl.h>
#include <CommDlg.h>
#include <ShellAPI.h>
#include <UXTheme.h>

#include <cstddef>
#include "../agsearch.h"

std::vector <std::wstring> file;
std::vector <std::pair <agsearch::location, agsearch::location>> results;

struct search : agsearch {
    using agsearch::pattern;

    std::size_t find (std::wstring_view needle) {
        ::results.clear ();
        return this->agsearch::find (needle);
    }

    virtual bool found (std::wstring_view needle, std::size_t index, location begin, location end) override {
        ::results.push_back ({ begin, end });
        return true;
    }

public:
    LARGE_INTEGER perfhz;
    LARGE_INTEGER perfT0;

    void start () {
        QueryPerformanceFrequency (&this->perfhz);
        QueryPerformanceCounter (&this->perfT0);
    }
    void report (HWND hWnd, bool full) const {
        LARGE_INTEGER perfT1;
        QueryPerformanceCounter (&perfT1);

        auto t = 1000.0 * double (perfT1.QuadPart - this->perfT0.QuadPart) / double (this->perfhz.QuadPart);

        wchar_t report [256];
        std::swprintf (report, 256, L"%u results in %.2f ms (%s)",
                       (unsigned) results.size (), t,
                       full ? L"FULL RESCAN AND SEARCH" : L"search only");
        SetDlgItemText (hWnd, 902, report);
        InvalidateRect (hWnd, NULL, FALSE);
    }
} search;



// Windows API windowing follows

#ifndef CP_UTF16
#define CP_UTF16 1200
#endif

extern "C" IMAGE_DOS_HEADER __ImageBase;
HFONT hCodeFont = NULL;
UINT charWidth = 1;
UINT nParameters = 0;
int wheelaccumulator = 0;
SCROLLINFO scrollbar = { sizeof (SCROLLINFO), 0, 0,0,0,0,0 };
wchar_t tmpstrbuffer [65536];

std::size_t GetFileSize (HANDLE h) {
    LARGE_INTEGER size;
    if (GetFileSizeEx (h, &size)) {
        return size.QuadPart;
    } else
        return 0;
}

void UpdateScrollBar (HWND hWnd) {
    scrollbar.fMask = SIF_DISABLENOSCROLL | SIF_PAGE | SIF_POS | SIF_RANGE;
    scrollbar.nMax = (int) file.size ();

    RECT rClient;
    if (GetClientRect (hWnd, &rClient)) {

        if (auto hDC = GetDC (hWnd)) {
            SelectObject (hDC, hCodeFont);

            SIZE character;
            if (GetTextExtentPoint32 (hDC, L"W", 1, &character)) {
                scrollbar.nPage = rClient.bottom / (character.cy - 2);
            }
            ReleaseDC (hWnd, hDC);
        }
    }

    SetScrollInfo (hWnd, SB_VERT, &scrollbar, TRUE);
}

void LoadFile (HWND hWnd, const wchar_t * path) {
    auto h = CreateFile (path, GENERIC_READ, 7, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h != INVALID_HANDLE_VALUE) {

        const auto size = GetFileSize (h);
        if (auto m = CreateFileMapping (h, NULL, PAGE_READONLY, 0, 0, NULL)) {
            if (auto p = (unsigned char *) MapViewOfFile (m, FILE_MAP_READ, 0, 0, 0)) {

                UINT cp = CP_ACP;
                if (size >= 2 && p [0] == 0xFF && p [1] == 0xFE) cp = CP_UTF16;
                if (size >= 3 && p [0] == 0xEF && p [1] == 0xBB && p [2] == 0xBF) cp = CP_UTF8;

                if (cp == CP_ACP) {
                    INT analysis = IS_TEXT_UNICODE_ASCII16 | IS_TEXT_UNICODE_STATISTICS | IS_TEXT_UNICODE_UTF8;
                    if (IsTextUnicode (p, size & 0x7FFFFFFF, &analysis)) {
                        cp = CP_UTF16;
                    } else
                    if (analysis & IS_TEXT_UNICODE_UTF8) {
                        cp = CP_UTF8;
                    }
                }

                file.clear ();

                if (cp == CP_UTF16) {
                    std::wstring line;
                    line.reserve (1024);

                    auto * text = (wchar_t *) p;
                    auto * end = text + size / sizeof (wchar_t);

                    for (; text != end; ++text) {
                        for (; (text != end) && (*text != '\n'); ++text) {
                            line.push_back (*text);
                        }
                        file.push_back (line);
                        line.clear ();
                    }
                } else {
                    std::string line;
                    std::vector <wchar_t> wbuffer;

                    line.reserve (1024);
                    wbuffer.resize (1024);

                    auto * text = (char *) p;
                    auto * end = text + size / sizeof (char);

                    for (; text != end; ++text) {
                        for (; (text != end ) && (*text != '\n'); ++text) {
                            line.push_back (*text);
                        }
                        
                        auto n = MultiByteToWideChar (cp, 0, line.data (), (int) line.size (), wbuffer.data (), (int) wbuffer.size ());
                        if (n == 0) {
                            if (GetLastError () == ERROR_INSUFFICIENT_BUFFER) {
                                n = MultiByteToWideChar (cp, 0, line.data (), (int) line.size (), NULL, 0);
                                wbuffer.resize (n);
                                n = MultiByteToWideChar (cp, 0, line.data (), (int) line.size (), wbuffer.data (), (int) wbuffer.size ());
                            }
                        }
                        if (n) {
                            file.push_back (std::wstring (wbuffer.data (), n));
                        }
                        line.clear ();
                    }
                }

                UnmapViewOfFile (p);
            }
            CloseHandle (m);
        }
        CloseHandle (h);
    }

    // reinitialize searching data
    //  - use search.replace function when only single line of code changes

    search.load (file);
    
    // clear GUI

    search.start ();
    search.find (L"");
    search.report (hWnd, true);

    // and rest...

    UpdateScrollBar (hWnd);
}

void Paint (HDC hDC, RECT rc) {
    SelectObject (hDC, hCodeFont);
    SelectObject (hDC, GetStockObject (DC_PEN));
    SelectObject (hDC, GetStockObject (DC_BRUSH));

    FillRect (hDC, &rc, GetSysColorBrush (COLOR_WINDOW));

    SetBkMode (hDC, TRANSPARENT);
    SetDCPenColor (hDC, 0x0000FF);
    SetDCBrushColor (hDC, 0xD0D0FF);

    SIZE character;
    GetTextExtentPoint32 (hDC, L"W", 1, &character);

    auto height = character.cy - 2;

    // visualize search results

    for (const auto & result : results) {
        auto points = 4u;
        POINT sp [2] = {
            { character.cx * (LONG) result.first.column, height * (LONG) result.first.row },
            { character.cx * (LONG) result.second.column, height * (LONG) result.second.row },
        };
            
        POINT polygon [9];
        if (result.first.row == result.second.row) {
            polygon [0].x = sp [0].x; polygon [0].y = sp [0].y;
            polygon [1].x = sp [1].x; polygon [1].y = sp [0].y;
            polygon [2].x = sp [1].x; polygon [2].y = sp [0].y + height;
            polygon [3].x = sp [0].x; polygon [3].y = sp [0].y + height;
        } else {
            polygon [0].x = sp [0].x; polygon [0].y = sp [0].y;
            polygon [1].x = rc.right; polygon [1].y = sp [0].y;
            polygon [2].x = rc.right; polygon [2].y = sp [1].y;
            polygon [3].x = sp [1].x; polygon [3].y = sp [1].y;
            polygon [4].x = sp [1].x; polygon [4].y = sp [1].y + height;
            polygon [5].x = 0;        polygon [5].y = sp [1].y + height;
            polygon [6].x = 0;        polygon [6].y = sp [0].y + height;
            polygon [7].x = sp [0].x; polygon [7].y = sp [0].y + height;
            points = 8u;
        };

        for (auto & pt : polygon) {
            pt.x += rc.right / 4;
            pt.y += 7 - height * scrollbar.nPos;
        }

        Polygon (hDC, polygon, points);
    }

    SetTextColor (hDC, 0x000000);
    for (std::size_t i = 0; i < scrollbar.nPage; ++i) {
        if (i + scrollbar.nPos < file.size ()) {
            const auto & line = file [i + scrollbar.nPos];
            TextOut (hDC, rc.right / 4, 7 + height * i, line.c_str (), (int) line.length ());
        } else
            break;
    }

    // visualize pattern

    search.pattern;

    rc.left += 5 * rc.right / 8;

    SetBkColor (hDC, 0xEEEEEE);

    for (auto & token : search.pattern) {
        switch (token.type) {
            case search::token::type::code: SetTextColor (hDC, 0x000000); break;
            case search::token::type::string: SetTextColor (hDC, 0x2233CC); break;
            case search::token::type::comment: SetTextColor (hDC, 0x339933); break;
            case search::token::type::identifier: SetTextColor (hDC, 0xCC6633); break;
            case search::token::type::numeric: SetTextColor (hDC, 0x990099); break;
        }

        if (token.type == search::token::type::string) {
            SetBkMode (hDC, OPAQUE);
        } else {
            SetBkMode (hDC, TRANSPARENT);
        }

        TextOut (hDC,
                 rc.left + character.cx * token.location.column,
                 rc.top + (character.cy - 2) * token.location.row + 7 - height * scrollbar.nPos,
                 token.value.c_str (), (int) token.value.length ());

        switch (token.type) {
            case search::token::type::identifier:
                /*if (token.alternative.length ()) {
                    SetBkMode (hDC, TRANSPARENT);
                    SetTextColor (hDC, 0xAA88AA);

                    TextOut (hDC,
                             rc.left + character.cx * (location.column + token.alternative.length () + 16),
                             rc.top + (character.cy - 2) * location.row + 7 - height * scrollbar.nPos,
                             token.alternative.c_str (), token.alternative.length ());
                }// */
                break;
            case search::token::type::numeric:
                /*SetBkMode (hDC, TRANSPARENT);
                SetTextColor (hDC, 0xAA88AA);

                wchar_t buffer [64];
                std::swprintf (buffer, 64, L"// %llu (%.3f)", token.integer, token.decimal);

                TextOut (hDC,
                         r.left + character.cx * (location.column + token.value.length () + 2),
                         r.top + (character.cy - 2) * location.row,
                         buffer, std::wcslen (buffer));*/
                break;
        }

        switch (token.string_type) {
            case 'L':
            case '8':
            case 'u':
            case 'U':
            case 'R':
                SetBkMode (hDC, TRANSPARENT);
                wchar_t st = token.string_type;
                TextOut (hDC,
                         rc.left + character.cx * token.location.column - 2 * character.cx / 3,
                         rc.top + (character.cy - 2) * token.location.row + character.cy / 2 + 7 - height * scrollbar.nPos,
                         &st, 1);
                break;
        }
    }
}

const wchar_t * LoadTmpString (UINT id) {
    if (LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase), id, tmpstrbuffer, sizeof tmpstrbuffer / sizeof tmpstrbuffer [0])) {
        return tmpstrbuffer;
    } else
        return L"";
}
void DeferChildPos (HDWP & hDwp, HWND hWnd, UINT id, const POINT & position, const SIZE & size, UINT flags = 0) {
    hDwp = DeferWindowPos (hDwp, GetDlgItem (hWnd, id), NULL, position.x, position.y, size.cx, size.cy, SWP_NOACTIVATE | SWP_NOZORDER | flags);
}
void InitOpenDlgMask (UINT id, wchar_t * mask, std::size_t size) {
    int m = 0;
    int n = 0;
    while (m = LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase), id, &mask [n], (int) (size - n - 1))) {
        n += m + 1;
        id++;
    }
    mask [n] = L'\0';
}

std::wstring GetCtrlText (HWND hControl) {
    std::wstring text;
    text.resize (GetWindowTextLength (hControl));
    GetWindowText (hControl, &text [0], (int) text.size () + 1);
    return text;
}

void SetParameterFont (HWND hWnd, HFONT hFont, std::size_t offset) {
    SendDlgItemMessage (hWnd, 1001 + (int) offset, WM_SETFONT, (WPARAM) hFont, TRUE);
}

HFONT SetFonts (HWND hWnd) {
    auto hFont = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
    auto hBoldFont = hFont;
    auto hCodeFont = (HFONT) GetStockObject (ANSI_FIXED_FONT);

    NONCLIENTMETRICS metrics {};
    metrics.cbSize = sizeof metrics;
    if (SystemParametersInfo (SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0)) {
        if (auto h = CreateFontIndirect (&metrics.lfMenuFont)) {
            hFont = h;
        }

        metrics.lfMenuFont.lfWeight = FW_BOLD;
        if (auto h = CreateFontIndirect (&metrics.lfMenuFont)) {
            hBoldFont = h;
        }

        std::wcscpy (metrics.lfMenuFont.lfFaceName, L"Consolas");
        metrics.lfMenuFont.lfPitchAndFamily = FIXED_PITCH;
        metrics.lfMenuFont.lfWeight = FW_NORMAL;;
        
        if (metrics.lfMenuFont.lfHeight < 0) {
            metrics.lfMenuFont.lfHeight++;
        } else {
            metrics.lfMenuFont.lfHeight--;
        }

        if (auto h = CreateFontIndirect (&metrics.lfMenuFont)) {
            hCodeFont = h;
        }
    }

    EnumChildWindows (hWnd,
                      [] (HWND hChild, LPARAM font) {
                          SendMessage (hChild, WM_SETFONT, font, TRUE);
                          return TRUE;
                      }, (LPARAM) hFont);
    
    SetParameterFont (hWnd, hBoldFont, offsetof (agsearch::parameter_set, individual_partial_words));
    SetParameterFont (hWnd, hBoldFont, offsetof (agsearch::parameter_set, orthogonal));
    SetParameterFont (hWnd, hBoldFont, offsetof (agsearch::parameter_set, numbers));
    SetParameterFont (hWnd, hBoldFont, offsetof (agsearch::parameter_set, match_snake_and_camel_casing));
    return hCodeFont;
}

LRESULT CALLBACK Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            try {
                auto cs = reinterpret_cast <const CREATESTRUCT *> (lParam);
                CreateWindowEx (0, L"STATIC", LoadTmpString (2), WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU) 900, cs->hInstance, NULL);
                CreateWindowEx (WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_VISIBLE | WS_CHILD | WS_TABSTOP |
                                WS_HSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | ES_AUTOVSCROLL |
                                ES_NOHIDESEL | ES_MULTILINE | ES_WANTRETURN, 0, 0, 0, 0, hWnd, (HMENU) 901, cs->hInstance, NULL);
                CreateWindowEx (0, L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU) 902, cs->hInstance, NULL);

                CreateWindowEx (0, L"STATIC", LoadTmpString (3), WS_VISIBLE | WS_CHILD | SS_LEFT, 0, 0, 0, 0, hWnd, (HMENU) 1000, cs->hInstance, NULL);
                
                auto pptr = reinterpret_cast <bool *> (&search.parameters);
                while (true) {
                    auto s = LoadTmpString (1001 + nParameters);
                    if (*s) {
                        if (auto h = CreateWindow (L"BUTTON", s,
                                                   WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                   0, 0, 0, 0, hWnd, (HMENU) (1001 + nParameters), cs->hInstance, NULL)) {
                            if (*pptr++) {
                                SendMessage (h, BM_SETCHECK, BST_CHECKED, 0);
                            }
                            ++nParameters;
                        }
                    } else
                        break;
                }
            
                hCodeFont = SetFonts (hWnd);
                return 0;
            } catch (...) {
                return -1;
            }

        case WM_GETMINMAXINFO:
            if (auto mmi = reinterpret_cast <MINMAXINFO *> (lParam)) {
                mmi->ptMinTrackSize.x = 600;
                mmi->ptMinTrackSize.y = 220 + 18 * nParameters;
            }
            break;

        case WM_WINDOWPOSCHANGED:
            if (auto position = reinterpret_cast <const WINDOWPOS *> (lParam)) {
                if (!(position->flags & SWP_NOSIZE) || (position->flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED))) {
                    RECT client;
                    if (GetClientRect (hWnd, &client)) {
                        if (HDWP hDwp = BeginDeferWindowPos (2)) {

                            long Y = client.bottom - (120 + 18 * nParameters);
                            if (Y > 230) {
                                Y = 230;
                            }

                            DeferChildPos (hDwp, hWnd,  900, { 7,      7 }, { client.right / 4 - 14, 20 });
                            DeferChildPos (hDwp, hWnd,  901, { 7,     27 }, { client.right / 4 - 21,  Y });
                            DeferChildPos (hDwp, hWnd,  902, { 7, Y + 37 }, { client.right / 4 - 21, 60 });
                            DeferChildPos (hDwp, hWnd, 1000, { 7, Y + 97 }, { client.right / 4 - 14, 20 });

                            for (auto i = 0; i != nParameters; ++i) {
                                DeferChildPos (hDwp, hWnd, 1001 + i, { 7, Y + 117 + 18 * i }, { client.right / 4 - 14, 18 });
                            }

                            EndDeferWindowPos (hDwp);
                        }
                    }
                }
                UpdateScrollBar (hWnd);
            }
            break;

        case WM_MOUSEWHEEL: {
            int lines = 0;
            if (!SystemParametersInfo (SPI_GETWHEELSCROLLLINES, 0, &lines, FALSE) || lines == 0) {
                lines = 3;
            }

            bool change = false;
            wheelaccumulator += (short) HIWORD (wParam);
            while (wheelaccumulator >= WHEEL_DELTA / lines) {
                wheelaccumulator -= WHEEL_DELTA / lines;
                --scrollbar.nPos;
                change = true;
            }
            while (wheelaccumulator <= -WHEEL_DELTA / lines) {
                wheelaccumulator += WHEEL_DELTA / lines;
                ++scrollbar.nPos;
                change = true;
            }
            if (change) {
                if (scrollbar.nPos < 0) {
                    scrollbar.nPos = 0;
                }
                if (scrollbar.nPos > scrollbar.nMax - (int) scrollbar.nPage) {
                    scrollbar.nPos = scrollbar.nMax - (int) scrollbar.nPage + 1;
                }
                UpdateScrollBar (hWnd);
                InvalidateRect (hWnd, NULL, FALSE);
            }
        } break;

        case WM_VSCROLL:
            switch (LOWORD (wParam)) {
                case SB_TOP:
                    scrollbar.nPos = 0;
                    break;
                case SB_LINEUP:
                    if (scrollbar.nPos) {
                        scrollbar.nPos--;
                    }
                    break;
                case SB_PAGEUP:
                    if (scrollbar.nPos >= (int) scrollbar.nPage) {
                        scrollbar.nPos -= (int) scrollbar.nPage;
                    } else {
                        scrollbar.nPos = 0;
                    }
                    break;
                case SB_LINEDOWN:
                    if (scrollbar.nPos <= scrollbar.nMax - (int) scrollbar.nPage) {
                        scrollbar.nPos++;
                    }
                    break;
                case SB_PAGEDOWN:
                    scrollbar.nPos += scrollbar.nPage;
                    if (scrollbar.nPos > scrollbar.nMax - (int) scrollbar.nPage) {
                        scrollbar.nPos = scrollbar.nMax - (int) scrollbar.nPage + 1;
                    }
                    break;

                case SB_BOTTOM:
                    if (scrollbar.nMax < (int) scrollbar.nPage) {
                        scrollbar.nPos = scrollbar.nMax - scrollbar.nPage;
                    }
                    break;

                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    scrollbar.fMask = SIF_TRACKPOS;
                    if (GetScrollInfo (hWnd, SB_VERT, &scrollbar)) {
                        scrollbar.nPos = scrollbar.nTrackPos;
                    }
            }
            UpdateScrollBar (hWnd);
            InvalidateRect (hWnd, NULL, FALSE);
            break;

        case WM_COMMAND:
            switch (LOWORD (wParam)) {
                case 0xF1:
                    ShellExecute (hWnd, NULL, LoadTmpString (0x0F), NULL, NULL, SW_SHOWDEFAULT);
                    break;
                case IDCLOSE:
                    PostMessage (hWnd, WM_CLOSE, 0, 0);
                    break;

                case 0xC0: {
                    wchar_t mask [2048];
                    InitOpenDlgMask (0xC1, mask, sizeof mask / sizeof mask [0]);

                    tmpstrbuffer [0] = L'\0';

                    OPENFILENAME ofn {};
                    ofn.lStructSize = sizeof ofn;
                    ofn.hInstance = reinterpret_cast <HINSTANCE> (&__ImageBase);
                    ofn.lpstrFilter = mask;
                    ofn.lpstrCustomFilter = NULL;
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFile = tmpstrbuffer;
                    ofn.nMaxFile = sizeof tmpstrbuffer / sizeof tmpstrbuffer [0];
                    ofn.lpstrFileTitle = NULL;
                    ofn.lpstrInitialDir = NULL;
                    ofn.lpstrTitle = NULL;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER | OFN_ENABLESIZING | OFN_DONTADDTORECENT;
                    ofn.lpstrDefExt = L"cpp";

                    if (GetOpenFileName (&ofn)) {
                        LoadFile (hWnd, tmpstrbuffer);
                        InvalidateRect (hWnd, NULL, FALSE);
                    }
                } break;

                case 901:
                    switch (HIWORD (wParam)) {
                        case EN_CHANGE:
                            search.start ();
                            search.find (GetCtrlText ((HWND) lParam));
                            search.report (hWnd, false);
                            break;
                    }
                    break;

                default:
                    if (LOWORD (wParam) >= 1001 && LOWORD (wParam) < 1001 + nParameters) {
                        if (HIWORD (wParam) == BN_CLICKED) {
                            reinterpret_cast <bool *> (&search.parameters) [LOWORD (wParam) - 1001] = (SendMessage ((HWND) lParam, BM_GETCHECK, 0, 0) == BST_CHECKED);
                            
                            search.start ();
                            search.load (file); // currently need to reload the file, most transformations are done at that time
                            search.find (GetCtrlText (GetDlgItem (hWnd, 901)));
                            search.report (hWnd, true);
                        }
                    }
            }
            break;

        case WM_CLOSE:
            PostQuitMessage ((int) wParam);
            break;
        case WM_ENDSESSION:
            if (wParam) {
                PostMessage (hWnd, WM_CLOSE, ERROR_SHUTDOWN_IN_PROGRESS, 0);
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            if (HDC hDC = BeginPaint (hWnd, &ps)) {

                HDC hBufferedDC = NULL;
                if (HPAINTBUFFER hPaintBuffer = BeginBufferedPaint (hDC, &ps.rcPaint, BPBF_COMPATIBLEBITMAP, NULL, &hBufferedDC)) {

                    RECT client;
                    if (GetClientRect (hWnd, &client)) {
                        Paint (hBufferedDC, client);
                    }

                    EndBufferedPaint (hPaintBuffer, TRUE);
                }
                EndPaint (hWnd, &ps);
            }
        } break;

        case WM_PRINTCLIENT: {
            RECT client;
            if (GetClientRect (hWnd, &client)) {
                Paint ((HDC) wParam, client);
                return true;
            }
        } break;

        case WM_CTLCOLORSTATIC:
            SetBkColor ((HDC) wParam, GetSysColor (COLOR_WINDOW));
            SetTextColor ((HDC) wParam, GetSysColor (COLOR_WINDOWTEXT));
        case WM_CTLCOLORBTN:
            return (LRESULT) GetSysColorBrush (COLOR_WINDOW);

        case WM_ERASEBKGND:
            return TRUE;

    }
    return DefWindowProc (hWnd, message, wParam, lParam);
}

LPCTSTR Initialize (HINSTANCE hInstance) {
    WNDCLASSEX wndclass = {
        sizeof (WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        Procedure, 0, 0, hInstance,  NULL,
        LoadCursor (NULL, IDC_ARROW), NULL,
        NULL, L"AGSEARCH", NULL
    };
    return (LPCTSTR) (std::intptr_t) RegisterClassEx (&wndclass);
}

int CALLBACK wWinMain (_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    InitCommonControls ();
    BufferedPaintInit ();

    if (auto atom = Initialize (hInstance)) {
        auto menu = LoadMenu (hInstance, MAKEINTRESOURCE (1));
        auto accs = LoadAccelerators (hInstance, MAKEINTRESOURCE (1));
        
        static const auto D = CW_USEDEFAULT;
        if (auto hWnd = CreateWindow (atom, LoadTmpString (1),
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL, D,D,D,D,
                                      HWND_DESKTOP, menu, hInstance, NULL)) {
            ShowWindow (hWnd, SW_MAXIMIZE);

            if (*lpCmdLine) {
                if (*lpCmdLine == '\"') {
                    ++lpCmdLine;

                    auto e = lpCmdLine + std::wcslen (lpCmdLine);
                    while (e [-1] != '\"' && e > lpCmdLine) {
                        --e;
                    }
                    e [-1] = '\0';
                }
                if (*lpCmdLine) {
                    LoadFile (hWnd, lpCmdLine);
                }
            }

            MSG message {};
            while (GetMessage (&message, NULL, 0u, 0u)) {
                auto root = GetAncestor (message.hwnd, GA_ROOT);
                if (!TranslateAccelerator (root, accs, &message)) {
                    if (!IsDialogMessage (root, &message)) {
                        TranslateMessage (&message);
                        DispatchMessage (&message);
                    }
                }
            }
            return (int) message.wParam;
        }
    }
    return (int) GetLastError ();
}
