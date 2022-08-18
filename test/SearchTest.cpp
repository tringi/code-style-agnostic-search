#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <CommCtrl.h>
#include <CommDlg.h>
#include <ShellAPI.h>

#include <cstddef>

// using 'agsearch'

#include "../agsearch.h"

std::vector <std::wstring> file;
std::vector <std::pair <agsearch::location, agsearch::location>> results;

struct search : private agsearch {
    using agsearch::load;

    std::size_t find (std::wstring_view needle) {
        results.clear ();
        auto n = this->agsearch::find (needle);

        // TODO: invalidate hWnd
        // TODO: report 'n' found

        return n;
    }

private:
    virtual bool found (std::wstring_view needle, std::size_t index, location begin, location end) override {
        results.push_back ({ begin, end });
        return true;
    }
} search;



// Windows API windowing follows

#ifndef CP_UTF16
#define CP_UTF16 1200
#endif

extern "C" IMAGE_DOS_HEADER __ImageBase;
HFONT hCodeFont = NULL;
UINT fontHeight = 1;
UINT charWidth = 1;
UINT nParameters = 0;
wchar_t tmpstrbuffer [65536];

std::size_t GetFileSize (HANDLE h) {
    LARGE_INTEGER size;
    if (GetFileSizeEx (h, &size)) {
        return size.QuadPart;
    } else
        return 0;
}

void LoadFile (const wchar_t * path) {
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
}

void Paint (HDC hDC, RECT rc) {
    SelectObject (hDC, hCodeFont);
    SelectObject (hDC, GetStockObject (DC_PEN));
    SelectObject (hDC, GetStockObject (DC_BRUSH));

    FillRect (hDC, &rc, GetSysColorBrush (COLOR_WINDOW));

    SetBkMode (hDC, TRANSPARENT);
    SetDCPenColor (hDC, 0x806060);
    SetDCBrushColor (hDC, 0xE0D0D0);

    SIZE character;
    GetTextExtentPoint32 (hDC, L"W", 1, &character);

    for (auto result : results) {
        auto points = 4u;
        POINT sp [2] = {
            { character.cx * result.first.column, fontHeight * result.first.row },
            { character.cx * result.second.column, fontHeight * result.second.row },
        };
            
        POINT polygon [9];
        if (result.first.row == result.second.row) {
            polygon [0].x = sp [0].x; polygon [0].y = sp [0].y;
            polygon [1].x = sp [1].x; polygon [1].y = sp [0].y;
            polygon [2].x = sp [1].x; polygon [2].y = sp [0].y + fontHeight;
            polygon [3].x = sp [0].x; polygon [3].y = sp [0].y + fontHeight;
        } else {
            polygon [0].x = sp [0].x; polygon [0].y = sp [0].y;
            polygon [1].x = rc.right; polygon [1].y = sp [0].y;
            polygon [2].x = rc.right; polygon [2].y = sp [1].y;
            polygon [3].x = sp [1].x; polygon [3].y = sp [1].y;
            polygon [4].x = sp [1].x; polygon [4].y = sp [1].y + fontHeight;
            polygon [5].x = 0;        polygon [5].y = sp [1].y + fontHeight;
            polygon [6].x = 0;        polygon [6].y = sp [0].y + fontHeight;
            polygon [7].x = sp [0].x; polygon [7].y = sp [0].y + fontHeight;
            points = 8u;
        };

        for (auto & pt : polygon) {
            pt.x += rc.right / 4;
            pt.y += 7;
        }

        Polygon (hDC, polygon, points);
    }

    for (auto i = 0; i != file.size (); ++i) {
        TextOut (hDC, rc.right / 4, 7 + fontHeight * i, file [i].c_str (), file [i].length ());
        if (7 + fontHeight * i > rc.bottom)
            break;
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
    GetWindowText (hControl, &text [0], text.size () + 1);
    return text;
}

HFONT SetFonts (HWND hWnd) {
    auto hFont = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
    auto hCodeFont = (HFONT) GetStockObject (ANSI_FIXED_FONT);

    NONCLIENTMETRICS metrics {};
    metrics.cbSize = sizeof metrics;
    if (SystemParametersInfo (SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0)) {
        if (auto h = CreateFontIndirect (&metrics.lfMenuFont)) {
            hFont = h;
        }

        std::wcscpy (metrics.lfMenuFont.lfFaceName, L"Consolas");
        metrics.lfMenuFont.lfPitchAndFamily = FIXED_PITCH;
        
        if (metrics.lfMenuFont.lfHeight < 0) {
            metrics.lfMenuFont.lfHeight++;
        } else {
            metrics.lfMenuFont.lfHeight--;
        }

        if (auto h = CreateFontIndirect (&metrics.lfMenuFont)) {
            hCodeFont = h;
            if (metrics.lfMenuFont.lfHeight < 0) {
                fontHeight = -metrics.lfMenuFont.lfHeight;
            } else {
                fontHeight = metrics.lfMenuFont.lfHeight;
            }
        }
    }

    EnumChildWindows (hWnd,
                      [] (HWND hChild, LPARAM font) {
                          SendMessage (hChild, WM_SETFONT, font, TRUE);
                          return TRUE;
                      }, (LPARAM) hFont);
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
                
                while (true) {
                    auto s = LoadTmpString (1001 + nParameters);
                    if (*s) {
                        if (auto h = CreateWindow (L"BUTTON", s,
                                                   WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
                                                   0, 0, 0, 0, hWnd, (HMENU) (1001 + nParameters), cs->hInstance, NULL)) {
                            SendMessage (h, BM_SETCHECK, BST_CHECKED, 0);
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
                mmi->ptMinTrackSize.y = 220 + 20 * nParameters;
            }
            break;

        case WM_WINDOWPOSCHANGED:
            if (auto position = reinterpret_cast <const WINDOWPOS *> (lParam)) {
                if (!(position->flags & SWP_NOSIZE) || (position->flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED))) {
                    RECT client;
                    if (GetClientRect (hWnd, &client)) {
                        if (HDWP hDwp = BeginDeferWindowPos (2)) {

                            long Y = client.bottom - (120 + 20 * nParameters);
                            if (Y > 230) {
                                Y = 230;
                            }

                            DeferChildPos (hDwp, hWnd,  900, { 7,      7 }, { client.right / 4 - 14, 20 });
                            DeferChildPos (hDwp, hWnd,  901, { 7,     27 }, { client.right / 4 - 21,  Y });
                            DeferChildPos (hDwp, hWnd,  902, { 7, Y + 37 }, { client.right / 4 - 21, 60 });
                            DeferChildPos (hDwp, hWnd, 1000, { 7, Y + 97 }, { client.right / 4 - 14, 20 });

                            for (auto i = 0; i != nParameters; ++i) {
                                DeferChildPos (hDwp, hWnd, 1001 + i, { 7, Y + 117 + 20 * i }, { client.right / 4 - 14, 20 });
                            }

                            EndDeferWindowPos (hDwp);
                        }
                    }
                }
            }
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
                        LoadFile (tmpstrbuffer);
                        InvalidateRect (hWnd, NULL, FALSE);
                    }
                } break;

                case 901:
                    switch (HIWORD (wParam)) {
                        case EN_CHANGE:
                            search.find (GetCtrlText ((HWND) lParam));
                            InvalidateRect (hWnd, NULL, FALSE);
                            break;
                    }
                    break;

                default:
                    if (LOWORD (wParam) >= 1001 && LOWORD (wParam) < 1001 + nParameters) {
                        if (HIWORD (wParam) == BN_CLICKED) {

                            // TODO: update parameters
                            
                            
                            search.find (GetCtrlText (GetDlgItem (hWnd, 901)));
                            InvalidateRect (hWnd, NULL, FALSE);
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
                RECT client;
                if (GetClientRect (hWnd, &client)) {
                    Paint (hDC, client);
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

    if (auto atom = Initialize (hInstance)) {
        auto menu = LoadMenu (hInstance, MAKEINTRESOURCE (1));
        auto accs = LoadAccelerators (hInstance, MAKEINTRESOURCE (1));
        
        static const auto D = CW_USEDEFAULT;
        if (auto hWnd = CreateWindow (atom, LoadTmpString (1),
                                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, D,D,D,D,
                                      HWND_DESKTOP, menu, hInstance, NULL)) {
            ShowWindow (hWnd, nCmdShow);

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
                    LoadFile (lpCmdLine);
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