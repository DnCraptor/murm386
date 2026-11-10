#!/usr/bin/env python3
"""Write a Win16 program built by Open Watcom 2.0 (wcl -bt=windows -l=windows
-ms [-ox]), with the compiler's own start-up code (InitTask, WaitEvent,
InitApp, GetModuleFileName, INT 21h 30h and 4Ch). The sources are here;
the programs, small, are kept as they came out of the compiler.

    mkne_watcom.py NAME OUTPUT.EXE

OWHELLO: the command line in a message box, exit code 3: "OWHELLO hello world"

OWBUSY: a computation of some million instructions without a call (WINN
    switches tasks meanwhile), its result in a message box: 94FB0EA1

OWWIN: a window of its own class: painting, clicks counted, closing; the
    exit code is the number of clicks

OWMENU: resources: the title a string, the class's menu, an accelerator
    (Ctrl+C counts), a dialog (About) with its dialog procedure; medium
    model (-mm), wrc owmenu.rc

OWSEGS: code in three segments (medium model, two modules, linked with
    wlink option packcode=0), far calls between them: C8CA765F

OWDEMO: many API functions at once (controls with strings, GDI, timers,
    memory, a file, a subclassed edit control, CREATESTRUCT, MINMAXINFO)

OWFILE: DOS through the C library (fopen, fprintf, fgets, fseek, rename,
    _dos_findfirst, open, filelength, isatty, remove, access, getcwd, time),
    KERNEL's OpenFile and DOS3Call, KEYBOARD's AnsiToOem and GetKBCodePage;
    its start-up asks for GetDOSEnvironment and grows its local heap past
    DGROUP's stack: "line 2|18|1|C:\\|1"
OWFILEL: the same, large model (far data: global memory)

OWRES: resources of its own: its icons (the class's, another drawn), its
    cursor, a colour and a monochrome bitmap; ExtTextOut, GetTextExtentPoint,
    TabbedTextOut, GetTextFace, CreateBitmap and GetBitmapBits, a BUTTON
    superclassed through GetClassInfo, _hwrite/_hread: "own 1, bits
    f0000f00, huge io", "extent 48x16, face System"
"""
import sys

PROGRAMS = {
    'OWHELLO': (
        # #include <windows.h>
        #
        # /* the command line in a message box, then exit code 3 */
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     MessageBox(NULL, lpCmdLine, "Open Watcom", MB_OK);
        #     return 3;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '6c0002000000000002030200000400206600010000000200020002000f004000500050005b005f00ee000000'
            '000001000000020000000000000000037f006003400d600350021001410c1001074f5748454c4c4f00000001'
            '000600000455534552064b45524e454c00000b6f7768656c6c6f2e65786500000000ccebfd90909090005589'
            'e5b81000e8220031c050ff7608ff76061eb810005031c0509affff0000b803005dc20a008c162800c33bc473'
            '132bc4f7d83b060a0077068cd03b0628007401c30e5ab85500bb0100e9b201537461636b204f766572666c6f'
            '77210000eb0203009affff00000bc0750e5033c0ba0f00e86a0258b44ccd218c062e00575606535253be1001'
            '893649008c1e4b00268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07b910'
            '01bf10012bcf32c0f3aab8b001a34500b8ffffa34700b430cd21a23000882631009affff0000a23200882633'
            '0086e0a3340054583bc475120f01e0a90100740ab001a23600b003a237001ebf510057b89000509affff0000'
            'b85100893e4d008c1e4f00b8ff00e86b01e8e2fee9f9004f70656e20576174636f6d20432f432b2b31362052'
            '756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20436f'
            '6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f7079726967687420284329'
            '205379626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df8d9e'
            '7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026c607'
            '001e8d9e7aff531eb81c005031c0509affff000089f8e94400515689c189d6e8330185c0750689c889f2eb98'
            '89d8e92c0000c3c3535289c3833e0801007404ff160801ff16e200baff00b81000e8a800ff16e200ff16e400'
            '89d8e928fe5289c2ff16e200ff16e40089d0e918fe5589e5535289c3833f0075078d66fc5a5b5dc31eff171f'
            'ebf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee813ffbf10'
            '0189fabb0a0189fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d440280'
            '3c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee8bcfe88d5bf'
            '100189fabb100189fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c01'
            '7305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803ee60000750331c0c352'
            '50cceb0657564944454fb8010083c404c3000800030123000100010003016b0002005b000301aa0002001e00'
            '0301b100010005000200cb00010066000301dc000200030003010e01020031000301fe010100010000000000'
            '0500000000000000000000004f70656e20576174636f6d004f70656e20576174636f6d000000ffff00000000'
            '000000000000000c000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000210221020000000000000000000000000000000000000000000000000000000000000000000000000020'
            '2e000000'
        )),
    'OWBUSY': (
        # #include <windows.h>
        #
        # static char text[9];
        #
        # /* a long computation without a call: WINN switches tasks in between, and
        #  * the result is the same as without */
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     unsigned long x = 1;
        #     unsigned i;
        #     char *p = text + 8;
        #     for (i = 0; i < 60000u; i++)
        #         x = x * 1103515245UL + 12345UL;
        #     while (p > text) {
        #         *--p = "0123456789ABCDEF"[(unsigned)x & 15];
        #         x >>= 4;
        #     }
        #     MessageBox(NULL, text, "Busy", MB_OK);
        #     return 0;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '6b0002000000000002030200000400209a00010000000200020002000e004000500050005a005e00ed000000'
            '000001000000020000000000000000037e000a03400d0a0320020401410c1001064f57425553590000000100'
            '0600000455534552064b45524e454c00000a6f77627573792e65786500000000ccebfd90909090005589e556'
            '5783ec04be010031ffc746fa0c01897ef8bb6d4eb9c64189f089fae8580089c689d781c6393083d700ff46f8'
            '817ef860ea72de817efa0401761c89f383e30fff4efa8a8710008b5efab90400d1efd1dee2fa8807ebdd31c0'
            '501eb80401501eb821005031c0509affff000031c08d66fc5f5e5dc20a009350920bc07402f7e2910bc07404'
            'f7e303c858f7e303d1c3eb0203009affff00000bc0750e5033c0ba0f00e8fc0158b44ccd218c062a00575606'
            '535253be0d01893645008c1e4700268a078804464384c075f589360a005b2bc0509affff000057579affff00'
            '00fc1e07b90d01bf04012bcf32c0f3aab8e401a34100b8ffffa34300b430cd21a22c0088262d009affff0000'
            'a22e0088262f0086e0a3300054583bc475120f01e0a90100740ab001a23200b003a233001ebf4d0057b89000'
            '509affff0000b84d00893e49008c1e4b00b8ff00e8fd00e8aefee98b004f70656e20576174636f6d20432f43'
            '2b2b31362052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174'
            '636f6d20436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f7079726967'
            '687420284329205379626173652c20496e632e20313938382d323030322e00c3cb00c3c3535289c3833e0201'
            '007404ff160201ff16de00baff00b81000e8a800ff16de00ff16e00089d8e996fe5289c2ff16de00ff16e000'
            '89d0e986fe5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c0'
            '750485d274df1eff1f1febd95589e5535152565789c11ee881ffbf040189fabb040189fe88c839fb7316803f'
            '02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffeb'
            'f61f8d66f65f5e5a595b5dc35589e55351565789c11ee82aff88d5bf040189fabb040189fe88c839fb731680'
            '3f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e8'
            '27ffebeee83bffebe91f8d66f85f5eeb9f000700030173000100010003019f0002005b000301de0002001e00'
            '0301e500010005000200ff0001009a0003011001020003000301420102003100000000000500000000000000'
            '0000000030313233343536373839414243444546004275737900ffff00000000000000000000000c00000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000000000000000000000000000000000000000000000e701e701000000000000'
            '00000000000000000000000000000000000000000000000000000000'
        )),
    'OWWIN': (
        # #include <windows.h>
        #
        # /* a window: its class, painting, a click, closing */
        # static int clicks;
        #
        # LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        # {
        #     PAINTSTRUCT ps;
        #     HDC dc;
        #     char text[32];
        #     switch (msg) {
        #     case WM_PAINT:
        #         dc = BeginPaint(hwnd, &ps);
        #         TextOut(dc, 10, 10, text, wsprintf(text, "Clicks: %d", clicks));
        #         EndPaint(hwnd, &ps);
        #         return 0;
        #     case WM_LBUTTONDOWN:
        #         clicks++;
        #         InvalidateRect(hwnd, NULL, TRUE);
        #         return 0;
        #     case WM_DESTROY:
        #         PostQuitMessage(clicks);
        #         return 0;
        #     }
        #     return DefWindowProc(hwnd, msg, wParam, lParam);
        # }
        #
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     WNDCLASS wc;
        #     MSG msg;
        #     HWND hwnd;
        #     if (!hPrevInstance) {
        #         wc.style = CS_HREDRAW | CS_VREDRAW;
        #         wc.lpfnWndProc = WndProc;
        #         wc.cbClsExtra = 0;
        #         wc.cbWndExtra = 0;
        #         wc.hInstance = hInstance;
        #         wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszMenuName = NULL;
        #         wc.lpszClassName = "OwWin";
        #         if (!RegisterClass(&wc))
        #             return 1;
        #     }
        #     hwnd = CreateWindow("OwWin", "Open Watcom window", WS_OVERLAPPEDWINDOW,
        #                         CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, NULL, NULL, hInstance, NULL);
        #     ShowWindow(hwnd, nCmdShow);
        #     UpdateWindow(hwnd);
        #     while (GetMessage(&msg, NULL, 0, 0)) {
        #         TranslateMessage(&msg);
        #         DispatchMessage(&msg);
        #     }
        #     return msg.wParam;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '7a0007000000000002030200000400208e01010000000200020003000d004000500050006300690001010000'
            '000001000000020000000000000000038700fe03400dfe03eb021201410c2001054f5757494e000007574e44'
            '50524f43010000010006000a0000045553455203474449064b45524e454c01012908000000096f7777696e2e'
            '657865000000ccebfd90909090001e5890455589e51e8ed85683ec408b460c8b760e3d0102745d3d0f00741c'
            '3d02007567ff3612019affff000031c031d28d66fc5e1f5d4dca0a00561e8d56bc529affff000050b80a0050'
            '501e8d56dc52ff3612011eb81000501e529affff000083c40a509affff0000561e8d56bc529affff0000ebb6'
            '5631c05050b8010050010612019affff0000eba25650ff760aff7608ff76069affff0000eb945589e55683ec'
            '2c8b760c837e0a0074661eb81b00501eb8210050b8cf005031c050b800805050b82c0150b8c8005031c05050'
            '5650509affff000050ff760489c69affff0000569affff00001e8d56ec5231c05050509affff000085c07416'
            '1e8d56ec529affff00001e8d56ec529affff0000ebd7eb6fc746d2030031c0c746d4ffff8946d8508946da50'
            'b8007fc746d6ffff508976dc9affff00008946de31c05050b8007f509affff00008946e031c0509affff0000'
            'c746e81b008d56d28946e28c5eea1e31c0528946e48946e69affff000085c07403e936ffb801008d66fe5e5d'
            'c20a008b46f0ebf3eb0203009affff00000bc0750e5033c0ba0f00e8fc0158b44ccd218c0638005756065352'
            '53be1401893653008c1e5500268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc'
            '1e07b91401bf12012bcf32c0f3aab8d802a34f00b8ffffa35100b430cd21a23a0088263b009affff0000a23c'
            '0088263d0086e0a33e0054583bc475120f01e0a90100740ab001a24000b003a241001ebf5b0057b89000509a'
            'ffff0000b85b00893e57008c1e5900b8ff00e8fd00e856fee98b004f70656e20576174636f6d20432f432b2b'
            '31362052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f'
            '6d20436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f70797269676874'
            '20284329205379626173652c20496e632e20313938382d323030322e00c3cb00c3c3535289c3833e10010074'
            '04ff161001ff16ec00baff00b81000e8a800ff16ec00ff16ee0089d8e996fe5289c2ff16ec00ff16ee0089d0'
            'e986fe5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c07504'
            '85d274df1eff1f1febd95589e5535152565789c11ee881ffbf120189fabb120189fe88c839fb7316803f0275'
            '0583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f'
            '8d66f65f5e5a595b5dc35589e55351565789c11ee82aff88d5bf120189fabb120189fe88c839fb7316803f02'
            '750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ff'
            'ebeee83bffebe91f8d66f85f5eeb9f00190003013000010006000301490001002700030164000100a4010301'
            '6d0002002100030178000100280003018c0001007d0003019e0001006b000301da00010029000301e5000100'
            '2a000301eb0001007c000301fa0001006c000301080101007100030112010100720005002401010008000200'
            '34010100080003013b010100ae0003014b010100ad0003015601020057000301730101003900030193010300'
            '5b000301d20103001e000301d901010005000200f30101008e01030104020300030003013602030031000000'
            '0000050000000000000000000000436c69636b733a202564004f7757696e004f70656e20576174636f6d2077'
            '696e646f7700ffff00000000000000000000000c000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000db02db020000000000000000000000000000000000000000000000000000'
            '0000000000000000'
        )),
    'OWMENU': (
        # ---- owmenu.h
        # #define IDM_MENU    100
        # #define IDM_COUNT   101
        # #define IDM_ABOUT   102
        # #define IDM_EXIT    103
        # #define IDD_ABOUT   200
        # #define IDC_COUNT   201
        # #define IDS_TITLE   1
        # ---- owmenu.rc
        # #include <windows.h>
        # #include "owmenu.h"
        #
        # IDM_MENU MENU
        # BEGIN
        #     POPUP "&File"
        #     BEGIN
        #         MENUITEM "&Count\tCtrl+C", IDM_COUNT
        #         MENUITEM "&About...", IDM_ABOUT
        #         MENUITEM SEPARATOR
        #         MENUITEM "E&xit", IDM_EXIT
        #     END
        # END
        #
        # IDM_MENU ACCELERATORS
        # BEGIN
        #     "^C", IDM_COUNT
        # END
        #
        # STRINGTABLE
        # BEGIN
        #     IDS_TITLE "Watcom menus"
        # END
        #
        # IDD_ABOUT DIALOG 20, 20, 140, 64
        # STYLE DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU
        # CAPTION "About"
        # BEGIN
        #     LTEXT "Counted:", -1, 10, 10, 40, 10
        #     EDITTEXT IDC_COUNT, 54, 8, 40, 12
        #     DEFPUSHBUTTON "OK", IDOK, 50, 40, 40, 14
        # END
        # ---- owmenu.c
        # #include <windows.h>
        # #include "owmenu.h"
        #
        # /* resources: a menu with an accelerator, a string, a dialog */
        # static HINSTANCE inst;
        # static int count;
        #
        # BOOL FAR PASCAL __export AboutProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
        # {
        #     switch (msg) {
        #     case WM_INITDIALOG:
        #         SetDlgItemInt(dlg, IDC_COUNT, count, FALSE);
        #         return TRUE;
        #     case WM_COMMAND:
        #         if (wParam == IDOK || wParam == IDCANCEL) {
        #             EndDialog(dlg, wParam);
        #             return TRUE;
        #         }
        #         break;
        #     }
        #     return FALSE;
        # }
        #
        # LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        # {
        #     PAINTSTRUCT ps;
        #     HDC dc;
        #     char text[32];
        #     FARPROC proc;
        #     switch (msg) {
        #     case WM_COMMAND:
        #         switch (wParam) {
        #         case IDM_COUNT:
        #             count++;
        #             InvalidateRect(hwnd, NULL, TRUE);
        #             return 0;
        #         case IDM_ABOUT:
        #             proc = MakeProcInstance((FARPROC)AboutProc, inst);
        #             DialogBox(inst, MAKEINTRESOURCE(IDD_ABOUT), hwnd, (DLGPROC)proc);
        #             FreeProcInstance(proc);
        #             return 0;
        #         case IDM_EXIT:
        #             DestroyWindow(hwnd);
        #             return 0;
        #         }
        #         break;
        #     case WM_PAINT:
        #         dc = BeginPaint(hwnd, &ps);
        #         TextOut(dc, 10, 10, text, wsprintf(text, "Count: %d", count));
        #         EndPaint(hwnd, &ps);
        #         return 0;
        #     case WM_DESTROY:
        #         PostQuitMessage(count);
        #         return 0;
        #     }
        #     return DefWindowProc(hwnd, msg, wParam, lParam);
        # }
        #
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     WNDCLASS wc;
        #     MSG msg;
        #     HWND hwnd;
        #     HACCEL acc;
        #     char title[40];
        #     inst = hInstance;
        #     LoadString(hInstance, IDS_TITLE, title, sizeof(title));
        #     if (!hPrevInstance) {
        #         wc.style = CS_HREDRAW | CS_VREDRAW;
        #         wc.lpfnWndProc = WndProc;
        #         wc.cbClsExtra = 0;
        #         wc.cbWndExtra = 0;
        #         wc.hInstance = hInstance;
        #         wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszMenuName = MAKEINTRESOURCE(IDM_MENU);
        #         wc.lpszClassName = "OwMenu";
        #         if (!RegisterClass(&wc))
        #             return 1;
        #     }
        #     hwnd = CreateWindow("OwMenu", title, WS_OVERLAPPEDWINDOW,
        #                         CW_USEDEFAULT, CW_USEDEFAULT, 300, 200, NULL, NULL, hInstance, NULL);
        #     ShowWindow(hwnd, nCmdShow);
        #     UpdateWindow(hwnd);
        #     acc = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDM_MENU));
        #     while (GetMessage(&msg, NULL, 0, 0)) {
        #         if (TranslateAccelerator(hwnd, acc, &msg))
        #             continue;
        #         TranslateMessage(&msg);
        #         DispatchMessage(&msg);
        #     }
        #     return msg.wParam;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            'db000a000000000002030200000400206e02010000000200020003000e0040005000a400c400ca0065010000'
            '0000010004000208ba00940300000a03ba00e004400de004c3030001410c1001010004800100000000004e04'
            '1e00301064800000000009800100000000004b040300300064800000000005800100000000006c0429003010'
            'c88000000000068001000000000095040e0030100180000000000000064f574d454e5500000941424f555450'
            '524f43010007574e4450524f43020000010006000d00000455534552064b45524e454c034744490201290000'
            '294c0000000a6f776d656e752e657865000000001e5890455589e51e8ed88b460a8b5e0c8b560e81fb110175'
            '0e3d010074253d0200742031c0eb6c81fb100175f652b8c90050ff36020131c0509affff0000b80100eb5052'
            '509affff0000ebf21e5890455589e51e8ed8565783ec428b560a8b460c8b760e3d1101753083fa67744083fa'
            '66743d83fa65756c5631c05050b8010050010602019affff000031c031d28d66fa5f5e1f5d4dca0a003d0f00'
            '74503d02007541ff3602019affff0000ebdceb36b8ffff50b8ffff50ff3600019affff0000ff36000189c731'
            'c050b8c800505652578956f89affff0000ff76f8579affff0000eba6eb45569affff0000eb9c561e8d56d852'
            '9affff000050b80a0050501e8d56b852ff3602011eb81000501e529affff000083c40a509affff0000561e8d'
            '56d8529affff0000e95fff565052ff7608ff76069affff0000e952ff1e5890455589e51e8ed8565783ec548b'
            '460ea3000150b80100501e8d56a652b82800509affff0000837e0c007503e98f001eb81a00501e8d56a652b8'
            'cf005031c050b800805050b82c0150b8c8005031c05050ff760e50509affff00005089c6ff760689c79affff'
            '0000569affff0000ff760e31c050b86400509affff000089c61e8d56e85231c05050509affff000085c07426'
            '57561e8d56e8529affff000085c075dd1e8d56e8529affff00001e8d56e8529affff0000ebc78b46ece992fe'
            '31c08946d48946d68b460e8946d831c050c746ce030050b8007fc746d0ffff50c746d2ffff9affff00008946'
            'da31c05050b8007f509affff00008946dc31c0509affff0000c746e06400c746e20000c746e41a008d56ce1e'
            '8946de528c5ee69affff000085c07403e906ffb80100e921fe00eb009affff00000bc0750e5033c0ba0f00e8'
            '000258b44ccd218c062600575606535253be0401893641008c1e4300268a078804464384c075f589360a005b'
            '2bc0509affff000057579affff0000fc1e07b90401bf00012bcf32c0f3aab8ba03a33d00b8ffffa33f00b430'
            'cd21a22800882629009affff0000a22a0088262b0086e0a32c0054583bc475120f01e0a90100740ab001a22e'
            '00b003a22f001ebf490057b89000509affff0000b84900893e45008c1e4700b8ff00e801010e3ee80efee98d'
            '0089c04f70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374656d2e20436f7079'
            '726967687420286329204f70656e20576174636f6d20436f6e7472696275746f727320323030322d32303236'
            '2e20506f7274696f6e7320436f7079726967687420284329205379626173652c20496e632e20313938382d32'
            '3030322e00c3cb00c3c3535289c3833efe00007404ff16fe00ff16da00baff00b81000e8a800ff16da00ff16'
            'dc0089d8e992fe5289c2ff16da00ff16dc0089d0e982fe5589e5535289c3833f0075078d66fc5a5b5dc31eff'
            '171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee881ff'
            'bf000189fabb000189fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d44'
            '02803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee82aff88'
            'd5bf000189fabb000189fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a'
            '6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f00240003013a000100'
            '5e0003014600010058000301860001007d000301a800010006000200b100010000000500b500010000000301'
            'bd00020033000301d500010057000301de00020034000301e800010035000301f50001002700030110010100'
            'a401030119010300210003012401010028000301350101006b00030160010100b00003019501010029000301'
            'a20101002a000301a80101007c000301b7010100b1000301c80101006c000301d8010100b2000301e6010100'
            '71000301f001010072000500190201004c0002001f0201004c00030122020100ae00030132020100ad000301'
            '3d020300570003015c02010039000301710202005b000301b00202001e000301b702010005000200d1020100'
            '6e020301e2020200030003011403020031000000000000000000000000000000000000000000050000000000'
            '000000000000436f756e743a202564004f774d656e750000ffff00000000000000000000000c000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000bd03bd030000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000008003'
            '006500000000000090002646696c65000000650026436f756e74094374726c2b4300000066002641626f7574'
            '2e2e2e00000800000080006700452678697400008000c88003140014008c004000000041626f7574000a000a'
            '0028000a00ffff0000025082436f756e7465643a00003600080028000c00c900000081508100003200280028'
            '000e00010001000150804f4b0000000c576174636f6d206d656e75730000000000000000000000000000'
        )),
    'OWSEGS': (
        # ---- owsegs.c
        # #include <windows.h>
        #
        # /* code in two segments (medium model, two modules): far calls between them */
        # extern unsigned long far compute(unsigned n);
        #
        # static char text[9];
        #
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     unsigned long x = compute(1000);
        #     char *p = text + 8;
        #     while (p > text) {
        #         *--p = "0123456789ABCDEF"[(unsigned)x & 15];
        #         x >>= 4;
        #     }
        #     MessageBox(NULL, text, "Segments", MB_OK);
        #     return 0;
        # }
        # ---- owsegs2.c
        # /* the second code segment */
        # unsigned long far compute(unsigned n)
        # {
        #     unsigned long x = 7;
        #     while (n--)
        #         x = x * 69069UL + 1UL;
        #     return x;
        # }
        # ---- owsegs.lnk
        # system windows
        # option packcode=0
        # name owsegs.exe
        # file owsegs.o, owsegs2.o
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '7b0002000000000002030400000400200000030000000400040002000e004000600060006a006e00fd000000'
            '0000010000000200000000000000000386005200400d5200b8002800400d2800d1008a02400d8a0233020801'
            '410c2001064f575345475300000001000600000455534552064b45524e454c00000a6f77736567732e657865'
            '000000001e5890455589e51e8ed856b8e803bb10019affff000081fb0801761789c683e60f4b8a8c1000880f'
            'b90400d1ead1d8e2faebe331c0501eb80801501eb821005031c0509affff000031c05e1f5d4dca0a00000200'
            '0300120002000000030144000100010053515689c6b8070031d24e83feff75045e595bcbbbcd0db901009aff'
            'ff000005010083d200ebe300010003001b0003004e01eb009affff00000bc0750e5033c0ba0f00e8180258b4'
            '4ccd218c062e00575606535253be1101893649008c1e4b00268a078804464384c075f589360a005b2bc0509a'
            'ffff000057579affff0000fc1e07b91101bf08012bcf32c0f3aab84c01a34500b8ffffa34700b430cd21a230'
            '00882631009affff0000a232008826330086e0a3340054583bc475120f01e0a90100740ab001a23600b003a2'
            '37001ebf510057b89000509affff0000b85100893e4d008c1e4f00b8ff00e819019affff0000e9a50089c04f'
            '70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374656d2e20436f707972696768'
            '7420286329204f70656e20576174636f6d20436f6e7472696275746f727320323030322d323032362e20506f'
            '7274696f6e7320436f7079726967687420284329205379626173652c20496e632e20313938382d323030322e'
            '00c3cb009350920bc07402f7e2910bc07404f7e303c858f7e303d1cbc3c3535289c3833e0601007404ff1606'
            '01ff16e200baff00b81000e8a800ff16e200ff16e40089d8e97afe5289c2ff16e200ff16e40089d0e96afe55'
            '89e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df'
            '1eff1f1febd95589e5535152565789c11ee869ffbf080189fabb080189fe88c839fb7316803f02750583c306'
            'ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f'
            '5e5a595b5dc35589e55351565789c11ee812ff88d5bf080189fabb080189fe88c839fb7316803f02750583c3'
            '06ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83b'
            'ffebe91f8d66f85f5eeb9f0007000301030002005b000301420002001e000301490001000500020063000300'
            '000003017400020003000301a600020031000300bc0001000000000000000500000000000000000000003031'
            '3233343536373839414243444546005365676d656e747300ffff00000000000000000000000c000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000670167010000000000000000'
            '0000000000000000000000000000000000000000000000000000'
        )),
    'OWDEMO': (
        # #include <windows.h>
        # #include <string.h>
        #
        # /* many API functions at once: controls with strings, GDI drawing, timers
        #  * (a WM_TIMER one, a TIMERPROC one), global and local memory, a file,
        #  * text metrics, enumeration, a subclassed edit control, WM_CREATE's
        #  * CREATESTRUCT, WM_GETMINMAXINFO; what each gave in the window */
        # static HINSTANCE inst;
        # static HWND edit, list;
        # static FARPROC edit_old;
        # static int ticks, ticks2, windows, chars, created;
        # static char title[40], file_text[16], global_text[16], local_text[16];
        # static TEXTMETRIC tm;
        #
        # LRESULT FAR PASCAL __export EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        # {
        #     if (msg == WM_CHAR)
        #         chars++;
        #     return CallWindowProc(edit_old, h, msg, wp, lp);
        # }
        #
        # void FAR PASCAL __export TimerProc(HWND h, UINT msg, UINT id, DWORD time)
        # {
        #     if (++ticks2 >= 3)
        #         KillTimer(h, id);
        # }
        #
        # BOOL FAR PASCAL __export EnumProc(HWND h, LPARAM lp)
        # {
        #     windows++;
        #     return TRUE;
        # }
        #
        # static void memory_and_file(void)
        # {
        #     HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 1000);
        #     char FAR *p = GlobalLock(g);
        #     HLOCAL l;
        #     HFILE f;
        #     lstrcpy(p, "global ok");
        #     lstrcpy(global_text, p);
        #     GlobalUnlock(g);
        #     GlobalFree(g);
        #     l = LocalAlloc(LMEM_FIXED, 32);
        #     lstrcpy((char NEAR *)l, "local ok");
        #     lstrcpy(local_text, (char NEAR *)l);
        #     LocalFree(l);
        #     f = _lcreat("OWDEMO.TMP", 0);
        #     _lwrite(f, "file ok", 8);
        #     _lclose(f);
        #     f = _lopen("OWDEMO.TMP", OF_READ);
        #     _lread(f, file_text, 8);
        #     _lclose(f);
        # }
        #
        # LRESULT FAR PASCAL __export WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        # {
        #     PAINTSTRUCT ps;
        #     HDC dc;
        #     char text[80], item[32];
        #     HPEN pen, oldpen;
        #     HBRUSH brush, oldbrush;
        #     POINT tri[3];
        #     RECT rc;
        #     int y = 160;
        #     switch (msg) {
        #     case WM_CREATE:
        #         lstrcpy(title, ((LPCREATESTRUCT)lParam)->lpszName);
        #         created = 1;
        #         edit = CreateWindow("EDIT", "edit", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 10, 120, 22,
        #                             hwnd, (HMENU)10, inst, NULL);
        #         list = CreateWindow("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 40, 120, 60,
        #                             hwnd, (HMENU)11, inst, NULL);
        #         CreateWindow("BUTTON", "Go", WS_CHILD | WS_VISIBLE, 10, 106, 50, 24, hwnd, (HMENU)12,
        #                      inst, NULL);
        #         SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"one");
        #         SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"two");
        #         SendMessage(list, LB_SETCURSEL, 1, 0);
        #         edit_old = (FARPROC)GetWindowLong(edit, GWL_WNDPROC);
        #         SetWindowLong(edit, GWL_WNDPROC, (LONG)MakeProcInstance((FARPROC)EditProc, inst));
        #         SetTimer(hwnd, 1, 50, NULL);
        #         SetTimer(hwnd, 2, 50, (TIMERPROC)MakeProcInstance((FARPROC)TimerProc, inst));
        #         EnumWindows((WNDENUMPROC)MakeProcInstance((FARPROC)EnumProc, inst), 0);
        #         memory_and_file();
        #         dc = GetDC(hwnd);
        #         GetTextMetrics(dc, &tm);
        #         ReleaseDC(hwnd, dc);
        #         return 0;
        #     case WM_GETMINMAXINFO:
        #         ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.x = 200;
        #         ((MINMAXINFO FAR *)lParam)->ptMinTrackSize.y = 150;
        #         return 0;
        #     case WM_TIMER:
        #         if (wParam == 1 && ++ticks >= 3)
        #             KillTimer(hwnd, 1);
        #         InvalidateRect(hwnd, NULL, TRUE);
        #         return 0;
        #     case WM_COMMAND:
        #         if (wParam == 12) {
        #             SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"three");
        #             SetWindowText(edit, "went");
        #             InvalidateRect(hwnd, NULL, TRUE);
        #         }
        #         return 0;
        #     case WM_PAINT:
        #         dc = BeginPaint(hwnd, &ps);
        #         pen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
        #         brush = CreateSolidBrush(RGB(255, 255, 0));
        #         oldpen = SelectObject(dc, pen);
        #         oldbrush = SelectObject(dc, brush);
        #         Rectangle(dc, 150, 10, 220, 60);
        #         Ellipse(dc, 230, 10, 300, 60);
        #         tri[0].x = 310; tri[0].y = 60; tri[1].x = 340; tri[1].y = 10; tri[2].x = 370; tri[2].y = 60;
        #         Polygon(dc, tri, 3);
        #         MoveTo(dc, 150, 70);
        #         LineTo(dc, 370, 70);
        #         SelectObject(dc, oldpen);
        #         SelectObject(dc, oldbrush);
        #         DeleteObject(pen);
        #         DeleteObject(brush);
        #         SetBkMode(dc, TRANSPARENT);
        #         SetTextColor(dc, RGB(0, 0, 128));
        #         SendMessage(list, LB_GETTEXT, (WPARAM)SendMessage(list, LB_GETCURSEL, 0, 0),
        #                     (LPARAM)(LPSTR)item);
        #         TextOut(dc, 150, 80, text, wsprintf(text, "list %d, '%s'",
        #                 (int)SendMessage(list, LB_GETCOUNT, 0, 0), (LPSTR)item));
        #         GetWindowText(edit, item, sizeof(item));
        #         TextOut(dc, 150, 96, text, wsprintf(text, "edit '%s', %d chars", (LPSTR)item, chars));
        #         TextOut(dc, 150, 112, text, wsprintf(text, "timers %d %d", ticks, ticks2));
        #         TextOut(dc, 150, 128, text, wsprintf(text, "%s, %s, %s", (LPSTR)global_text,
        #                 (LPSTR)local_text, (LPSTR)file_text));
        #         GetClientRect(hwnd, &rc);
        #         TextOut(dc, 10, y, text, wsprintf(text, "font %d high, %d windows, client %dx%d",
        #                 tm.tmHeight, windows > 0, rc.right, rc.bottom));
        #         TextOut(dc, 10, y + 16, text, wsprintf(text, "created '%s'", (LPSTR)title));
        #         EndPaint(hwnd, &ps);
        #         return 0;
        #     case WM_DESTROY:
        #         PostQuitMessage(0);
        #         return 0;
        #     }
        #     return DefWindowProc(hwnd, msg, wParam, lParam);
        # }
        #
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     WNDCLASS wc;
        #     MSG msg;
        #     HWND hwnd;
        #     inst = hInstance;
        #     memset(&wc, 0, sizeof(wc));
        #     wc.lpfnWndProc = WndProc;
        #     wc.hInstance = hInstance;
        #     wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #     wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #     wc.lpszClassName = "OwDemo";
        #     RegisterClass(&wc);
        #     hwnd = CreateWindow("OwDemo", "Watcom API tour", WS_OVERLAPPEDWINDOW, 20, 20, 400, 240,
        #                         NULL, NULL, hInstance, NULL);
        #     ShowWindow(hwnd, nCmdShow);
        #     UpdateWindow(hwnd);
        #     while (GetMessage(&msg, NULL, 0, 0)) {
        #         TranslateMessage(&msg);
        #         DispatchMessage(&msg);
        #     }
        #     return msg.wParam;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '9d0010000000000002030200000400202607010000000200020003000e0040005000500086008c002d010000'
            '000001000000020000000000000000039e009609400d9609fa06ca01410c6002064f5744454d4f0000084544'
            '495450524f4301000954494d455250524f43020008454e554d50524f43030007574e4450524f430400000100'
            '06000d00000455534552064b45524e454c034744490401290800293e0019600029430100000a6f7764656d6f'
            '2e65786500000000ccebfd90909090001e5890455589e51e8ed88b460c3d02017504ff062802ff362402ff36'
            '2202ff760e50ff760aff7608ff76069affff00001f5d4dca0a001e5890455589e51e8ed8ff063002833e3002'
            '037ce5ff760eff760a9affff0000ebd81e5890455589e51e8ed8b80100ff062e021f5d4dca06005589e55351'
            '52565783ec02b802005031c050b8e803509affff00005089c69affff000052508946f41eb810005089d79aff'
            'ff00001eb8ca015057ff76f49affff0000569affff0000569affff000031c050b82000509affff00001e5089'
            'c61eb81a00509affff00001eb81202501e569affff0000569affff00001eb823005031c0509affff00005089'
            'c61eb82e0050bf0800579affff0000569affff00001ebe23005631f6569affff0000501ebf020257bf080057'
            '89c69affff0000569affff00008d66f65f5e5a595b5dc31e5890455589e51e8ed8565781ecb0008b76068b56'
            '088b5e0a8b460c8b7e0ec746f6a0003d0f007203e9ab013d02007503e9c3043d01007403e9c804b8da011e8e'
            'c25026ff741826ff74169affff00001eb83b00501eb8360050b880505031c050b80a005050b8780050b81600'
            '5057b80a0050ff36260231c05050c706320201009affff0000a32a021eb84100501eb8400050b880505031c0'
            '50b80a0050b8280050b8780050b83c005057b80b0050ff36260231c050509affff0000a334021eb84c00501e'
            'b8490050b800505031c050b80a0050b86a0050b8320050b818005057b80c0050ff36260231c050509affff00'
            '00ff363402b801045031c0501eb85300509affff0000ff363402b801045031c0501eb85700509affff0000ff'
            '363402b8070450b801005031c050509affff0000ff362a02b8fcff509affff0000ff362a02a32202b8fcff50'
            'b8ffff50b8ffff50ff362602891624029affff000052509affff000057b8010050b832005031c050509affff'
            '000057b8020050b8320050b8ffff50b8ffff50ff3626029affff000052509affff0000b8ffff50b8ffff50ff'
            '3626029affff0000525031c050509affff0000e87dfd579affff00005089c61eb83602509affff000057569a'
            'ffff000031c031f6eb3076383d1301753583fb017512011e2c02833e2c02037c0757539affff00005731c050'
            '50b801005031f69affff000031c089f28d66fa5f5ee9e4fceb583d1101753883fb0c75b4ff363402b8010450'
            '31c0501eb85b00509affff0000ff362a021eb86100509affff00005731c05050b80100509affff0000eb813d'
            '240075138ec226c7440cc80031c026c7440e9600e96bffe99902571e8d569a529affff000089c68946f831c0'
            '50b802005031c050b8ff00509affff00008946f031c050b8ffff509affff000056ff76f08946ee9affff0000'
            '56ff76ee8946f29affff00008946f456b8960050b80a0050b8dc0050b83c00509affff000056b8e60050b80a'
            '0050b82c0150b83c00509affff0000c746da3601c746dc3c00c746de5401c746e00a00568d56da1eb8030052'
            'c746e2720150c746e43c009affff000056b8960050b84600509affff000056b8720150b84600509affff0000'
            '56ff76f29affff000056ff76f49affff0000ff76f09affff0000ff76ee9affff000056b80100509affff0000'
            '56b880005031c0509affff0000ff363402b80a0450ff363402b809045031c05050509affff0000501e8d56ba'
            '529affff000056b8960050b85000501e8d964aff521e8d56ba52ff363402b80c045031c05050509affff0000'
            '501eb86600501e8d964aff529affff000083c40e509affff0000ff362a021e8d56ba52b82000509affff0000'
            '56b8960050b86000501e8d964aff52ff3628021e8d56ba521eb87400501e8d964aff529affff000083c40e50'
            '9affff000056b8960050b87000501e8d964aff52ff363002ff362c021eb88800501e529affff000083c40c50'
            '9affff000056b8960050b88000501e8d964aff521eb80202501eb81202501eb8ca01501eb89500501e529aff'
            'ff000083c414509affff0000571e8d56e6529affff000056b80a0050b8a000501e8d964aff52ff76ecff76ea'
            '833e2e02007e5fb8010050ff3636021eb8a000501e8d964aff529affff000083c410509affff0000ff76f8b8'
            '0a00508b46f6051000501e8d964aff521eb8da01501eb8c700501e529affff000083c40c509affff0000571e'
            '8d569a5231f69affff0000e912fd31c0eba031c05031f69affff0000e901fd57505352569affff000089d6e9'
            'f4fc5589e5565783ec2c8b760cb91a008d7ed030c089362602571e078ae0d1e9f3ab13c9f3aa5f30e450c746'
            'd2ffff50b8007fc746d4ffff508976da9affff00008946de31c0509affff0000c746e6d4008d56d01e8946e0'
            '528c5ee89affff00001eb8d400501eb8db0050b8cf005031c050b814005050b8900150b8f0005031c0505056'
            '50509affff000050ff760489c69affff0000569affff00001e8d56ea5231c05050509affff000085c0750c8b'
            '46ee8d66fc5f5e5dc20a001e8d56ea529affff00001e8d56ea529affff0000ebcb00eb0203009affff00000b'
            'c0750e5033c0ba0f00e8fc0158b44ccd218c06f000575606535253be550289360b018c1e0d01268a07880446'
            '4384c075f589360a005b2bc0509affff000057579affff0000fc1e07b95502bfca012bcf32c0f3aab87008a3'
            '0701b8ffffa30901b430cd21a2f2008826f3009affff0000a2f4008826f50086e0a3f60054583bc475120f01'
            'e0a90100740ab001a2f800b003a2f9001ebf130157b89000509affff0000b81301893e0f018c1e1101b8ff00'
            'e8fd00e870fee98b004f70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374656d'
            '2e20436f7079726967687420286329204f70656e20576174636f6d20436f6e7472696275746f727320323030'
            '322d323032362e20506f7274696f6e7320436f7079726967687420284329205379626173652c20496e632e20'
            '313938382d323030322e00c3cb00c3c3535289c3833ec801007404ff16c801ff16a401baff00b81000e8a800'
            'ff16a401ff16a60189d8e996fe5289c2ff16a401ff16a60189d0e986fe5589e5535289c3833f0075078d66fc'
            '5a5b5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e55351525657'
            '89c11ee881ffbfca0189fabbca0189fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef'
            '39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789'
            'c11ee82aff88d5bfca0189fabbca0189fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701eb'
            'ef39fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f006400'
            '0301ca0401006f000301f00401006f00030101050100a40103010a050300210003011c050100240003014405'
            '0100a40103014d0503002100030170050100a40103017905030021000301a3050100a4010301ac0503002100'
            '0301b705010021000301eb050100a4010301f40503002100030119060100a401030122060300210003012f06'
            '01002800030140060100060003014d0601006b00050081060100430102008a0601004301030191060100ad00'
            '03019c06030057000301b106010039000301db06010029000301e60601002a000301ec0601007c000301fb06'
            '01006c00030115070100710003011f070100720003012b0702005b0003016a0702001e000301710701000500'
            '02008b070100260703019c07020003000301ce07020031000301340001007a0003015a0001000c0003018e00'
            '02000f0003019600020012000301a700020058000301b500020058000301bb00020013000301c10002001100'
            '0301cd00020005000301db00020058000301e700020058000301ed00020007000301fa000200530003010b01'
            '02005600030111010200510003011e010200550003012f0102005200030135010200510003018f0102005800'
            '0301c501010029000301fb010100290003013102010029000301460201006f0003015b0201006f0003017002'
            '01006f0003017d020100870002008d0201000800050091020100080003019d02020033000301a40201008800'
            '0301b60201000a000200c40201003e000500c80201003e000301d002020033000301d70201000a000200dc02'
            '010060000500e002010060000301e802020033000301f302010036000301fc02010042000301090303005d00'
            '03011003010044000301340301000c000301440301007d000301710301006f0003017f030100250003018d03'
            '01007d000301b503010027000301cd0303003d000301dc03030042000301e80303002d000301f40303002d00'
            '03010d0403001b000301230403001800030150040300240003015e040300140003016c040300130003017504'
            '03002d0003017e0403002d00030186040300450003018e040300450003019804030002000301a50403000900'
            '0301bf0401006f0000000000050000000000000000000000676c6f62616c206f6b006c6f63616c206f6b004f'
            '5744454d4f2e544d500066696c65206f6b0065646974004544495400004c495354424f5800476f0042555454'
            '4f4e006f6e650074776f0074687265650077656e74006c6973742025642c2027257327006564697420272573'
            '272c2025642063686172730074696d6572732025642025640025732c2025732c20257300666f6e7420256420'
            '686967682c2025642077696e646f77732c20636c69656e742025647825640063726561746564202725732700'
            '4f7744656d6f00576174636f6d2041504920746f75720000ffff00000000000000000000000c000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000730873080000000000000000'
            '0000000000000000000000000000000000000000000000000000'
        )),
    'OWFILE': (
        # wcl -bt=windows -l=windows -ms owfile.c
        # #include <windows.h>
        # #include <stdio.h>
        # #include <string.h>
        # #include <stdlib.h>
        # #include <io.h>
        # #include <fcntl.h>
        # #include <direct.h>
        # #include <dos.h>
        # #include <time.h>
        #
        # /* KERNEL's DOS3Call: INT 21h with the caller's registers */
        # extern void far pascal DOS3Call(void);
        # unsigned char dos3call_drive(void);
        # #pragma aux dos3call_drive = "mov ah,19h" "call far ptr DOS3Call" value [al] modify [ah];
        #
        # /* files through the C library (DOS calls): write, read back, seek, rename,
        #  * find, delete, the current directory, the date; KERNEL's OpenFile,
        #  * DOS3Call, KEYBOARD's AnsiToOem and code page; a summary in a message box */
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     char line[64], out[256], cwd[80];
        #     FILE *f;
        #     long size;
        #     int found = 0, fd;
        #     struct find_t ff;
        #     time_t now;
        #
        #     f = fopen("OWFILE.TMP", "w");
        #     if (!f) { MessageBox(NULL, "fopen w", "OWFILE", MB_OK); return 1; }
        #     fprintf(f, "line one\nline %d\n", 2);
        #     fclose(f);
        #     f = fopen("OWFILE.TMP", "r");
        #     if (!f) { MessageBox(NULL, "fopen r", "OWFILE", MB_OK); return 2; }
        #     fgets(line, sizeof(line), f);
        #     fgets(line, sizeof(line), f);
        #     fseek(f, 0L, SEEK_END);
        #     size = ftell(f);
        #     fclose(f);
        #     line[strcspn(line, "\n")] = 0;
        #     if (rename("OWFILE.TMP", "OWFILE.TXT")) { MessageBox(NULL, "rename", "OWFILE", MB_OK); return 3; }
        #     if (!_dos_findfirst("OWFILE.*", _A_NORMAL, &ff)) {
        #         do { if (!strcmp(ff.name, "OWFILE.TXT") && ff.size == size) found++; } while (!_dos_findnext(&ff));
        #     }
        #     fd = open("OWFILE.TXT", O_RDONLY | O_BINARY);
        #     if (fd < 0 || filelength(fd) != size || isatty(fd)) { MessageBox(NULL, "open", "OWFILE", MB_OK); return 4; }
        #     close(fd);
        #     {
        #         OFSTRUCT of;
        #         char oem[4];
        #         if (OpenFile("owfile.txt", &of, OF_EXIST) == HFILE_ERROR ||
        #             !strstr(of.szPathName, "\\OWFILE.TXT")) {
        #             MessageBox(NULL, "OpenFile", "OWFILE", MB_OK); return 7;
        #         }
        #         AnsiToOem("\xc0\xff", oem);
        #         if ((unsigned char)oem[0] != 0x80 || (unsigned char)oem[1] != 0xef || GetKBCodePage() != 866 ||
        #             dos3call_drive() != 2) {
        #             MessageBox(NULL, "AnsiToOem/DOS3Call", "OWFILE", MB_OK); return 8;
        #         }
        #     }
        #     if (remove("OWFILE.TXT")) { MessageBox(NULL, "remove", "OWFILE", MB_OK); return 5; }
        #     if (!access("OWFILE.TXT", 0)) { MessageBox(NULL, "access", "OWFILE", MB_OK); return 6; }
        #     getcwd(cwd, sizeof(cwd));
        #     now = time(NULL);
        #     sprintf(out, "%s|%ld|%d|%s|%d", line, size, found, cwd, localtime(&now)->tm_year >= 100);
        #     MessageBox(NULL, out, "OWFILE", MB_OK);
        #     return 0;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '760002000000000002030200000400200a0d010000000200020003000e004000500050005a006000f8000000'
            '0000010000000200000000000000000383009241400d9241b9215805410ca006064f5746494c450000000100'
            '06000d00000455534552064b45524e454c084b4559424f41524400000a6f7766696c652e657865000000cceb'
            'fd909090900000000000000000005589e5b86802e89402565781ec500231f6ba1000b81200e8870489c785c0'
            '7517561eb82400501eb81d0050569affff0000b80100e95802b8020050b82c005057e80b0583c40689f8e8b8'
            '05ba3e00b81200e84d0489c785c07517561eb84000501eb81d0050569affff0000b80200e91e0289c3ba4000'
            '8d4684e8a90589fbba40008d4684e89e05ba020031db31c989f8e8490689f8e844078946f88956fa89f8e860'
            '05ba48008d4684e87e0789c7c6438400ba4a00b81200e8b30785c07417561eb85500501eb81d0050569affff'
            '0000b80300e9b9018d5ec431d2b85c00e89f0785c07528ba4a008d46e2e8bb0785c075118b46e03b46fa7509'
            '8b46de3b46f87501468d46c4e88d0785c074d8b8000250b84a0050e8820989c783c40485c07c16e8b0093b56'
            'fa750e3b46f8750989f8e8a40985c0741b31c0501eb86500501eb81d005031c0509affff0000b80400e93d01'
            '89f8e8bb091eb86a00508d96acfe1e52b80040509affff00003dffff740eba75008d86b4fee89b0985c0751b'
            '31c0501eb88100501eb81d005031c0509affff0000b80700e9f6001eb88a00508d56f01e529affff0000807e'
            'f080751b807ef1ef75159affff00003d6203750bb4199affffffff3c02741b31c0501eb88d00501eb81d0050'
            '31c0509affff0000b80800e9ab00b84a00e8ab0985c0741b31c0501eb8a000501eb81d005031c0509affff00'
            '00b80500e9860031d2b84a00e8870985c07518501eb8a700501eb81d005031c0509affff0000b80600eb62ba'
            '50008d8634ffe8940931c0e8020a8946f48956f68d46f4e8930a89c7837d0a647c05b80100eb0231c0508d86'
            '34ff5056ff76faff76f88d468450b8ae00508d86acfd50e8700a83c41031c0508d96acfd1e521eb81d005031'
            'c0509affff000031c08d66fc5f5e5dc20a008c168401c33bc473132bc4f7d83b060a0077068cd03b06840174'
            '01c30e5ab8cf02bb0100e9dc0b537461636b204f766572666c6f7721005351565589e583ec0489c689d331d2'
            '85db740aa13e023d0100753389078a043c77752f80ca02b40130c946884efc884efe30ed8a0438c5744d84e4'
            '74493c6373323c62755084c9744a88ec46ebe58917ebcb3c72750580ca01ebcb3c61750580ca82ebc2c7066c'
            '05090031c089ec5d5e595bc377073a6efe7438ebcd3c74750884c97423ebc3eb3c3c6e75bf3a6efe742aebb6'
            'eb163c2b75b23a6efc75abc646fc0180ca03eba4b101eba0b10180ca40eb99c646fe01800f01eb90c646fe01'
            '8027feeb8784c9750b813e40020002750380ca4089d0eb955589e556578b760689c788d030e4e8000b3c7275'
            '6e31c0f6c3027403b80200f6c340745a80cc0231d252ff76045057e8e10683c4088944083dffff746bc74402'
            '0000c7440a00008b7c04095c06c7450600008b7c04894d088b7c04c745040000f6c380740cba020089f031db'
            '31c9e8c50289f0e8a90a89f05f5e5dc2040080cc01eba4f6c3017417b82200f6c38074140c10f6c340741380'
            'cc02ba8001eb8ab82100ebe70c40ebeaeb0580cc01ebeb89f0e8900a31c0ebc05589e551565783ec0289c789'
            'd689d98d56f889f0e85afe89c385c075088d66fa5f5e595dc3e89f0a89c285c07411508a0451988b4ef889c2'
            '89f8e80bff89c289d0ebda5331dbe8b7ff5bc35351525689c18b1e6e0585db7512be70058b1c85db74333b4f'
            '02741e89deebf18b470239c174048b1febdfba0100e8890089cb89d85e5a595bc38b0789048b366e05891e6e'
            '058937ebe7c7066c050400ebe15589e551565783ec0289c789d68d56f889f0e8bffd89c285c07503e962ff8b'
            '4708833e5c0300752589d8e881ff89c385c074155031c0508a048b4ef89889d389c289f8e865fe89c389d8e9'
            '33ffff165c03ebd55589e553528b46048d5e088b5606e8550a5a5b5dc35589e55351565783ec0489c68956f6'
            'f6440603746931fff644071075668b440285c07416f7d88b5c0899895ef489d189c3ba01008b46f4e8af0a83'
            '7ef60074088b4408e8e20a09c7f644060874118b5c048b4704e81f0b8b5c04c747040000f6440708740689f0'
            'ff165805c64406008064074089f88d66f85f5e595b5dc3bfffffebf0e8b30b85c07493bfffffeb8e5389c3e8'
            '6bff89c289d8e8eb0889d05bc353528b1e6e0585db74123b470274048b1febf3ba0100e8d6ff5a5bc3b8ffff'
            '5a5bc35589e551565783ec0489c789de8b470630e4806706cf243089fb8946f64a85d27f22837ef8ff753639'
            'fb7406f6440620742c31ff8b46f609440689f88d66fa5f5e595dc389f0e8cd0c8946f83dffff74d18a4ef888'
            '0f433d0a0074c6ebbfc60700ebd151565789c189d68b47029939d67c06751739c177138b7f048b45042b0799'
            '39d67f0d750439c17307b801005f5e59c3806706ef010f31c0294f02ebef535689c38b7704806706ef8b7404'
            'c74702000089375e5bc35589e5565783ec0a89c689df8956faf6440606745af6440710753d83fa0175088b44'
            '029929c719d18b5c048b4704c74402000089048b56fa89fb8b4408806406ebe89a0c83faff75363dffff7531'
            'b8ffff8d66fc5f5e5dc3e8650a85c074d685d275eb85c97de7c7066c050900ebdf83fa02744883fa01740885'
            'd27441ebe8eb378b44029989f38946f68956f889f889cae818ff85c074208b56fa89fb8b44082b5ef61b4ef8'
            'e8350c83faff75053dffff749b89f0e830ff31c0eb95eb398b4408e8450c8946f28b440289d3998956f48b56'
            'f229c289d089fa1b5ef429c289d089ca19da89f3e8c3fe85c074cb8b56fa89fb8b4408ebafe8eefe8b4408e9'
            '41ff53515689c3f64706807409f64707107403e8040c8b4708e8ef0b89c189d683faff75053dffff7414837f'
            '0200740ef6470710740c8b47029901c811f25e595bc38b47029929c119d689c889f2ebee5589e55351565783'
            'ec2089c38d46d8b103e8c30b31f68a0784c0741d88c230f689d72407d3ff30e48a53d889c78a85220185c275'
            '044643ebdd89f08d66f85f5e595b5dc35789d789c2061e07b456cd2107e8ca0b5fc35189d189da89c3b41acd'
            '218bd3b44ecd21e8a80b59c35289c2b41acd21b44fcd21e8980b5ac331c0c351565789d789c68cd88ec0a675'
            '114f4e83c9ff33c0f2aef7d12bf9f3a6740519c083d8ff5f5e59c35589e5565783ec0a89c68956f8895ef689'
            'cfc746f2ffff803c20750346ebf88b46f82583008d5ef289c28946f40b56f689f0e84c0b85c0750c8b46f23b'
            '067c037203e91601f646f803743b8b46f23dffff7433e8b00185c0752cf646f9047409f646f8207403e90a01'
            'f646f84074178d56fa8b5ef231c9b440cd2119d289c185d27d03e9fe00837ef2ff754ff646f8207503e9d400'
            'a174053d020075f501c78b45fe85c07503b880018b16a803f7d221d031d2a8807503ba01008d5ef289f0e803'
            '0b85c075cc8b46f23b067c037203e9cc00837ef6007403e9cd008b46f2e8b40a243c89c28b46f2e81b0185c0'
            '740380ce208066f47f837ef402750380ca03837ef400750380ca01837ef401750380ca02f646f810740380ca'
            '8089d00c40f646f9037470f646f902740289c28b46f2e8390b833e5e0300741dbabe0089f0e8730b85c07511'
            'b8ffff50ff76f231c050ff165e0383c4068b46f2eb1189c3b43ecd2119d2c7066c050b00b8ffff8d66fc5f5e'
            '5dc38b5ef2b43ecd2119d2c7066c050700ebe58b5ef2b43ecd2119d289c8e85c0bebd8813e400200027590eb'
            '8c89c3b43ecd2119d2ebbb89c3b43ecd2119d285d27cdb8b56f48d5ef289f00b56f6e8bf0985c075a3e912ff'
            '5589e5538d5e088b075031c050ff7606ff760483c302e8060083c4085b5dc35589e55351528b46048d4e0a8b'
            '5e088b5606e803fe5a595b5dc300e9410b535289c3833e5a0300751db80044cd2119db750289d089c285db7f'
            '027518a8807414b801005a5bc3ff165a0385d275f285c074d7ebec31c05a5bc3e976055589e55351565783ec'
            '0689c18956f689d3803f007426807f01007429bbffff31d2e8380b8946f28b46f6e84a0b8946f48b5ef229cb'
            '3b5ef4731731c08d66f85f5e595b5dc38a079889c289c8e83f0bebeb8b76f68a049889c289c8e8fe0a89c285'
            'c074d68b4ef48cd889f78ec089d6f3a69ff6c440750589d141ebb489d0ebbce9280b5589e55383ec0289d38d'
            '56fce8250b85c07513f646fc01740583fb02740d31c08d66fe5b5dc3b8ffffebf5b80006e8f609ebed5589e5'
            '535181ec900089c189d38d866fff31d2e8000b85c0740fc7066c05010031c08d66fc595b5dc3b419cd210441'
            'c6866dff3a88866cff8d866cffc6866eff5ce87d0a89c24285c9751139d3761889d8e8ce0a89c185c0741189'
            'd38d966cff89c8e8770bebbb89d0ebe6c7066c050500ebaf5589e553515683ec1289c38d46e8e8810b8d46e8'
            'e8050c89c189d685db750c89c889f28d66fa5e595b5dc38907895702ebed5589e55351565783ec0289c389d6'
            'e84e11528b3f8b4702ff36f0048946f689fb89c131d2b8df63ff36ee04c74410ffffe88c1189f0e8e31385c0'
            '750b89f08d66f85f5e595b5dc3a1f4048b1eee04995629c389d88b1ef00419d38b4ef65331d25089fbb8df63'
            'e85611ebd152ba5a05e88eff5ac35589e553528b46048d5e088b5606e81e165a5b5dc300eb0203009affff00'
            '000bc0750e5033c0ba0f00e8dc1658b44ccd218c068a01575606535253be94068936a5018c1ea701268a0788'
            '04464384c075f589360a005b2bc0509affff000057579affff0000fc1e07b99406bf58052bcf32c0f3aab854'
            '0ea3a101b8ffffa3a301b430cd21a28c0188268d019affff0000a28e0188268f0186e0a3900154583bc47512'
            '0f01e0a90100740ab001a29201b003a293011ebfad0157b89000509affff0000b8ad01893ea9018c1eab01b8'
            'ff00e8dd15e846f2e96b154f70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374'
            '656d2e20436f7079726967687420286329204f70656e20576174636f6d20436f6e7472696275746f72732032'
            '3030322d323032362e20506f7274696f6e7320436f7079726967687420284329205379626173652c20496e63'
            '2e20313938382d323030322e00c3cb005589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074'
            '173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026c607001e8d9e7aff531eb8c20050'
            '31c0509affff000089f8e9b614515689c189d6e8a51585c0750689c889f2eb9889d8e99e14b86c05c33d4100'
            '7c083d5a007f03052000c35389c3f647072074025bc38b4708e801fc85c074f4804f0720f647070775ea804f'
            '07025bc35356be6e058b1c85db74163b4702740489deebf18b078904a17005891e700589075e5bc3538b1e70'
            '0585db75025bc38b07a3700589d8e8b601ebea53515256578b3e700585ff7514be420281fe5a03734af64406'
            '03743483c60eebef8b058b7502a370058b4c06bb0e0089f031d2e80a15894c06897502a16e05897c04893e6e'
            '05890589f05f5e5a595bc3b80c00e8720789c785c0741831c9ebccb81a00e8620789c785c07408b900408d75'
            '0cebb8c7066c05050031c0ebcc5389c388d08b1730e4e8b715ff470a5bc35589e551565783ec0489c689d789'
            'da8b5c048b47063d0100740c85c07403e97300c7470601008b440630e48b5c042430806406cf8946f6837f04'
            '00750589f0e81616c746f80000f6440704740d806407fac746f80100804c0701b9b30f89d389f089fae87b1f'
            '89c2837ef800740d806407fa89f0804c0704e86d01f64406207403baffff8b46f609440689d08d66fa5f5e59'
            '5dc331c0ebf45589e5565783ec045089de89c889d38d7ef88b5ef688d089f2b442cd2136890536895502d1d2'
            'd1ca85d27c0d8b46f88b56fa8d66fc5f5e5dc3e8370599ebf35351525689c189c3b43ecd2119d231f685d27c'
            '15833e600300751589c831d2e8d00389f05e5a595bc3e8080589c6ebeb89c8ff165a0389c385d2750485c074'
            'db89c8ff165c0389d8ff166003ebcd53525685c074558b36720585f6743439c677088b1401f239d0724389f3'
            '8b740285f6740c39c677088b1401f239d0722e8b770485f6740c39c677088b1401f239d072518b36c00385f6'
            '75158b36be0385f6740939c6765a8b7404ebf3eb4feb3439c677088b1401f239d0722889f38b740285f6740c'
            '39c677088b1401f239d072138b770485f674c339c677bf8b1401f239d073b78cda89f3e8f420893672053b36'
            'c0037308a1c2033b440a720e5e5a5bc38b1401f239d072dbeb9c8b440aa3c203ebea5589e5535152565783ec'
            '0289c6c746f40000f64407107403e96d008b5c04837f0400743b806406eff644072075318b44029989d309c3'
            '741489d189c3ba0100f7d98b4408f7db83d900e864fe83faff750e3dffff7509c746f4ffff804c06208b5c04'
            '8b4704c7440200008904837ef40075098b5c04f6470801755f8b46f48d66f65f5e5a595b5dc3806407eff644'
            '060274cd8b7c048b450485c074c389c78b4c0285c974ba837ef40075b489cb89fa8b4408e8912189c23dffff'
            '750d8946f4804c062001d729d1ebd885c075f6c7066c050c00c746f4ffffebe58b4408e8dd213dffff759689'
            '46f4eb9153525689c68b5c04837f0400744c8a4407a820740aa8067406b80020e8b7218b5c04806406fb8b47'
            '0489048b440630c080e4243d002475328b440885c0752b894402e8c72189c23dffff75138b440285c07e328b'
            '44025e5a5bc3e82913ebaf8b1c8807c744020100ebe9f64407047410bb01008b148b4408e8b521894402ebcc'
            '8b5c0aebee7506804c0610ebc6c744020000804c0620ebbb5389c3e862ff85c07505b8ffff5bc3ff4f02ff07'
            '8b1f8a47ff30e45bc3535689c38b77048b44063d0100740985c07544c744060100f6470601753fc7066c0504'
            '00b8ffff804f0620f647064075233d0d007512ff4f02837f02007c398b378a044630e489373d1a007507b8ff'
            'ff804f06105e5bc3b8ffff5e5bc3ff4f02837f02007c0b8b378a044630e48937ebba89d8e86dffebb389d8e8'
            '66ffebc9565789c689d7e8a30085c97f06751285db760ea880750a80cc8089c289f0e8a20089fa89f0e86afc'
            '5f5ec35351ba010031db31c9e85bfc595bc385c07506e8922031c0c3e99bfd5589e5535156575089d6bb2000'
            '31d2b103e850108a0484c0741888c3240730ff30e4d3fb89c7035ef68a852201460807ebe28d66f85f5e595b'
            '5dc3730750e87d0158eb022bc0c37305e87201eb022bc0c35189d189c28ac1b43dcd2172028907e8d8ff59c3'
            '533b067c03730c8b1ea603d1e001c38b075bc331c05bc3533b067c0372025bc38b1ea603d1e001c389175bc3'
            'b87405c35189d189c2b43ccd2172028907e896ff59c35189d189c2b45bcd2172028907e884ff59c353515256'
            '89c6d1e0813ea6037e03741c8b1ea60389c289d8e81c2089c185c9752ac7066c0505005e5a595bc38b1e7c03'
            '891e7605e8fc0189c185c074e08b1e7c03ba7e03d1e3e84320ebd2a17c0389f331d22b1e7c03d1e0d1e301c8'
            'e84c0f890ea60389367c03ebbea1a6033d7e037501c3e8aafba17605c706a6037e03a37c03c353515689c3a1'
            '7c0339c373243b1e7c03732a89d9d1e185d2742789d8e808208b36a60301ce80ce40891489d85e595bc389c1'
            'd1f901c840e844ffebd0bbffffebe98b36a60301ceebdf535689c389d68a07433c4172063c5a770204208a24'
            '4680fc41720880fc5a770380c42038e0750484e475db88c388e030ff30e429c389d85e5bc3535289c288c330'
            'ff891e74053d00017338803e8c0103722b3c507504b00eeb063c227217b01388c330ff8a87aa039889c2b8ff'
            'ff89166c055a5bc33c207204b005ebe33c1376dfebdb88e230e6ebe25289c2e8abff89d05ac35589e5535156'
            '5783ec0689c6ba010031db31c9e81efa8946f689d783faff75133dffff750eb8ffff89c28d66f85f5e595b5d'
            'c3ba020089f031db31c9e8f5f98b5ef68946f28956f489f989f031d2e8e3f98b46f28b56f4ebd1515789c789'
            'd988d0e30a8cda8ec2f2ae75024fa98bf989f85f59c3515789c78cd88ec083c9ff33c0f2aef7d14989c85f59'
            'c3535689c38a0730e439d0740c89de8a044384c075ef5e5bc389d85e5bc35289c2b441cd21e87efd5ac35351'
            '89d389c2b80043cd217202890fe85efd595bc35689c6b447cd21e851fd5ec35589e5535152565783ec0489c7'
            '85c074523deaff774d05030024fe8946f23d0600724cc646f40031d28b46f23b06c203764e8b36c00385f674'
            '3c85f674538b4c0a8936c00339f9720f8cda89f389f8e8611a89c285c0755e3b0ec203772d8b7404ebd731c0'
            '8d66f65f5e5a595b5dc3c746f20600ebad8936c2038b36be03ebba31c08b36be03a3c203ebaf890ec203ebcd'
            '807ef400740f89f8e80c1e85c07416c646f400eb8389f8e8f01e85c074e8c646f401e973ff89d0ebab515657'
            '89d685db75045f5e59c38cda89d989c78ec257acaa84c0e0fa33c0d1e9f3ab13c9f3aa58ebe05589e5535152'
            '5683ec0289c3b42acd2181e96c0788c588c830e48956f689470a8a46f7488947088a46f630e4894706b42ccd'
            '2188e830e489470488c889470288f030f689d6d1e6d1e6890701d6b42acd2181e96c0788c5d1e689d03a56f6'
            '7406837f0417751089f0c74710ffff8d66f85e5a595b5dc388ca30f689570a88e24a30e4895708894706ebdc'
            '5589e55351565783ec0a89c68d5ef28d56eee8431e8b5ef28b4ef48946f65631c089d7508b56f0508b46eee8'
            'a705e844058b4ef689f0030eee048b1ef00411fbe8ee0785c07408a1f4049929c119d38b46f03d01007f3e75'
            '07817eee0f26733585db7f02750e3d01007f2a7507817eee0c26732185db7c0d89c889da8d66f85f5e595b5d'
            'c38b46f085c07c0975ea817eeedf6377e3b9ffff89cbebdc52a1f60489c230e624fc80e201a3f60489d05ac3'
            '52a1f60489c230e60c01a3f60480e2018026f604fdebe5535689c389d68d570131c0803f2d740e803f2b7502'
            '89d3890489d85e5bc3b80100ebf2535689c389d631c0803f307ce7803f397fe2ba0a00f7ea89c28a079801d0'
            '432d3000ebe45689dee8d6ff89c3803f3a74025ec34089f2e8c7ff89c3803f3a75f14089cae8baff5ec35589'
            'e55351565783ec0489c68956f431ff803c3c744189f28a0c84c9754089f329d3895ef681fb80007e05c746f6'
            '80008b5ef68b46f4e8811b8b5ef4035ef6c6070085ff740680f93e75014689f08d66f85f5e595b5dc3bf0100'
            '01feebb885ff740880f93e74b746ebae80f92c74af80f92d74aa80f92b74a580f9307ce980f9397e9bebe255'
            '89e55351565783ec0a89c789d68d56f2e8fcfe89c78a053c307c043c397e0489f8eba58d4eee8d5ef031c08d'
            '56f68946ee8946f08946f689f8e81aff89c78b46f6d1e0d1e0b10489c2d3e029d0bb3c000346f031c999e8df'
            '1d89c18b46ee8956f49901c889048b46f411d0894402837ef20074abf75c02f71c835c0200eba05589e55351'
            '565783ec0889c689d7bbffff803c4a7505bb010001de803c4d75034631db8d56f089f0895d10e885fe89c289'
            'c685db742c8b46f089450e31c0c746f202008946f68946f4803c2f74528b46f689058b46f48945028b46f289'
            '4504e9e5fe8b46f04889d3894508803f2e75298d56f08d4401e83afe89c389c68b46f0894506803f2e75118d'
            '56f08d4701e822fe89c68b46f089450cc7450e0000eb9c8d4ef68d5ef48d56f289f0e829fe89c6eb9c535156'
            '89c689d189d0bb100e99f7fb29440489c8bb3c0099f7fb99f7fb89c829540299f7fb29145e595bc35589e553'
            '51565783ec0a89d38d56f089c1e8c2fd89c639c17405803c2c740589c8e948fe408d56f689c1e886fd8d56f2'
            'e8a3fd89c639c174e6803c2c75e1837ef6007406837ef20175d540837ef600740b837ef2017505c746f20500'
            '8d56ee89c1e872fd89c639c174b5803c2c75b046837ef200752e8b7ef0d1e78b56ee8b852801c74710010001'
            'c289570e8d56f489f089f1e840fd89c639c17483803c2c741de97bffc7471000008b46f0488947088b46f289'
            '47068b46ee89470cebce8b56f489d8f7dae80dffe99ffd5589e5565783ec26505253518cd88d7ed68ec0bef8'
            '04a5a5a5a5a5a5a5a5a58d7ee8be0a05a5a5a5a5a5a5a5a5a58b76d48d56d689f0e8fcfe89c339f07405803f'
            '2c740989f08d66fc5f5e5dc38d77018d56e889f089f1e8dbfe89c339c6744e803f2c7549807f010074438d56'
            'fa40e899fc89c389c6803f0075c9bb12008d56d68b46d2e88618bb12008d56e88b46d0e87a188b46fa998b7e'
            'ce89c1a1ee048b1ef00429c819d38905895d02eb9689c8eb945589e553515283ec0489c331c0a3f204803f3a'
            '750143bae80389d8e883fcbaee04e802fd89c38a0784c07505a26904eb52a1ee04c706f204010005f0f18946'
            'f6a1f00415ffffba69048946f889d8c706f404100ee84afc89c3803f2c752c8d4ef6bbd603bac40340e8e3fe'
            '89c3803f007511a1ee042b46f6a3f40489c2b8d603e8d5fd8d66fa5ae96afc8d56f6e896fc89c3a1ee042b46'
            'f6a3f404803f2c75e3bac4038d4701e8fdfc89c3803f2c75d3bad60340e8effcb8d6038b16f404ebc085c075'
            '0fa0f604a8017410a802740cb80100c38026f604fd31c0c3b80100800ef60402c352b8ce00e8b61a89c2e8d0'
            'ff85c0750485d275025ac389d0e801ff5ac35351565789c189c3bf640081c36b0731d289d8f7f789ded1eed1'
            'ee29c631d289d8bb9001f7f3bb6d0101c631d289c831c981eecc01e8261a31db01f011da5f5e595bc35589e5'
            '5657505289de89cf8b56048b460685c9751f81fbc0a8731985c07f06751385d2760f81c6805183d7018346fa'
            'ff8356f8ffbb8051b9010029d619c789f089fae8d31abb8051b901000146fa1156f889f089fae8c01a89df89'
            'ce89d889cabb100e31c9e8b01a8b5e0889f231c9894704bb100e89f8e89e1a89df89ce89d889cabb3c0031c9'
            'e88e1a8b5e0889f231c9894702bb3c0089f88b7608e8791a8b46fa8b56f831c9891cbb6d01e8691a8b4efa89'
            'c389c6e804ff81c36b0729c185c97c638b5e08bf2a0189770a8d846c07894f0ee85d1985c07403bf440189c8'
            'bb1f0099f7fb89c3d1e301fb3b4f027c01408b5e08894708d1e001c72b0d418b46fa894f06bb07000501008b'
            '56f883d20031c98b7608e8001a89f0895c0c8d66fc5f5e5dc2060089d84ee80719056d014b01c1eb8b535156'
            '89c389d65231c0c744100000508b4f02508b0731d289c3b8df63e8acfee9a5fe52ba7805e8d6ff5ac35589e5'
            '5351565783ec1489c68b441085c074153d0100750d8b440e488d66f85f5e595b5dc3e9930089d0056c07e8a3'
            '1885c07503e97800bb44018b4408d1e001c38b47022b078946f631c0c746ea01008946e48946e68946e88b44'
            '08c746f400008946ec8d46e48956eee85ef88b440c2b46f0050700bb070099f7fb8b44063d05007534b10348'
            '89d789c3d3e04729d801f83b46f67e1d8b44062d02008b5ef2b10301d389c2d3e029d001d8e96dffbb2a01eb'
            '8689d8ebe548ebe28b440ee95bff565789c689d7837c10007510837d1000750a8b44083b45087f167c1a89da'
            '89f0e81cff89c689da89f8e813ff39c67e06b801005f5ec331c05f5ec35589e5535152565783ec1689c68b44'
            '1085c07c0589c7e9900031ff833ef204007503e98100bad603b8c4038b5c0ae894ff8946ee85c07503e97900'
            'c746f2d603c746f4c4038b44088946f08b440a056c07e8831785c07468bb44018b46f0d1e001c38b57022b17'
            '8b5ef2c746e000008b47108956e485c0754c8b46f03b47087e4dbf0100837ee000740f8b56f289f0e8bb0185'
            'c07532bf010085ff752e8b46ee85c0740289c7897c1089f88d66f65f5e5ae971fec746f2c403c746f4d603eb'
            '85bb2a01eb96e97d00e9a600e9a80075b48b440c0507002b470cbb070099f7fb8b440629d08946e68b440c8b'
            '5ef20506002b470cbb070099f7fb8b44064829d08b5ef28946ea8b47063d0500751c8b46e42d07003b46e67c'
            '03e969ffbf01003b46ea7c03e964ffe95bff48b10389c2d3e229c289d0403b46e67fdebf01003b46ea7fe1e9'
            '3fff8b570e3d010075158b440a056c07e8811685c074073b162e017e01424a8b440e39c27fafbf010074b5e9'
            '13ff31ffe923ff8b5ef48b4710c746e2000085c0750c8b46f03b47087e0731ffeb52e98d00754d8b440c0507'
            '002b470cbb070099f7fb8b440629d08946ec8b440c8b5ef40506002b470cbb070099f7fb8b44064829d08b5e'
            'f48946e88b470631ff3d050075348b46e42d07003b46ec7c22bf0100837ee200740a8b56f489f0e85c0089c7'
            '8b46ee85c07503e9a9fe29f8e9a2fe3b46e87de2ebda48b10389c2d3e229c289d0403b46ec7fc63b46e87fca'
            'ebc28b570e3d010075158b440a056c07e8a51585c074073b162e017e01424a8b440e39c27f9e31ff39c2749e'
            'eb96535689c689d38b540431c03b57047c1275138b54023b57027c0875098b343b377d03b801005e5bc35356'
            '89c38b378d440189078814ff470a5e5bc3515689c6b91023e8780c01c6c604005e59c300c3c3535289c3833e'
            '7a03007404ff167a03ff161c05baff00b81000e8a800ff161c05ff161e0589d8e9b6e95289c2ff161c05ff16'
            '1e0589d0e9a6e95589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b17'
            '85c0750485d274df1eff1f1febd95589e5535152565789c11ee8a1eabf460589fabb2e0589fe88c839fb7316'
            '803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89b'
            'ffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee84aea88d5bf580589fabb460589fe88c839fb73'
            '16803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c0075'
            '05e827ffebeee83bffebe91f8d66f85f5eeb9f803e200500750331c0c35250cceb0657564944454fb8010083'
            'c404c3515789c789d988d0571e078ae0d1e9f3ab13c9f3aa5f89f85f59c35352565780266502f8be4202800e'
            '6502048b440685c07508a370055f5e5a5bc3b80c00e847f285c0742a89c7a16e058975028905897c04c74504'
            '0000893e6e058b7c04c6450a008b7c0483c60ec745060000ebbdb80c00e813f289c785c075ccbb0100b8d200'
            '8cdae9b0e95589e5535152565783ec02d1e0b10389c2d3e08b1e6e0529d0ba420231f601c231c98956f485db'
            '742e8b3f8b5f028a4707ba0100a840740c89d8e8cae04109c689fbebe1a80875f03b5ef472f381fb880273e5'
            '31d2ebe185f6740fbbffff89d88d66f65f5e5a595b5dc389cbebf0b80500eb8d31c0e888ffe9a8e953515657'
            '89c189d38b77048b44063d0100740c85c07403e96f00c744060100f64706027503e969008b7704837c040075'
            '0589d8e87000be000483f90a7528be0006f6470640751f8b3f804f0710c6050dff07ff47028b47023b470a75'
            '0989d8e8d4eb85c075238b3f804f0710880dff07ff470285770675088b47023b470a752089d8e8b1eb85c074'
            '17b8ffff5f5e595bc3c7066c050400b8ffff804f0620ebec88c830e4ebe6535689c3e8b2e8837f0a00750c8a'
            '4707a8027440c7470a86008b470ae8c2f08b77048944048b7704837c04007538806707f88d470c8b7704804f'
            '0704894404c7470a01008b77048b4404c74702000089075e5bc3a8047407c7470a4000ebbac7470a0002ebb3'
            '804f0608ebd8535689c689d3c7470200008a043c2d740b3c23750d804f020146ebef804f0208ebf73c2b750a'
            '804f0204806702fdebe93c20750cf647020475df804f0202ebd93c307505884719ebd089f05e5bc351565789'
            'c689d189dac6471920e8a2ff89c7c74706000089c6803d2a74228a043c307c2e3c397f2aba0a008b4706f7ea'
            '89c28a04982d300001c246895706ebde89cf8305028b3d8b45fe89470685c07c3846c74708ffff803c2e7548'
            '46c747080000803c2a742b8a043c307c373c397f33ba0a008b4708f7ea89c28a04982d300001c246895708eb'
            'def75f06804f0208ebbf89cf8305028b3d8b45fe89470885c07c21468a048d54013c68731e3c4c73573c4975'
            '55807c01367536807c02347530e97700c74708ffffebd88d7c02770e807c0168755089fe804f0210eb133c74'
            '73153c6c75263a4401751689fe804f028089f05f5e59c3764f3c7a744b3c7775f046804f0240ebe9eb0aeb1c'
            '3c6a75e189d6ebd9762a3c5774123c4e75d389d6804f0302ebcbeb0b3c4675c546804f0304ebbe89d6804f02'
            '20ebb683c603ebad89d6804f0301eba946eba6565789c689d789da8ec731c039d0740c89f34626803f007403'
            '40ebf05f5ec35589e551565783ec0289de89c389d731c983feff75278ec7268b0785c0741489c28d46f883c3'
            '02e872113dffff74e701c1ebe389c88d66fa5f5e595dc38ec7268b0785c0741839f17f1489c28d46f883c302'
            'e847113dffff74e301c1ebdf39f17ed189f0ebcf5589e5515683ec0a5289d9bb10008d56f2e885118d46f28b'
            '5ef0e8d9ed89c601cb89c889f24e85d27e0f89c24b4885d27e078a52f28817ebea8b76f001c689c24e4885d2'
            '7e05c60430ebf38b5ef001cbc607008d66fce97aff5589e5565783ec0889c68956f8895ef6894efa85c97c28'
            '8b5ef8837f08ff7505c747080400bb0a008b46fa89f2e810118976f48d4401803c00741489c6ebf4f75efac6'
            '042df75ef6835efa0046ebc88b5ef8837f08007435c6042e31ff89c68b5ef83b7f087d238b46f6bb0a0031d2'
            '31c9c746fa0000e8e20e8956fa8946f68a46fa043047880446ebd5c60400f646f78074423b76f474114e803c'
            '2e75014e803c397538c60430ebeac60431468a048d7c013c30750489feebf33c2e7510c604308d7501880580'
            '3c30750346ebf8c6043046c604008d66fc5f5e5dc3fe04ebf55389c3f64702087522807f1930751c8b47062b'
            '470c2b470e2b47102b47122b47142b471685c07e0301470e5bc35589e5565783ec0689c78956fa894ef8837f'
            '10007eb28e46fa8d46f6268b15e8ae0f83c70289c13dffff74e43b47107f198d76f64983f9ff74d68a049889'
            'c289d846ff56f8ff4f10ebeac747100000e976ff272b902f722f902f272bb82c272b902f902f902f902f902f'
            '902f902f902faf2e902f902ffb2c902f902f902f902f782d902f902f902f902f902f902f902f902f272b102e'
            '242fc52d272bb82c272b902fc52d902f902f902f902f902f172eaf2e902f902ffb2c902fc52d902f902f782d'
            '5589e5565783ec125089d789dec7471600008946f48b471689471489471289471089470e89470c8a47188c5e'
            'f63c6973393c64756a8a4402a88074618305088b1d8b47fc8946f28b47fa8946f08b47f88b4ffe8946eee9e5'
            '008b46e889f389faff46f4ff162205e9b50176c93c7875238a4402a88074308305088b1d8b47fc8946f28b47'
            'fa8946f08b47f88b4ffe8946eeeb293c7574d93c6f74d5eb1feb653c6274cd3c58ebf2a840742a8305048b1d'
            '8b47fc8946f88b47fe8946fa8a44182c41c746ec0a003c37775130e489c3d1e32effa75e2a8305028b1d8b47'
            'fec746fa00008946f8f64402207407c746fa0000ebcaf644021074c48a46f830e48946f830c0ebb5a8407416'
            '8305048b1d8b47fc8946f88b47fe8946faeb23e9a4038305028b1d8b47fe998946f88a44028956faa8207461'
            '8b46f8998946f88956fa30c0f6440280745985c97c10750a837ef2007504837ef00084c0745a8b440c89c28b'
            '5ee84201c389540cc6072df6440280743631c02b46ee8946eeb800001b46f08946f0b800001b46f28946f2b8'
            '000019c889c1e927ffa81074a58a46f898eb98837efa007cb5ebaff75efaf75ef8835efa00e908ff8a4402a8'
            '0474148b440c89c28b5ee84201c389540cc6072be9edfea8027503e9e6fe8b440c89c28b5ee84201c389540c'
            'c60720e9d2fef64402207503e966fe8305048b1d8b47fc89f28946f88b47fe8b5ef88946fa89c18b46e8e820'
            'fcbbffff8b46e88cdae837fb89441089f08b56f6e8eafc8b46f4e9d9fc8b5ee8c607008a4403a804753fa802'
            '8305028b1d8b5ffe85db74068c5ef6895ef4807c1853754ef6440220743a8b46f48b56f68b5c08e8edfa8944'
            '10837c08007cb03b44087eab8b4408eba38305048b1d8b47fe8b7ffc85c0750485ff74c2897ef48946f6ebba'
            '8b46f48b56f68b5c08e8d2faebc4f644024075ecebb08a4402a8017441a8807503e9820085c97512837ef200'
            '750c837ef0007506837eee0074248b440c89c28b5ee84201c389540cc607308b440c8b5ee889c201c3428a44'
            '1889540c8807c746ec1000837c08ff7404c64419208b5ee8035c0c8c5ef6895ef4f6440280743d837c080075'
            '3985c97535837ef200752f837ef0007529837eee0075238e46f631c026c60700eb488b46fa0b46f8eb8ac746'
            'ec0200ebaec746ec0800eba7eb5cff76ec8b46e88b5ef203440c8b56ee5089c88b4ef0e82f0c807c18587506'
            '8b46e8e83b0dbbffff8b46f48b56f6e8d1f989441089c23b44087c4af64402017503e98afe837eec0a7df785'
            'c07409c45ef426803f3074eaff440ee971fe837c0800750e8b46fa0b46f875068e46f6e972ff8b5ee88b4eec'
            '8b46f88b56fa035c0ce8fd0ceb948b440829d089440ee93efe837c0600750bf64403047458c7440609008064'
            '02f98305028b1d8b4ffef6440304744883c302891d8b56e88b47febb0400e8c3f98b5ee889c88d5705c64704'
            '3abb0400e8b1f9807c185075068b46e8e87e0cbbffff8b46f48b56f6e814f989440ce9dafdc744060400eba6'
            'bb04008b56e889c8ebcec744100100f644024075108305028b1d8a47fe8b5ee88807e9aefd8305028b1d8d46'
            'ea8b57fee88f0a3dffff7503e998fd8b5ee88a46ea880730e4e8b10c85c074ec8a46eb884701ff440ce97bfd'
            '8305028b1d8b46e88b57fee85c0a3dffff7403e962fdc744100000e95dfdc7440600008b5ee88a44188807c7'
            '440c0100e948fd5589e5565783ec6a5389cfc646fa00c746dc0000c746dec800c746e400008946da8956f88b'
            '5ef88a0784c074588d4f013c25740d9889c28d46da894ef8ffd7ebe38d5eda8d569089c8e8e5f689c38a0743'
            '8846f2895ef884c0742a3c6e755c8a46dda8047422834690048b5e908b76908b5ffc8e44fe8a46dca810741f'
            '8a46e4268807eb9fe976018b5e9083c302a8028cd8895e908ec08b5ffeebdaa82074098b46e4268907e97bff'
            'a84074108b46e49926890726895702e969ffeb1fa88074df8b46e499268907b10f2689570226d3fa26895704'
            '26895706e948ff8d4efa8d5eda8d56908d4692e83efa89c68b46e60346e80346ea0346ec0346ee0346f08956'
            'f62946e0f646dc0874198d5e92837ee6007e298a079889c28d46da43ffd7ff4ee6ebea807ef32075e1837ee0'
            '007edbba20008d46daffd7ff4ee0ebed837ee8007e0dba30008d46daffd7ff4ee8ebed8a46f23c737540f646'
            'dc40751a837eea007e218e46f6268a049889c28d46da46ffd7ff4eeaebe68d5eda8b56f689f989f0e8e3f883'
            '7eec007e2bba30008d46daffd7ff4eecebed3c5374dc837eea007ee38e46f6268a049889c28d46da46ffd7ff'
            '4eeaebe6837eee007e148e46f6268a049889c28d46da46ffd7ff4eeeebe6837ef0007e0dba30008d46daffd7'
            'ff4ef0ebedf646dc087503e939fe837ee0007ef7ba20008d46daffd7ff4ee0ebed8b46e4e92bf80000000000'
            '00005156571e5589e583ec0289dec746fe000085c07503e96c0089c783c70383e7fe39c772608eda8b440a83'
            'ff067303bf060039c7774f8eda8b4c088b5c0639cf76508eda8b0739c7774f894c0829f8ff440c3d0600725e'
            '89d901f9894c0689ce8904893f89cf8b77028975028b4704894504894c0489c6894c028eda800f0183c30289'
            '5efe8b56fe89d089ec5d1f5f5e59c38b5c1431c9eba939c1730289c189f08eda0510008b5f0439c375958b56'
            'fe894c0aebd3ff4c0e8b7f02897c068b7704897504897c02ebb15156571e5589e583ec0689d1895efe85c075'
            '03e9840089c68eda83ee02f6040174f18b0489f324fe01c3f607017403e97e0003078b7efe89043b5d067503'
            '8975068ed98b7f028b5f04895d04897f028b7efeff4d0e8ed98b7f0289fa8b040315897efc39d6753b89fb03'
            '0789078b5efe3b57067503897f068b76fc8b5efe8ed9ff4f0c3b770673083b470876038947088b5efe8ed93b'
            '470a770b89ec5d1f5f5e59c3e9bd0089470a89ec5d1f5f5e59c38b5efe89048b5f0639de7203e96c003b7702'
            '77918b5efe8b5f1439de72878b5efe8ed98b5f0e89d88b7efe4031d28946fa8b450cf776fa39d873188b550c'
            '29dad1e039d3734e8ed98b1c8ed901f3833fff75468b5efe8ed98b5f0639de724b8ed939de7303e93dff8b5f'
            '0439de72f68b5f0439de72ef8b5f04ebe68b5f0439de72e38b7efe83c7108b450289fb39c6768de911ffb8ff'
            'ffebadf6070174c78b1780e2fe01d34874abeba48b5efe8b5f14ebad8b7efeff450e8b7efc895c04897c0289'
            '7504897702e901ff5589e551565783ec0289c68956f889dfe871e0a880753c833e700300740e89f0ff165a03'
            '85d2754485c075408b56f889f989f3b440cd2119d285d27c2a89c239f87406c7066c050c0089d08d66fa5f5e'
            '595dc3b00289f331d231c9b442cd21d1d2d1ca85d27db0e873e1ebdf8b5ef889f9ff167003ebd4e9c8075351'
            '525689c18b366e0531d285f6741a8b5c02854f0675048b34ebf042f647071074f589d8e818ddebee89d05e5a'
            '595bc3b8ffffebca52a19401c7069401000085c07507833e76030075025ac3b80100ff165a03ff1676035ac3'
            '51565789c689d789d9833e6e0300740cff165a0385d2751685c0751289fa89f3b43fcd2119d285d27c0c5f5e'
            '59c389fbff166e03ebf4e8d0e0ebef5351565789c189d385c0743285d27435e82d0789c689c8e8350789c785'
            'c0751789d8e8d7e189c785c0742189f389cae8230089c8e8b1db89f85f5e595bc389d0e8b9e1ebf4e8a0db31'
            'c0ebed89f289c8e8fc06ebe251565789d989d689c7578cd88ec0d1e9f3a513c9f3a45f89f85f5e59c353523d'
            '05007d1089c28b1ea603d1e201d3f647014074035a5bc3804f0140e853d585c074f28b1ea60301d3804f0120'
            '5a5bc331c0c353565789c68b1ebe0331ff85db740e39de720789df8b5f04ebf1897702897c02895c0485ff74'
            '39897504c744100000c744080000c7440c00008d4410c7440e00008944128b1c89441483eb16894406895c16'
            '01f38d4416c74716ffff5f5e5bc38936be03ebc4535289c38b1789d005030024fe39d0722089c2051e0039d0'
            '72173b0626057305a1260524fe890785c07403b801005a5bc331c05a5bc353515289c331c050ff379affff00'
            '005a595bc35589e5535650e817068d46fae8deff89c385c075078d66fc5e5b5dc38b46fa2d0200890789d8e8'
            '28ff89c6800c01c7470a0000050200ff470ce856dab80100ebd45589e550a1280585c07409833e8801fe7506'
            '31c089ec5dc38d46fee858ff85c074f28b46fee897ffebea5589e551565783ec0a50525389c38b4708bb0c00'
            '99f7fb8b7eeec746f42a018b7d0a89d681fffdf47c168b5eee8b4708bb0c0099f7fb01c785f67c1185ff7d13'
            'b8ffff89c28d66fa5f5e595dc34f83c60cebe58d856c07e8560185c07403e9cd008d450399b102d3e21bc2d3'
            'f8bb6d019931c98946f089f88956f299e819018b4ef001c18b46f211d08946f28d4563bb640099f7fb9929c1'
            '8b46f219d08946f28d852b01bb900199f7fb9989f301c18b7ef211d7d1e3035ef48b078b76ee9989fb01c18b'
            '440611d39901c811dabb3c0005ffff83d2ff8946f68956f88b440431c9998b7eeee8b00089c68b450289d399'
            '01f011dabb3c0031c9e89c008b5eee89c78b0789d69901c711d685f67f02752683fe017c32750681ff805172'
            '2a8346f6018356f80081c780ae83d6feebe2c746f44401e92bff8346f6ff8356f8ff81c7805183d601ebc38b'
            '46f885c07d03e9effe7507817ef6de6372f4837eec00740e8b5eec8b46f689078b46f88947028b46ea85c074'
            '0789c3893f8977028b46f6bb8051b9010005219c8b56f883d2ffe8070001f811f2e9adfe9350920bc07402f7'
            'e2910bc07404f7e303c858f7e303d1c353515289c3a803751db9640031d2f7f185d2750b89d8bb9001f7f385'
            'd27507b801005a595bc331c0ebf8535152565789c78b368e0685f6745e85c0745a8b1485d2745489fb89d0e8'
            '140485c0740583c602ebea89d8e8060485c0752689d0e82304e83d0489c189d8e81904e8330429c175dc89d0'
            'e83e0489c289d8e8370489c3ebc389d0e8fd033d3d0075c289d0e824045f5e5a595bc331c0ebf6000bd27816'
            '0bc97941f7d9f7db83d900e83700f7daf7d883da00c3f7daf7d883da000bc97912f7d9f7db83d900e81a00f7'
            'd9f7db83d900c3e80f00f7d9f7db83d900f7daf7d883da00c30bc9751a4b7416433bda77098bc88bc22bd2f7'
            'f391f7f38bda8bd12bc9c33bca721a75103bd8770c2bc38bd82bc92bd2b80100c32bc92bdb9387cac355562b'
            'f68bee03db13c97211453bca72f577043bd876eff813f64d7820d1d9d1db2bc319caf572f003f64d780cd1e9'
            'd1db03c313d173f1ebdf03c313d18bd88bca8bc633d25e5dc3005389c385c0740df6c6ff740a8837b8020088'
            '57015bc3b8010088175bc35589e551565783ec245289d9c646d6008d76d789d78d5ef8894ef833d2f7378907'
            '89d38a875e0188048b46f84685c075e44e8a0488054784c075f68b46d48d66fa5f5e595dc35689d683fb0a75'
            '0485c07c07e8abff89f05ec3f7d842c6042debf15352bb0100b800018cdae944d45589e5565783ec48538b76'
            '048976f88d76b5c646b4008976fa8b76068b5eb28976f68d76f68bfa85c074278bd08bc333db3b1472098bd8'
            '8bc233d2f73493f73491f73497f73489148bd08bc38bd98bcfeb2c85db74148bd38bc133db33c93b1472df8b'
            'c88bc233d2ebd48bd18bc733c933ff3b1472ce8bf88bc233d2ebc38b7ef689de895eb28a9d5e018b7efaff46'
            'fa881d85c0758385f67403e97cff85c975f985d275f5ff4efa8b76fa8a048b76f8ff46f8880484c075ec8b46'
            '048d66fc5f5e5dc204005589e5565783ec028b76048976fa837e060a750c85c07c15750685db750285c9ff76'
            '06ff76fae812ff89f0ebca31ff29d789fabf000019cf89f9bf000019df89fbbf000019c789f88d7c01c6042d'
            '897efaebcd535289c289c38a0784c0740f2c613c19760343ebf104418807ebf789d05a5bc35589e5565783ec'
            '245351c646d8008d76d989df8b5ed4895efa8d5efa33c93b1772069287d1f73791f73789178bd18b5efa8a9f'
            '5e01881c4685d275d785c075d34e8a0488054784c075f68b46d68d66fc5f5e5dc35689de83f90a750485d27c'
            '07e89dff89f05ec3f7dac6072df7d883da0043ebec5389c38a9f8b0580e30130e789d85bc331c0e974015389'
            'c3b43ecd21e81ad85bc35389c3b468f8cd21e80dd85bc35389c383eb028b0724fe2d02005bc35589e5535156'
            '83ec0289c68d4ef88cd889d389f2e89b0185c0740231f689f08d66fa5e595b5dc3535152568b36be0385f674'
            '3c8b048b5c122d16008b4c043b07741889ceebe9535152509affff000085c07403b8ffff5a595bc389f08b54'
            '02e8e4ff85c075dc89cb89f0e8d502ebd331c05eebe25289c2a12805891628055ac35389c38a0784c07505b8'
            '01005bc330e4e830ff85c0740b807f01007505b802005bc331c05bc3535289c38a1730f689d0e810ff85c074'
            '0788d68a5f0188da89d05a5bc3f6c4ff740c813e2a05a4037401c3e99403e99f035389c38a0730e4e8e2fe85'
            'c0740d807f0100740783c30289d85bc343ebf95352bb010131c031d2a32c05a32a05b88a05e81be75a5bc355'
            '89e553515250c746f80000eb0aff46f8817ef80001731f8a46f830e4509affff000085c074e78b5ef8c7062c'
            '050100c6878b0501ebd78d66fa5a595b5dc353515289c2e8a1ff83faff743e83fafe743983fafd744983fafc'
            '7526bb1f00ba0100b80c06e8b1e6bb1d00ba0100b86b06e8a5e6c7062c050100c7062a05a403eb1e85d27405'
            'b80100eb179affff00003d030a72f1e869ff9affff0000a32a0531c05a595bc35589e5565783ec0c89d6894e'
            'f88d570380e2fe39da7369baffff83ee028ec0268b1c80e3fe39da766789f729da01df8b5ef8897efa89178b'
            '5efa8ec026833fff744826f60701754b8b7efa268b0d268b7d04897ef68b7efa8cda268b7d0231db897ef439'
            'd075388b1ebe038ec026837f0400742b39f3761e8ec0268b5f04ebeb83fa067395ba0600eb90b80200e99b00'
            'e9a400e99c00268b1701da39d673d98ec0268b57063b56fa750a89d7268b5502268957068b7ef83b0d732b8b'
            '7ef48ec08b56f62689550489d78b56f42689550226010c26ff4f0e8b5ef83b0f734b014efa290fe951ff89ca'
            '2b158956f283fa0672c98b150156fa8ec08b56f28b5efa8b7ef426891726897f028b7ef626897f048b5ef48b'
            '7efa26897f048b5ef626897f028b5ef88b0726010431c08d66fc5f5e5dc3b80100ebf489d929d183f90672e9'
            '89d783cf0126893c89f780c90101d731db897ef08cda26890d39d075228b1ebe038ec026837f0400741539f3'
            '76088ec0268b5f04ebeb268b1701da39d673ef8b56f08ec083c20226ff470ce84401eb995689d63b06be0374'
            '323b06c00375148936c00385f6750c8b16be038936c2038916c0033b067205750531c0a3720585f67403895c'
            '0485db75085ec3891ebe03ebc88977025ec35589e5535152565783ec04833e8e0600740a8d66f65f5e5a595b'
            '5dc39affff00008ec289c68956f489c331d230c0263a07740a43263a0775fa4243ebf189d829f07503b80100'
            'e8dcd689c189c385c074c189d0d1e0d1e029d0050200e8c6d689c785c0743fa38e068b46f4890e92068946f2'
            '30c98e46f28d4502263a0c7414891d8e46f289c7268a044688074384c075f5ebe1a38c0689d331d2c7050000'
            'e8fce3e96eff89c8e868d0e966ffe86e00a18e0685c07517a1900685c07408e851d031c0a39006a1920685c0'
            '750bc3e841d031c0a38e06ebdfe837d031c0a39206c33d818272083d9a8277032d2100c33d61007c083d7a00'
            '7f032d2000c3535189c189d083faff74098cda39d17506e801d0595bc389ca31dbe886f1595bc35351525657'
            '31ff833e8e0600744031c931d2a18e0689c301d3833f0074188b368c0601ce803c00750683c20241ebe38b07'
            'e8c0cfebf3e8bbcfb80200e8c9d589c3a38e0685c07525bfffffa38c06833e900600743a31d2a1900689c301'
            'd3833f0074178b07e88ccf83c202ebeac707000083c302891e8c06ebd4e877cfb80200e885d589c3a3900685'
            'c0750bbfffff89f85f5e5a595bc3c7070000ebf21b0003013d000100010003017700010001000301dc000100'
            '010003015801010001000301770102004a0003019f01010001000301b401030005000301c501030084000501'
            'd101020066000201d301020066000301ea010100010003010f02010001000301340201000100030199020100'
            '010003010f0d02005b0003014e0d02001e000301550d0100050002006f0d01000a0d0301800d020003000301'
            'b20d020031000301a20e0100010003014f360200050003019b3c0200070003017c3d0200cf000301e83d0200'
            '03000301f53d030084000301f53f020083000000000005000000000000000000000077004f5746494c452e54'
            '4d50004f5746494c4500666f70656e2077006c696e65206f6e650a6c696e652025640a007200666f70656e20'
            '72000a004f5746494c452e5458540072656e616d65004f5746494c452e2a006f70656e006f7766696c652e74'
            '7874005c4f5746494c452e545854004f70656e46696c6500c0ff00416e7369546f4f656d2f444f533343616c'
            '6c0072656d6f7665006163636573730025737c256c647c25647c25737c256400636f6e004f70656e20576174'
            '636f6d00545a00004e6f7420656e6f756768206d656d6f727920746f20616c6c6f636174652066696c652073'
            '74727563747572657300466c6f6174696e672d706f696e7420737570706f7274206e6f74206c6f6164656400'
            '010204081020408000001f003b005a0078009700b500d400f300110130014e016d0100001f003c005b007900'
            '9800b600d500f400120131014f016e01303132333435363738396162636465666768696a6b6c6d6e6f707172'
            '737475767778797a00000000ffff00000000000000000000000c000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000010000000000000100000000000000000000000000'
            '0200010000000000000000000000020002000000000000000000000003000300000000000000000000000200'
            '0400000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000001400010002000200030002000000'
            '000000000000000000000000000000000000000000000000000000007e030000000901010b06040505050203'
            'ff0e0724060801ff000000000000000000000200010003000000000000000000000000000100050009000000'
            '0000000000004553540000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000004544540000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            'e8036904504600000100100e0100000000000000000000000000000000000000000000000000000000000000'
            '0000000000003723372300005a3a5a3a00200100000000000020a80200000020902400000020233c00000020'
            'd83f000000206e250000001433150000001f84400000'
        )),
    'OWFILEL': (
        # OWFILE's source, large model: wcl -bt=windows -l=windows -ml owfile.c
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '7800020000000000020302000004002036110100000002000200030011004000500050005c006200fa000000'
            '0000010000000200000000000000000386007c59400d7c59f12d8805410dd006084f5746494c455f4d000000'
            '010006000d00000455534552064b45524e454c084b4559424f41524400000d6f7766696c655f6d6c2e657865'
            '000000001e5890455589e51e8ed8b876020e3ee82303565781ec520231f6bb10008cd9b812008cda0ee89005'
            '9089c785d2751b85c07517561eb82400501eb81d0050569affff0000b80100e9d102b80200501eb82c005052'
            '570e3ee8b50683c40a89f80e3ee8ac07bb3e008cd9b812008cda0ee846059089c78956f485d2751b85c07517'
            '561eb84000501eb81d0050569affff0000b80200e984025250bb40008cda8d46800e3ee8b907ff76f457bb40'
            '008cda8d46800ee8a90790b802005031db31c989f88b56f40ee8a9089089f88b56f40ee8fa09908946f68956'
            'f889f88b56f40ee82e0790bb48008cd98cda8d46800e3ee8430a89c7c6438000bb4a008cd9b812008cda0ee8'
            '810a9085c07417561eb85500501eb81d0050569affff0000b80300e9f9018d56c01e5231dbb85c008cda0ee8'
            '6e0a9085c07532bb4a008cd98cda8d46de0e3ee8a70a85c075118b46dc3b46f875098b46da3b46f67501468c'
            'da8d46c00ee86f0a9085c074ceb80002501eb84a00500ee8a30c9089c783c40685c07c1a0ee8de0c903b56f8'
            '75103b46f6750b89f80e3ee8d10c85c0741b31c0501eb86500501eb81d005031c0509affff0000b80400e966'
            '0189f80e3ee8eb0c1eb86a00508d96a8fe1e52b80040509affff00003dffff7418bb75008cd98cda8d86b0fe'
            '0ee8cd0c9085d2751f85c0751b31c0501eb88100501eb81d005031c0509affff0000b80700e913011eb88a00'
            '508d56f01e529affff0000807ef080751b807ef1ef75159affff00003d6203750bb4199affffffff3c02741b'
            '31c0501eb88d00501eb81d005031c0509affff0000b80800e9c800b84a008cda0ee8fb0c9085c0741b31c050'
            '1eb8a000501eb81d005031c0509affff0000b80500e99f0031dbb84a008cda0e3ee8db0c85c07518501eb8a7'
            '00501eb81d005031c0509affff0000b80600eb77bb50008cda8d8630ff0e3ee8ec0c31c031d20ee87a0d9089'
            '46ec8956ee8cda8d46ec0ee8340e9089c78ec226837d0a647c05b80100eb0231c0508d9630ff1e5256ff76f8'
            'ff76f68d56801e521eb8ae00508d96a8fd1e520e3ee8120e83c41831c0508d96a8fd1e521eb81d005031c050'
            '9affff000031c08d66fa5f5e1f5d4dca0a00000000000000000000008c164c01c33bc473132bc4f7d83b060a'
            '0077068cd03b064c017401cb0e5ab85703bb0100e9700f537461636b204f766572666c6f772100565589e583'
            'ec0889c68956f8894efa31d285c9750485db740da106023d0100753e8ec12689078e46f8268a043c77753680'
            'ca02b40130c946884efc884efe30ed8e46f8268a0438c5744e84e4744a3c6373333c62755184c9744b88ec46'
            'ebe18ec1268917ebc03c72750580ca01ebc43c61750580ca82ebbbc7069c05090031c089ec5d5ec377073a6e'
            'fe7438ebcc3c74750884c97423ebc2eb453c6e75be3a6efe742eebb5eb163c2b75b13a6efc75aac646fc0180'
            'ca03eba3b101eb9fb10180ca40eb988e46fac646fe0126800f01eb8b8e46fac646fe01268027fee97dff84c9'
            '750b813e08020002750380ca4089d0eb8e5589e5565783ec028b760889c78956fa88d830e40e3ee8740e3c72'
            '7403e9af0031c0f6c1027403b80200f6c1407503e98a0080cc0231d252ff760650ff76fa570e3ee8a5098e46'
            '0a83c40a2689440c3dffff746e26c74404000026c7440e000026094c0a26c45c0626c7470c00008e460a26c4'
            '5c068b46042689470e8e460a26c45c0626c74708000026c7470a0000f6c1807412b802008b560a31db31c950'
            '89f00ee87304908b560a89f0e8ed0d8b560a89f08d66fc5f5e5dc2080080cc01e973ff89f08cc2e8020e31c0'
            '31d2ebe4f6c1017418b82200f6c18074150c10f6c140741280cc02ba8001e94bffb82100ebe60c40ebe980cc'
            '01ebec455589e5565783ec065089d789de894efa8d5ef68b56fa8cd189f0e8f6fd89c185c0750c31d28d66fc'
            '5f5e5d4dca0200e8460e89c38956f885d2750485c0741e528e46fa5089fa268a04ff760698ff76f689c38b46'
            'f4e8adfe89c38956f88b56f889d8ebc1455589e55631f6560ee88fff5e5d4dcb5589e55351565783ec0489c1'
            '89d78b1e9e058b36a00585f6753a85db7536bea2058c5ef48e46f4268b4402268b1c8946f685c0750485db74'
            '4f8ec0268b4706268b570439f8750439ca743f8cc089de8946f4ebd08ec6268b5706268b470439fa750439c8'
            '740b268b17268b770289d3eba5bb010089c889fae8ed0089c889fa8d66f85f5e595b5dc3eb2c268b17268b47'
            '028e46f426891426894402a19e058b16a0058e46f6891e9e052689078c06a00526895702ebc5c7069c050400'
            '31c031d2ebbd455589e5565783ec088b7e068946f88956f689de894efa8d5ef48b56fa8cd189f0e8b9fc89c1'
            '85c074518e4608268b450c833e74030075478b560889f8e8fefe89c789560885d2750485c0741f525031c08e'
            '46fa50268a048b56f698ff76f489c38b46f8e868fd89c78956088b560889f88d66fc5f5e5d4dca040031d2eb'
            'f2ff167403ebb3455589e55351528d560e8b5e0a8b4e0c168b4606528b5608e8cc0d5a595b5d4dcb5589e551'
            '565783ec0689c68956f8895ef68ec226f6440a037503e9970031ff26f6440b107403e990008e46f8268b4404'
            '85c07417f7d89989d1268b540c89c38956f4ba01008b46f4e8560e837ef600740c8e46f8268b440ce8850e09'
            'c78e46f826f6440a08742426c45c06268b4708268b570a0e3ee8b60e8e46f826c45c0626c74708000026c747'
            '0a00008e46f826f6440b08740889f08cc2ff1688058e46f826c6440a002680640b4089f88d66fa5f5e595dc3'
            'bfffffebf1e8c00e85c07503e966ffbfffffe960ff515689c189d6e82eff89c389f289c8e8190b89d85e59c3'
            '455589e55351565789c689d18b1e9e058b3ea00585ff750485db742e8ec7268b4706268b570439c8750439f2'
            '740b268b17268b7f0289d3ebdbbb010089f089cae8a6ff5f5e595b5d4dcbb8ffffebf4455589e551565783ec'
            '088b760689c78956f489d98e4608268b440a89fb30e48956f624302680640acf8946f24985c97f3b837ef8ff'
            '75588b46f63b46f4750439fb740a8e460826f6440a20744231ff897ef48b46f28e46088b56f42609440a89f8'
            '8d66fa5f5e595d4dca04008b560889f00ee83910908946f83dffff74b38e46f68a56f8268817433d0a0074a4'
            'eb9d8e46f626c60700ebba56575589e583ec0289c68956fe8ec1268b4704993b56fe7f06751d39c6771926c4'
            '7f06268b45088ec1262b07993b56fe7c0f750439c67309b8010089ec5d5f5ec32680670aef26013731c02629'
            '7704ebea535689c38ec22680670aef26c47706268b4408268b740a8ec226c747040000268907268977025e5b'
            'c3455589e5565783ec0a89c68956fa895ef889cf8ec226f6440a06746626f6440b107561837e0601750a268b'
            '4404992946f819d78e46fa26c45c06268b4708268b570a8e46fa26c744040000268904268954028e46fa8b56'
            '068b5ef889f9268b440c2680640aeb0e3ee8f50f83faff75403dffff753bb8ffff8d66fc5f5e5d4dca0200eb'
            '19e8b40c85c074c7837e060075e485c97de0c7069c050900ebd88b46063d0200745c3d0100740885c07455eb'
            'e5eb4a268b440489f3998cc18946f48956f68b46f889fae8bdfe85c0742f8b46f48e46fa8b560689f92946f8'
            '8b5ef81b4ef6268b440c0ee8770f9083faff75053dffff74818b56fa89f0e8dbfe31c0e977ffeb46268b440c'
            '0ee8860f908e46fa89c3268b44048956f29929c389d88b5ef219d389da8b5ef829c389d889cb19d38cc189da'
            '89f3e84efe85c074c08e46fa8b56068b5ef889f9eb9889f0e889fe8e46fa8b5606268b440ce90bff455589e5'
            '5351565789c389d78ec226f6470a80740c26f6470b1074050ee82b0f908ec7268b470c0e3ee80a0f89c189d6'
            '83faff75053dffff74198ec726837f0400741026f6470b107410268b47049901c811f25f5e595b5d4dcb268b'
            '47049929c119d689c889f2ebea455589e5565783ec2489c68956fa8d46d88cd2c746f800000e3ee8e10eb103'
            '8e46fa268a0484c0741e88c330ff89dfd3ff80e3078a53d88a9f1e0130f685da7506ff46f846ebdb8b46f88d'
            '66fc5f5e5d4dcb455589e55789df8ec11e928ed8b456cd211fe8ee0e5f5d4dcb455589e551565789c689d78b'
            '46068b560889d98ec789f31e928ed80653b41acd215a1fb44ecd211fe8b30e5f5e595d4dca0400455589e51e'
            '928ed8b41acd211fb44fcd21e8970e5d4dcb31c0cb455589e5565789df8ec189d61e968ed8a675114f4e83c9'
            'ff33c0f2aef7d12bf9f3a6740519c983d9ff1f89c85f5e5d4dcb5589e5565783ec0a89c68956f8895ef689cf'
            'c746f2ffff8e46f826803c20750346ebf78b46f68d56f21630e45224838cc28946f489c389f009fb0ee8380e'
            '9085c0750c8b46f23b0694037203e93601f646f60374478b46f23dffff743f0e3ee8e70185c07536f646f704'
            '7409f646f6207403e92a01f646f64074218d56fa8b5ef28cd031c98ec01e8cc08ed8b440cd2119d21f89c185'
            'd27d03e91e01837ef2ff7559f646f6207503e9e800a1a6053d020075f5014604c45e04268b47fe85c07503b8'
            '80018b16c203f7d221d031dba8807503bb01008d56f21689f0528b56f80e3ee8f40d85c075c08b46f23b0694'
            '037203e9d60085ff7403e9d9008b46f2e89b0d243c89c28b46f20ee83d019085c0740380ce208066f47f837e'
            'f402750380ca03837ef400750380ca01837ef401750380ca02f646f610740380ca8089d00c40f646f703746a'
            'f646f702740289c28b46f2e8920e833e7603007425bbbe008b56f88cd989f00e3ee8c60e85c07512b8ffff50'
            'ff76f231c05050ff16760383c4088b46f2eb1189c3b43ecd2119d2c7069c050b00b8ffff8d66fc5f5e5dc204'
            '008b5ef2b43ecd2119d2c7069c050700ebe3813e080200027596eb928b5ef2b43ecd2119d289c8e8a40eebcc'
            '89c3b43ecd2119d2ebb989c3b43ecd2119d285d27ce58d56f28b5ef41689f05209fb8b56f80e3ee8820c85c0'
            '759be900ff455589e5538cd08d5e0c8ec0268b075031c050ff760aff7608ff760683c3020ee8070083c40a5b'
            '5d4dcb455589e55351528d560e8b4e0c8b5e0a168b4606528b5608e8c0fd5a59ebdde97e0e89c0455589e553'
            '5289c3833e720300751fb80044cd2119db750289d089c285db7f02751aa8807416b801005a5b5d4dcbff1672'
            '0385d275f085c074d5ebea31c0ebe9455589e5e862075d4dcb455589e5565783ec0a89c689d7895ef8894efa'
            '8ec126803f00744126807f0100742bb9ffff31db0ee8620e908b56fa8946f48b46f80ee8730e908946f68b4e'
            'f429f13b4ef6731f31c031ffeb0f268a079889c389f00ee86e0e9089d789fa8d66fc5f5e5d4dcbc45ef8268a'
            '079889fa89c389f00ee8160e908946f289d385d2750485c074c28b4ef6c47ef889c61e8edaf3a69f1ff6c440'
            '75088b76f289d746eb9c8b46f2ebb4455589e50e3ee8410e5d4dcb455589e5515683ec0289de8d5efa8cd10e'
            '3ee83c0e85c07515f646fa01740583fe02740f31c08d66fc5e595d4dcbb8ffffebf3b80006e8ee0cebeb4555'
            '89e551565781ec900089c689d789d98d866dff8cd231db0e3ee8130e85c07413c7069c05010031c031d28d66'
            'fa5f5e595d4dcbb419cd21c6866bff3a04418cd288866aff8d866affc6866cff5c0e3ee86b0d89c34385ff75'
            '1d85f6751939d9762789c80e3ee8e00d89c689d785d2750485c0741889d98d9e6aff5189f089fa8cd10e3ee8'
            'c20eeba289d8ebd7c7069c050500eb92455589e55351565783ec1289c389d78d46e68cd2e8db0e8d46e68cd2'
            '0ee8670f9089c189d685ff751285db750e89c889f28d66f85f5e595b5d4dcb8ec726890726895702ebe74555'
            '89e5565783ec045089de894efa0e3ee88c168b7ef68b5ef6518ec256268b3d268b470231d28946f88ec189fb'
            'ff360e0589c1b8df63ff360c0526c74410ffffe8d4168b56fa89f0e8c21985c0750d8b56fa89f08d66fc5f5e'
            '5d4dcbff76faa112058b1e0c05995629c389d88b1e0e0519d38b4ef85331d25089fbb8df63e89616ebcc4555'
            '89e55351bb8a058cd90ee86dff595b5d4dcb455589e55351528d560e8b5e0a8b4e0c168b4606528b56080ee8'
            'a71c905a595b5d4dcb00eb009affff00000bc0750e5033c0ba0f00e87c1d58b44ccd218c0652015756065352'
            '891e6d018c066f012bc0509affff000057579affff0000fc1e07b9d006bf88052bcf32c0f3aab86e12a36901'
            'b8ffffa36b01b430cd21a25401882655019affff0000a256018826570186e0a3580154583bc475120f01e0a9'
            '0100740ab001a25a01b003a25b011ebf750157b89000509affff0000b87501893e71018c1e7301b8ff00e891'
            '1c0e3ee81eeee91d1c89c04f70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374'
            '656d2e20436f7079726967687420286329204f70656e20576174636f6d20436f6e7472696275746f72732032'
            '3030322d323032362e20506f7274696f6e7320436f7079726967687420284329205379626173652c20496e63'
            '2e20313938382d323030322e00c3cb005589e551565781ec800089c689df8d9e7aff8cd18ec2268a0484c074'
            '173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026c60700168d9e7aff531eb8c20050'
            '31c0509affff000089f8e9661b89c0515689c189d60e3ee8531c85c0750689c889f2eb9489d8ebe2b89c058c'
            'dacb3d41007c083d5a007f03052000cb5389c38ec226f6470b2074025bc3268b470c0ee85dfb9085c074f18e'
            'c226804f0b2026f6470b0775e326804f0b025bc3535156575589e583ec0489c189d7be9e058c5efc8e46fc26'
            '8b4402268b1c8946fe85c0750485db74458ec0268b5706268b470439fa750439c874098cc089de8946fcebd0'
            '268b07268b57028e46fc26890426895402a1a2058b16a4058e46fe891ea2052689078c06a4052689570289ec'
            '5d5f5e595bc35351528b0ea4058b1ea20585c9750885db75045a595bc38ec1268b07268b5702a3a2058916a4'
            '0589d889ca0e3ee89402ebd15589e55351565783ec06a1a4058b3ea2058946f485c0752585ff75218c5ef6be'
            '0a028e46f681fe7203730c26f6440a03740883c612ebeee9a200e983008ec0268b15268b4502268b7504a3a4'
            '05268b45068916a2058ec08946f6268b440a8946f2b912008b56f689f031db0e3ee80d1b8e46f68b46f22689'
            '440a8e46f48b46f62689750426894506268b5d048ec08b46f426897f068b169e0526894708a1a0058e46f489'
            '3e9e052689158c06a005268945028b56f689f08d66f85f5e595b5dc3b812000e3ee8540989c78956f485d275'
            '0485c0742ac746f20000eb85b824000e3ee8380989c78956f485d2750485c0740ec746f200408956f68d7512'
            'e962ffc7069c05050031f631d2ebaa515689c688d88ec230e4268b1c268b4c020ee8db1b908ec226ff440c5e'
            '59c35589e5565783ec0689c68956fa89df8ec2268b4408268b5c068ec026837f0c01741026837f0c007403e9'
            'ab0026c7470c01008e46fa268b440a268b5c0630e42680640acf2430268e44088946f6268b470a268b570885'
            'c0750c85d275088b56fa89f0e8541c8e46fac746f8000026f6440b04740f2680640bfac746f8010026804c0b'
            '01b8d714508b56faff760689fbff760489f00ee8f4289089c3837ef80074148e46fa89f02680640bfa8cc226'
            '804c0b04e801018e46fa26f6440a207403bbffff8e46fa8b46f62609440a89d88d66fc5f5e5dc2040031c0eb'
            'f35589e5565783ec045089de89c889d38d7ef88b5ef688d089f2b442cd2136890536895502d1d2d1ca85d27c'
            '0d8b46f88b56fa8d66fc5f5e5dc3e87d0699ebf35351525689c189c3b43ecd2119d231f685d27c15833e7803'
            '00751589c831d2e8a40489f05e5a595bc3e84e0689c6ebeb89c8ff16720389c385d2750485c074db89c8ff16'
            '740389d8ff167803ebcd455589e50ee803005d4dcb455589e5535189d185d274228cda39d1742189ca31db0e'
            '3ee83c2b3b0eda03740d8ec131dba1dc03263b470a720c595b5d4dcb0ee8ca2c90ebf4268b470aa3dc03ebeb'
            '5589e55351565783ec0889c68956f68ec2c746f4000026f6440b107403e9ac0026c47c06268b450a268b5508'
            '85c0750485d274468e46f62680640aef26f6440b207537268b44049989d309c3741589d189c3ba0100f7d926'
            '8b440cf7db83d900e8c2fe83faff75123dffff750d8e46f6c746f4ffff26804c0a208e46f626c47c06268b45'
            '08268b550a8e46f626c74404000026890426895402837ef400750b26c45c0626f6470e01750c8b46f48d66f8'
            '5f5e595b5dc38e46f6268b440c0e3ee8c92c3dffff75e38946f4ebde2680640bef26f6440a0274a2268b5c08'
            '268b7c068ec3268b450a268b550885c0750485d274888e46f68956f08946f2268b7c0485ff7503e974ff837e'
            'f40075f78e46f68b5ef08b4ef289fa268b440ce8792c89c23dffff75128e46f68946f426804c0a200156f029'
            'd7ebc885c075f5c7069c050c008e46f6c746f4ffffebe0455589e5e8aefe5d4dcb5351565789c689d78ec226'
            'c45c06268b470a268b570885c0750485d2746b8ec7268a440ba820740aa8067406b80020e8942c8ec7268064'
            '0afb26c45c06268b5708268b470a8ec726894402268b440a30c080e4242689143d00247548268b440c85c075'
            '40268944040e3ee8c52c89c23dffff751e8ec7268b440485c07e508ec7268b44045f5e595bc389f089fae81e'
            '19eb8c8ec726c41c2688078ec726c744040100ebda8ec726f6440b04741bba01008ec7268b1c268b4c02268b'
            '440ce89c2c8ec726894404ebac268b540eebe2750726804c0a10eba726c74404000026804c0a20eb9a535156'
            '89c389d1e812ff85c07505b8ffffeb8e8ec126ff4f0426ff0726c437268a44ff30e4e979ff455589e5535156'
            '89c389d18ec2268b4708268b77068ec026837c0c01740d26837c0c00756326c7440c01008ec126f6470a0175'
            '59c7069c050400b8ffff26804f0a208ec126f6470a4075373d0d00752326ff4f0426837f04007c60268b5702'
            '268b378ec2268a048ec1462689570230e42689373d1a00750a8ec1b8ffff26804f0a105e595b5d4dcbb8ffff'
            'ebf526ff4f0426837f04007c1a268b5702268b378ec2268a048ec1462689570230e4268937eb9089d889cae8'
            '1fffeb8789d889cae816ffebaf455589e5565789c689d7e8dc0085c97f06751285db760ea880750a80cc8089'
            'c289f0e8e00089fa89f0e8d4fb5f5e5d4dcb455589e55351ba010031db31c9e8bffb595b5d4dcb455589e585'
            'd2750e85c0750a0e3ee8fe2a31c05d4dcbe87cfc5d4dcb455589e5565783ec02505289de894efab9200031db'
            '0ee8fd1490b1038e46fa268a0484c0741c8e46f688c3240730ff30e4d3fb89c7035ef88a851e0146260807eb'
            'da8d66fc5f5e5d4dcb00730750e8060258eb022bc0c37305e8fb01eb022bc0c3455589e55189d98b5e068e46'
            '081e928ed88ac1b43dcd211f7203268907e8caff595d4dca0400533b06940373118b1ebe03d1e08e06c00301'
            'c3268b075bc331c05bc3533b06940372025bc38b1ebe03d1e08e06c00301c32689175bc3b8a6058cdacb4555'
            '89e55189d98b5e068e46081e928ed8b43ccd211f7203268907e86aff595d4dca0400455589e55189d98b5e06'
            '8e46081e928ed8b45bcd211f7203268907e846ffebda5589e55351525657508b16be038b36c0038cd9d1e039'
            'ce750681fa9603742789c389d089f20e3ee83d2a89c789d685f6754085ff753cc7069c0505008d66f65f5e5a'
            '595b5dc38b1694038916a8050ee85c029089c789d685d2750485c074cf8b0e9403d1e1bb9603518cd90e3ee8'
            'c92aebbc8b4ef4a1940389f231db2b0e9403d1e0d1e101f80ee88513908b46f4893ebe038936c003a39403eb'
            'a153528b16c0038cdba1be0339da75083d960375035a5bc30ee86efa90c706be039603a1a8058c1ec003a394'
            '035a5bc353515689c3a1940339c373253b1e9403732b89d9d1e185d2742889d8e8772ac436be0301ce80ce40'
            '26891489d85e595bc389c1d1f901c840e8fbfeebcfbbffffebe9c436be0301ceebde5689c68ec2268a04463c'
            '4172063c5a770204208ec1268a274380fc41720880fc5a770380c42038e0750484e475d588c388e030ff30e4'
            '29c389d85ecb535289c288c330ff891ea6053d00017338803e540103722b3c507504b00eeb063c227217b013'
            '88c330ff8a87c4039889c2b8ffff89169c055a5bc33c207204b005ebe33c1376dfebdb88e230e6ebe25289c2'
            'e8abff89d05ac3455589e55351565783ec0689c6ba010031db31c9e8d7f88946f689d783faff75143dffff75'
            '0fb8ffff89c28d66f85f5e595b5d4dcbba020089f031db31c9e8adf88b5ef68946f28956f489f989f031d2e8'
            '9bf88b46f28b56f4ebd0455589e55789c788d88ec2e308f2ae75044f8cc1a98bf989f889ca5f5d4dcb455589'
            'e5515789c78ec283c9ff33c0f2aef7d14989c85f595d4dcb515689c689d189da8ec1268a0430e439d0740f89'
            'f3268a074684c075ed31d25e59cb89f089ca5e59cb455589e51e928ed8b441cd211fe8d9fc5d4dcb455589e5'
            '8ec11e928ed8b80043cd211f720326890fe8b2fc5d4dcb455589e55689d688da1e968ed8b447cd211fe89afc'
            '5e5d4dcb455589e50ee803005d4dcb455589e55351565783ec0489c685c074623de6ff775d05030024fe8946'
            'f43d06007260c746f600008b46f43b06dc0376598b0eda0331ff85c9745c89fb89ca89f0890eda030ee8a422'
            '9089c385c0754589f289c80e3ee8892885c075de8ec1a1dc03263b450a72638ec1894ef6268b4d04ebc431c0'
            '31c989ca8d66f85f5e595b5d4dcbc746f40600eb9931c08b0ed803a3dc03eba0eb3089f00ee86c299089c289'
            'c185c0752789f00e3ee8312a85c07403e974ff85d2750f89f00e3ee8222a89c385c074028cd989d8ebaceb1e'
            '3b3ed8037506a3d803e962ff8ec08b46f6268945028ec026895504e950ff268b450aa3dc03e973ff455589e5'
            '565789c789d089ca8b4e0685c9750b89c289f85f5e5d4dca020089de8ec01e8eda57acaa84c0e0fa33c0d1e9'
            'f3ab13c9f3aa581f8cc289c7ebd75589e553515683ec0289c38ec2b42acd2181e96c0788c588c830e48956f8'
            '2689470a8a46f948268947088a46f830e426894706b42ccd2188e830e42689470488c82689470288f030f689'
            'd6d1e6d1e626890701d6b42acd2181e96c0788c5d1e689d03a56f8740726837f0417751089f026c74710ffff'
            '8d66fa5e595b5dc388ca30f62689570a88e24a30e42689570826894706ebd9455589e55351565783ec0c89c6'
            '89d78d56f08d5eec168cd15289fae8be298b5ef08b4ef2578946f45631c08956f6508b56ee508b46ece89e07'
            '0ee82207908b4ef489f089fa030e0c058b5ef6131e0e05e87a0a85c07408a112059929c119d38b46ee3d0100'
            '7f3f7507817eec0f26733685db7f02750e3d01007f2b7507817eec0c26732285db7c0e89c889da8d66f85f5e'
            '595b5d4dcb8b46ee85c07c0975e9817eecdf6377e2b9ffff89cbebdb545a0052a1140589c230e624fc80e201'
            'a3140589d05ac352a1140589c230e60c01a3140580e20180261405fdebe5565789c689d78d54018ec731c026'
            '803c2d741426803c2b750289d68ec189fa26890789f05f5ec3b80100ebed565789c689d78ec731c026803c30'
            '7cdf26803c397fd9ba0a00f7ea89c2268a049801d0462d3000ebe15589e5e8d1ff89c38ec226803f3a74045d'
            'c208008b5e048b4e0640e8b9ff89c38ec226803f3a75e88b5e088b4e0a40e8a5ffebdc5589e5565783ec0a89'
            'c68956f6895ef2894ef48ec231ff26803c3c74528b4ef68e46f689f3268a048846fa84c0754789f029d88946'
            'f83d80007e05c746f88000ff76f88b46f28b56f40ee8072590c45ef2035ef826c6070085ff7407807efa3e75'
            '01468b56f689f08d66fc5f5e5dc3bf010001feeba785ff74073c3e74b146eba43c2c74aa3c2d74a63c2b74a2'
            '3c307ced3c397e9aebe75589e5565783ec0e5089d789de894efa8d5ef28cd1e8c8fe8946f88ec289c38946ec'
            '268a0789d73c307c043c397e078b46ec89faeb9b8d56ee8d5ef41631c08cd18946ee8946f08946f4528d56f0'
            '168b46f8528cc2e8ddfe8946ec8b46f4d1e0b104d1e089d789c2d3e029d0bb3c000346f031c9990e3ee83829'
            '89c18b46ee8956f6998e46fa01c82689048b46f611d026894402837ef200749526f75c0226f71c26835c0200'
            'eb875589e5565783ec0e89c68956fa89df894ef88ec2c746f6ffff26803c4a7508b801008946f601c68e46fa'
            '26803c4d7506c746f6000046c446f68d5ef28b56fa8cd12689451089f0e81afe89c389c68956fa837ef60074'
            '3f8e46f88b46f22689450ec746f4020031c08e46fa8946f08946ee26803c2f741d8e46f88b46f02689058b46'
            'ee268945028b46f48b56fa26894504e993feeb5d8b46f28e46f848268945088ec226803f2e753e8d47018cd1'
            '8d5ef2e8b0fd8e46f889c389c68b46f2268945068ec28956fa26803f2e751a8d47018cd18d5ef2e88cfd8e46'
            'f889c68b46f28956fa2689450c8e46f826c7450e0000e96eff8d56f08d5ef4168cd1528d56ee1689f0528cc2'
            'e884fd89c68956fae966ff515689c68ec289d989d8bb100e99f7fb2629440489c8bb3c0099f7fb99f7fb89c8'
            '2629540299f7fb2629145e59c35589e5565783ec245089d789de894efa8d5ed88946e68cd18956e8e807fd89'
            'c38956f839d775053b46d674088ec226803f2c74098b46e68b56e8e9adfd8d7f018cd18d5edc89f88956f0e8'
            'acfc8d5ede8cd1897eeee8cdfc89c389c78956f63b56f875083b46ee7503e9b9008ec226803f2c75f5837edc'
            '007406837ede0175e947837edc00740b837ede017505c746de05008b46f68d5eda8b56f68cd18946ea89f889'
            '7ee4e87dfc89c33b56f6750739c77503e978008ec226803f2c7570438956f2895ef4837ede00756c8b5ed8b8'
            'ffffd1e38ec08b56da268b877e4b8e46fa01c226c7441001002689540e8b46f48d5ee08b56f28946e28b46f2'
            '8cd18946ec8b46f4e81ffc89c789d13b56f275053b46f474088ec226803d2c743c8b46e28b56ece9c5fc8b46'
            'ee8b56f0e9bcfc8b46e48b56eae9b3fc8e46fa26c7441000008b46d848268944088b46de268944068b46da26'
            '89440ceb948b5ee08b56fa89f0f7dbe86dfe89f889cae97efc5589e5565783ec2a505253518cd08d7ee48ec0'
            'be1605a5a5a5a5a5a5a5a5a58d7ed2be2805a5a5a5a5a5a5a5a5a58d5ee48b76d08cd189f089d7e857fe89c3'
            '89f88956fa39d0750439f374088ec226803f2c740c89f089fa8d66fc5f5ee992fb8d77018956f88cd18d5ed2'
            '89f089f7e822fe89c33b56fa750439c674718ec226803f2c756926807f010074628d5ef6408cd1e824fb89c3'
            '8ec289c689d726803f0075b1b812008d5ee48b56ca8cd1508b46cc0e3ee8b320b812008d5ed28b56068cd150'
            '8b46040e3ee89f208b46f69989c389d18b160c05a10e058e460a29da8b5e0819c826891726894702e966ff8b'
            '56f889f8e962ff5589e55351565783ec0489c68ec231c0a3100526803c3a750146bb02048cd989f08cc2e8fa'
            'fabb0c058cd9e889fb89c68ec2268a0484c07505a28304eb68c70610050100a10c05bb830405f0f18946f4a1'
            '0e0515ffff8cd98946f689f0c7061205100ee8bafa89c68ec289d726803c2c753d8d56f4b8f00316bbde0352'
            '8cd91e89fa508d4401e881fe89c38ec226803f0075138b1e0c05b8f0032b5ef48cda891e1205e8cafc8d66f8'
            '5f5e595b5dc38d5ef48cd1e800fb89c68ec2a10c052b46f4a3120526803c2c75dcbbde038d44018cd9e88afb'
            '89c68ec226803c2c75c7bbf003408cd9e877fbb8f0038b1e12058cdaebb085d2751385c0750fa01405a80174'
            '10a802740cb80100c380261405fd31c0c3b80100800e140502c3455589e5535152b868208cca0ee8b4249089'
            'c389d1e8c0ff85c0750885c9750a85db75065a595b5d4dcb89d889cae8b4feebf15351565789c189c3bf6400'
            '81c36b0731d289d8f7f789ded1eed1ee29c631d289d8bb9001f7f3bb6d0101c631d289c831c981eecc010ee8'
            'e2239031db01f011da5f5e595bc35589e5565783ec02505289de89cf8b56048b460685c9751f81fbc0a87319'
            '85c07f06751385d2760f81c6805183d7018346f8ff8356f6ffbb8051b9010029d619c789f089fa0e3ee82325'
            'bb8051b901000146f81156f689f089fa0ee80f259089df89ce89d889cabb100e31c90ee8fd2490c45e0889f2'
            '31c926894704bb100e89f80e3ee8e72489df89ce89d889cabb3c0031c90e3ee8d5248b5e0889f231c9268947'
            '02bb3c0089f88b76080e3ee8bd248b46f88b56f631c926891cbb6d010ee8ab24908b4ef889c389c6e8eefe81'
            'c36b0729c185c97d03e98100c45e08bf804bc746faffff2689770a8d846c0726894f0ee82e2385c07408bf9a'
            '4bc746faffff89c8bb1f0099f7fb89c3d1e38e46fa01fb263b4f027c0140c45e0826894708d1e08e46fa01c7'
            '262b0d8e460a418b46f826894f06bb07000501008b56f683d20031c98b76080e3ee8232489f08cc226895c0c'
            '8d66fc5f5e5dc2080089d84ee8c122056d014b01c1e969ff455589e55689c68ec15126c74710000031c0538e'
            'c250268b1c268b4c0231d250b8df63e870fe5e5d4dcb455589e55351bbaa058cd90ee8c7ff595b5d4dcb5589'
            'e551565783ec1489c68956f889da8e46f8268b441085c074153d0100750d268b440e488d66fa5f5e595dc3e9'
            'ad008d876c07e8432285c07503e98f00bb9a4bb8ffff8e46f8268b7c08d1e78ec001fb268b7f0231c0262b3f'
            'c746ec01008e46f88946e68946e88946ea268b44088946ee8956f08d46e68cd2c746f600000e3ee8fdf58e46'
            'f8268b440c2b46f2050700bb070099f7fb268b44063d0500753a89c34bb10389d8d3e089d129d84101c839f8'
            '7e22268b44062d0200b10389c3d3e30356f429c389d001d8e958ffbb804bb8ffffe96eff89d8ebe148ebde26'
            '8b440ee941ff5589e5565783ec0289c789de894efa8b4e048ec226837d100075288e46fa26837c1000751e8e'
            'c2268b45088e46fa263b44087f268ec2268b45088e46fa263b44087c2389cb89f8e8d2fe8b56fa89c789cb89'
            'f0e8c6fe39c77e0cb801008d66fc5f5e5dc2020031c0ebf35589e55351565783ec1c89c68956f68ec2268b44'
            '1085c07c0589c7e9b90031ff833e1005007503e9a900bbf003b8de038cd926ff740a8cdae85bff8946ea85c0'
            '7503e99d00c746eef003c746f0de038c5ef28c5ef48e46f6268b44088946ec268b440a056c07e8c32085c075'
            '03e97f00bb9a4bb8ffff8b56ecd1e28ec001d3268b4f02262b0f8e46f28b5eeec746de0000268b4710894ee0'
            '85c0755c8b46ec263b47087e5cbf0100837ede0074158b5eee8b4ef28b56f689f0e8180285c0753bbf010085'
            'ff75378b46ea85c0740289c78e46f626897c1089f88d66f85f5e595b5dc3c746eede03c746f0f003e960ffbb'
            '804bb8ffffe97effe99100e9c900e9cb0075a58e46f6268b440c8e46f2050700262b470cbb070099f7fb8e46'
            'f6268b440629d08b5eee8946e2268b440c8e46f2050600262b470cbb070099f7fb8e46f6268b4406488b5eee'
            '29d08e46f28946e6268b47063d0500751983e9073b4ee27c03e944ffbf01003b4ee67c03e93fffe936ff48b1'
            '0389c2d3e229c2423b56e27fe0bf01003b56e67fe3e91cff268b570e3d0100751f8e46f6268b440a056c07e8'
            '8a1f85c0740db8ffff8ec0263b16844b7e01424a8e46f6268b440e39c27fa2bf010074a8e9e1fe31ffe9f7fe'
            '8e46f48b5ef0268b4710c746dc000085c0750d8b46ec263b47087e0731ffeb6be9aa0075668e46f6268b440c'
            '8e46f4050700262b470cbb070099f7fb8e46f6268b440629d08b5ef08946e4268b440c8e46f4050600262b47'
            '0cbb070099f7fb8e46f6268b4406488b5ef029d08e46f48946e8268b470631ff3d0500753a8b46e02d07003b'
            '46e47c28bf0100837edc0074108b5ef08b4ef48b56f689f0e8690089c78b46ea85c07503e959fe29f8e952fe'
            '3b46e87ddcebd448b10389c2d3e229c2423b56e47fc23b56e87fc6ebbe268b570e3d0100751f8e46f6268b44'
            '0a056c07e87d1e85c0740db8ffff8ec0263b16844b7e01424a8e46f6268b440e39c27f8b31ff39c2748beb83'
            '565789c689d78ec2268b54048ec131c0263b57047c368ec7268b54048ec1263b5704752b8ec7268b54028ec1'
            '263b57027c1a8ec7268b54028ec1263b5702750f8ec7268b348ec1263b377d03b801005f5ec351565789c689'
            'd188d88ec2268b1c8d5701268b7c022689148ec72688078ec126ff440c5f5e59c3455589e5565789c689d7b8'
            'ae2d50ff7608ff760689f00e3ee892108ec701c626c604005f5e5d4dca040000c3c3535289c3833e92030074'
            '04ff169203ff163a05baff00b81000e8a800ff163a05ff163c0589d8e916e35289c2ff163a05ff163c0589d0'
            'e906e35589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c07504'
            '85d274df1eff1f1febd95589e55351525689c21ee8f2e3b97605bb5e0589ce88d081fb76057316803f027505'
            '83c306ebf03a470172f689de8a4701ebef81fe760574158d4402803c007508e889ffc60402ebc7e89affebf6'
            '1f8d66f85e5a595b5dc35589e553515689c389d11ee899e388deb98805bb760589ce88f081fb88057316803f'
            '02750583c306ebf03a470177f689de8a4701ebef81fe8805741c3a54017305c60402ebcd8d4402803c007505'
            'e824ffebeee838ffebe91f8d66fa5eeb9d455589e5803e3e0500750531c05d4dcb5250cceb0657564944454f'
            'b8010083c4045d4dcb455589e55789c788d88ec2578ae0d1e9f3ab13c9f3aa5f89f85f5d4dcb5589e5535152'
            '565783ec02be0a0280263902f88cd9800e3902048ec1268b440a85c07510a3a205a3a4058d66f65f5e5a595b'
            '5dc3b812000e3ee8321985c074618c5ef489c78e46f48b169e0526897504a1a00526894d06893e9e05268915'
            '8c06a005268945028cc08ec126897c0626894408268b7c068ec026c74508000026c7450a00008ec126c47c06'
            '26c64510008ec126c47c0683c61226c7450c0000e979ffb812000ee8daed9089c78956f485d2759385c0758f'
            'bb0100b8ce008cdae9a0e25589e5535152565783ec0689c3b103d3e08b16a00501d831ffd1e031c9050a028b'
            '1e9e058946f085d2750485db74478ec2268b07268b7704268b57068946f2268b47028ec28946f4268a440bbb'
            '0100a840741089f0e87ad74709c18b5ef28b56f4ebc4a80875ec3b76f072ef81fe640273e131dbebdd85c974'
            '0db8ffff8d66f65f5e5a595b5dc389f8ebf2455589e5b80500e86bff5d4dcb31c0e863ffe9d3e2455589e552'
            '5657508ec1268b4708268b77068ec026837c0c01741026837c0c007403e9a00026c7440c01008ec126f6470a'
            '027503e99a0026c47706268b4408268b740a85f6750b85c0750789d889cae89600be0004837ef80a75368ec1'
            'be000626f6470a40752a26804f0b1026c43f26c6050d8ec126ff0726ff4704268b4704263b470e750b89d889'
            'cae858e585c075348ec126804f0b1026c43f8a46f82688058ec126ff0726ff47042685770a750a268b470426'
            '3b470e752789d889cae824e585c0741cb8ffff8d66fa5f5e5a5d4dcbc7069c050400b8ffff26804f0a20ebe7'
            '8a46f830e4ebe053515689c389d1e83be18ec126837f0e007511268a470ba8027503e9790026c7470e86008e'
            'c1268b470e0e3ee80aec8ec126c47706268944082689540a8ec126c47706268b440a268b540885c0755d85d2'
            '75598ec12680670bf826804f0b0426c477068d471026894c0a268944088ec126c7470e01008ec126c4770626'
            '8b4408268b540a8ec126c747040000268907268957025e595bc3a804740826c7470e4000eb8126c7470e0002'
            'e978ff8ec126804f0a08ebc15689c68ec126c7470400008ec2268a043c2d740e3c2375138ec126804f040146'
            'ebe98ec126804f0408ebf43c2b750e8ec126804f040426806704fdebe23c2075108ec126f647040475d52680'
            '4f0402ebce3c3075088ec12688471bebc289f05ec35589e5565783ec068b7e0489c68956fa895ef6894ef88e'
            '460689fb8cc126c6451b20e87aff8e460689c389c626c7450800008ec28956fa26803f2a74328e46fa268a04'
            '3c307c443c397f408e4606ba0a00268b4508f7ea8e46fa89c2268a04982d30008e460601c24626895508ebce'
            'c45ef62683070226c40789c3268b47fe8e46062689450885c07c58468e460626c7450affff8e46fa26803c2e'
            '756c8e46064626c7450a00008e46fa26803c2a743d8e46fa268a043c307c4f3c397f4b8e4606ba0a00268b45'
            '0af7ea8e46fa89c2268a04982d30008e460601c2462689550aebce26f75d0826804d0408eb9dc45ef6268307'
            '0226c40789c3268b47fe8e46062689450a85c07c27468e46fa268a048d54013c6873213c4c735b3c49755926'
            '807c0136754226807c0234753be99d0026c7450affffebd18d5c02771326807c016875568e460689de26804d'
            '0410eb183c7473263c6c7537263a440175268e460689de26804d04808b56fa89f08d66fc5f5e5dc20400eb24'
            'eb3876593c7a74553c7775e48e46064626804d0440ebd93c6a75d58e460689d6ebc9eb2576393c5774143c4e'
            '75c28e460689d626804d0502ebb63c4675b28e46064626804d0504eba78e460689d626804d0420eb9b8e4606'
            '83c603eb8eeb0c8e460689d626804d0501eb8546eb82565789c689d789da8ec731c039d0740c89f34626803f'
            '00740340ebf05f5ec35589e551565783ec0489df89c68956f831c983fbff752c8e46f8268b0485c0741889c3'
            '8cd28d46f683c6020ee88c18903dffff74e201c1ebde89c88d66fa5f5e595dc38e46f8268b0485c0741c39f9'
            '7f1889c38cd28d46f683c6020ee85c18903dffff74de01c1ebda39f97ecc89f8ebca5589e5565783ec0c5389'
            'cf8956faba10008d5ef08cd10ee8bf18908d46f08cd28b5eee0e3ee8fbe789c68b46fa8ec701c389f283c6ff'
            '85d27e1489c283c3ff05ffff85d27e088a52f0268817ebe38b5eee8ec701c389c283c3ff05ffff85d27e0626'
            'c60730ebee8b5eee8ec7035efa26c607008d66fc5f5e5dc35589e5565783ec0a89c68956f6895ef2894ef485'
            'c97c39c45e0426837f0aff750626c7470a0400ba0a008b4ef68b46f489f30ee82918908976f88b46f68e46f6'
            '8946fa8d440126803c00741789c6ebf38ec2f75ef426c6042df75ef2835ef40046ebb4c45e0426837f0a0074'
            '448e46f631ff26c6042e89c6c45e04263b7f0a7d298b46f2bb0a0031d231c9c746f400000ee82015908956f4'
            '8946f28a46f48e46f604304726880446ebce8e46f626c60400f646f3807503e9c3fd8b46f63b46fa75053b76'
            'f8741d83c6ff8ec026803c2e750383c6ff8e46f626803c39754b26c60430ebd98ec08b46f626c60431468ec0'
            '8d7c0126803c30750489feebf126803c2e751726c604308d750126c6052e8e46f626803c30750346ebf78e46'
            'f626c604304626c60400e954fd26fe04e94efd5389c38ec226f6470408752b26807f1b307524268b4708262b'
            '470e262b4710262b4712262b4714262b4716262b471885c07e04260147105bc35589e5565783ec06505289de'
            '894efa8e46fa26837c12007f098d66fc5f5e5dc202008e46f28b5ef48d46f68cd2268b1f8346f4020ee83016'
            '9089c13dffff74cf8e46fa263b44127f278d7ef68c56f84983f9ff74ba8e46f8268a05988b56fa89c389f0ff'
            '56048e46fa4726ff4c12ebdf26c744120000eba17b38603e343e603e7b386c3a7b38603e603e603e603e603e'
            '603e603e603efb3c603e603ecc3a603e603e603e603e6e3b603e603e603e603e603e603e603e603e7b382e3c'
            'c73dd33b7b386c3a7b38603ed33b603e603e603e603e603e353cfb3c603e603ecc3a603ed33b603e603e6e3b'
            '5589e5565783ec1a8b76048946f88956fa8e460626c7441800008946f0268b44182689441626894414268944'
            '12268944102689440e268a441a8956f23c6973503c6475478e4606268a4404a880743e8ec12683070826c43f'
            '268b45fe8946ee268b45fc8946ec268b45fa8946ea268b45f88946e8e92001ff76068b46f88b56fa56ff46f0'
            'ff1e4605e92602eb45e9bb0076b23c7875328e4606268a4404a88074398ec12683070826c43f268b45fe8946'
            'ee268b45fc8946ec268b45fa8946ea268b45f88946e8eb2d3c7574ca3c6f74c6eb233c6274c03c58ebf4a840'
            '74348ec12683070426c43f268b45fc8946f4268b45fe8946f68e4606268a441a2c41c746e60a003c37776130'
            'e489c7d1e72effa594378ec12683070226c43fc746f60000268b45fe8e46068946f426f64404207407c746f6'
            '0000ebbd26f644041074b68a46f430e48946f430c0eba7a840741c8ec12683070426c43f268b45fc8946f426'
            '8b45fe8946f6eb2fe9f1048ec12683070226c43f268b45fe998e46068946f4268a44048956f6a8207503e976'
            '008b46f4998946f48956f68e460630c026f6440480746a837eee007c10750a837eec007504837eea0084c074'
            '698e4606268b440e89c28b7ef84201c72689540e8e46fa26c6052d8e460626f6440480743831c02b46e88946'
            'e8b800001b46ea8946eab800001b46ec8946ecb800001b46ee8946eee9f2fea81074908a46f498eb83837ef6'
            '007ca6eba0f75ef6f75ef4835ef600e9d3fe8e4606268a4404a804741a268b440e89c28b7ef84201c7268954'
            '0e8e46fa26c6052be9aefea8027503e9a7fe268b440e89c28b7ef84201c72689540e8e46fa26c60520e98dfe'
            '8e460626f64404207503e902fe8ec126830704ff760626c43f8b56fa268b45fc568946f4268b45fe8b5ef489'
            '46f689c18b46f8e812fbbbffff8b46f88b56fae8fcf98e4606268944128b560689f0e826fc8b46f08b56f28d'
            '66fc5f5e5dc20800c47ef826c605008e4606268a4405a804751da80274198ec12683070226c43f268b5dfe85'
            'db74278c5ef2895ef0eb1f8ec12683070426c43f268b45fe268b5dfc85c0750485db7406895ef08946f28e46'
            '0626807c1a53754126f6440420742b8b46f08b56f2268b5c0ae872f98e46062689441226837c0a007d03e96c'
            'ff263b440a7ef7268b440ae95bff8b46f08b56f2268b5c0ae866f9ebd326f644044075eaebbd8e4606268a44'
            '04a8017455a8807503e9a500837eee007512837eec00750c837eea007506837ee8007436268b440e89c28b5e'
            'f84201c32689540e8e46fa26c607308e4606268b440e89c28b5ef84201c32689540e8b56fa268a441a8ec226'
            '8807c746e610008e460626837c0aff740526c6441b208e46068b5ef88b46fa26035c0e8946f2895ef026f644'
            '0480744026837c0a00753c837eee007536837eec007530837eea00752a837ee80075248ec026c6070031c0eb'
            '5d8b46f60b46f4e968ffc746e60200eb9ec746e60800eb97e97700ff76e68e46068b46f88b5eec8b4eeaff76'
            'fa2603440e8b56e8508b46ee0ee8ec11908e460626807c1a58750b8b46f88b56fa0e3ee82213bbffff8b46f0'
            '8b56f2e828f88e460626894412263b440a7c5d26f64404017503e91cfe837ee60a7df785c07409c45ef02680'
            '3f3074ea8e460626ff4410e9fffd26837c0a0075118b46f60b46f475098e46f226c60700ebb4ff76e68e4606'
            '8b5ef88b4efa8b46f48b56f626035c0e0ee8d41290e975ff268b440a262b441226894410e9bafd8e460626f6'
            '4405067503e9a3008e460626837c0800751026f64405027503e9970026c7440804008e460626806404f98ec1'
            '26830702268b5702268b3f8ec2268b45fe8e46068946e426f644050474728ec183c702268957028b4efa2689'
            '3fba0400268e47028b5ef8268b45fee8d0f7c45ef8ba04008b46e483c3058cc126c647ff3ae8baf78e460626'
            '807c1a50750b8b46f88b56fa0ee8041290bbffff8b46f08b56f2e809f78e46062689440ee90afd26804c0504'
            'e955ff26c744080900e966ffba04008b5ef88b4efaebb626c74412010026f644044075198ec12683070226c4'
            '3f8b5ef8268a45fe8e46fa268807e9c8fc8ec1268307028d46e226c43f8cd2268b5dfe0e3ee8800f3dffff75'
            '03e9a9fcc45ef88a46e226880730e40e3ee8441285c074e98e46fa8a46e3268847018e460626ff440ee981fc'
            '8ec1268307028b46f826c43f8b56fa268b5dfe0e3ee8380f3dffff7403e95afc8e460626c744120000e955fc'
            '8e460626c7440800008b5ef8268a441a8e46fa2688078e460626c7440e0100e933fc455589e5565783ec708b'
            '7e0ac646fa00c746d80000c746dac800c746e000008946d48956d6895ef4894ef88e46f88b5ef4268a0784c0'
            '74168d57013c257412988956f489c38cd28d46d4ffd7ebdde9fa018d5ed4168cd189d0538cc28d5e06e8e1f3'
            '89c38ec28956f8268a07438846ee895ef484c074d73c6e75678b76068b460883c604f646d904741e8ec08976'
            '06268b5cfc268b74fe8a46d8a81074218ec68a46e0268807eb83f646d90274dc834606028ec08b5e068cde26'
            '8b5ffeebd8a820740b8ec68b46e0268907e95dffa84074128b46e08ec69926890726895702e949ffeb21a880'
            '74db8b46e08ec699268907b10f2689570226d3fa2689570426895706e926ff8d56fa8d5e06168d468c528d56'
            'd4168cd1528cd2e866f889c68b46e20346e40346e60346e80346ea0346ec8956f62946dcf646d80874248d4e'
            '8c8c56f2837ee2007e338e46f289cb268a07988cd289c38d46d441ffd7ff4ee2ebe2807eef2075d6837edc00'
            '7ed0bb20008d46d48cd2ffd7ff4edcebeb837ee4007e0fbb30008d46d48cd2ffd7ff4ee4ebeb8a46ee3c7375'
            '45f646d840751c837ee6007e248e46f6268a04988cd289c38d46d446ffd7ff4ee6ebe48d5ed48b56f6578cd1'
            '89f0e8d3f6837ee8007e2fbb30008d46d48cd2ffd7ff4ee8ebeb3c5374d9837ee6007ee18e46f6268a04988c'
            'd289c38d46d446ffd7ff4ee6ebe4837eea007e168e46f6268a04988cd289c38d46d446ffd7ff4eeaebe4837e'
            'ec007e0fbb30008d46d48cd2ffd7ff4eecebebf646d8087503e9f5fd837edc007ef7bb20008d46d48cd2ffd7'
            'ff4edcebeb8b46e08d66fc5f5e5d4dca06000000000000005156575589e583ec0289dec746fe000085c07503'
            'e97c0089c783c70383e7fe39c772f18ec2268b440a83ff067303bf060039c7775e8ec2268b4c08268b5c0639'
            'cf765c8ec2268b0739c7775b26894c0829f826ff440c3d0600726a89d901f926894c0689ce26890426893f89'
            'cf268b770226897502268b47042689450426894c0489c626894c028ec226800f0183c302895efe8b56fe89d0'
            '89ec5d5f5e59cb268b5c1431c9eb9c39c1730289c189f08ec2051000268b5f0439c375878b56fe26894c0aeb'
            'd126ff4c0e268b7f0226897c06268b77042689750426897c02eba8fc5156575589e583ec0689d1895efe85c0'
            '7503e99c0089c68ec283ee0226f6040174f0268b0489f324fe01c326f607017403e992002603078b7efe2689'
            '04263b5d067504268975068ec1268b7f02268b5f0426895d0426897f028b7efe26ff4d0e8ec1268b7f0289fa'
            '268b04260315897efc39d6754389fb2603072689078b5efe263b5706750426897f068b76fc8b5efe8ec126ff'
            '4f0c263b7706730a263b47087604268947088b5efe8ec1263b470a770a89ec5d5f5e59cbe9d2002689470a89'
            'ec5d5f5e59cb8b5efe268904268b5f0639de7203e97700263b770277838b5efe268b5f1439de7303e975ff8b'
            '5efe8ec1268b5f0e89d88b7efe4031d28946fa268b450cf776fa39d8731b268b550c29dad1e039d373538ec1'
            '268b1c8ec101f326833fff75498b5efe8ec1268b5f0639de72528ec139de72b0268b5f0439de72a8268b5f04'
            '39de72a0268b5f04ebe6268b5f0439de72928b7efe83c710268b450289fb39c67685e9f7feb8ffffeba826f6'
            '07017503e9e9fe268b1780e2fe01d34874a3eb9b8b5efe268b5f14eba58b7efe26ff450e8b7efc26895c0426'
            '897c022689750426897702e9e3fe455589e553525685c074558b36bc0585f6743439c677088b1401f239d072'
            '4389f38b740285f6740c39c677088b1401f239d0722e8b770485f6740c39c677088b1401f239d072538b3642'
            '0585f675178b36400585f6740939c676098b7404ebf3eb53eb36eb5539c677088b1401f239d0722889f38b74'
            '0285f6740c39c677088b1401f239d072138b770485f674c139c677bd8b1401f239d073b58cda89f30ee8b4fd'
            '908936bc053b3642057308a144053b440a72105e5a5b5d4dcb8b1401f239d072d7eb968b440aa34405ebe845'
            '5589e50e3ee84a0c5d4dcb5589e5565783ec0489c689df894ef88956fae862d6a8807544833e880300740e89'
            'f0ff16720385d2754c85c075488b4efa8e46f889fa89f31e8cc08ed8b440cd2119d21f85d27c2989c239c874'
            '06c7069c050c0089d08d66fc5f5e5dc3b00289f331d231c9b442cd21d1d2d1ca85d27da8e8d3d7ebe0ff76fa'
            '8b4ef889fbff168803ebd25589e5535152565783ec0289c78b1e9e05a1a00531c98946f48b46f485c0750485'
            'db742f8ec0268b5706268b77048ec226857c0a750e8e46f4268b37268b470289f3ebd24126f6440b1074ea89'
            'f0e894d1ebe389c88d66f65f5e5a595b5dc3455589e5b8ffffe897ff5d4dcb455589e552a15c01c7065c0100'
            '0085c07507833e8e030075045a5d4dcbb80100ff167203ff168e03ebef565789c689cf89d1833e860300740c'
            'ff16720385d2751d85c0751989da8ec789f31e8cc08ed8b43fcd2119d21f85d27c0d5f5ec35189f9ff168603'
            '5f5ec3e8ecd65f5ec3455589e50ee803005d4dcb455589e551565783ec0689c689d789d985d2750485c0745e'
            '85db74630ee8cf0a908946f48cd839c775608976f689da89f08c5ef80ee8c90a9085c0755c89c80e3ee803d8'
            '8946f68956f885d2750485c07455ff76f489f389f90e3ee8ce0a89f089fa0ee857d0908b46f68b56f88d66fa'
            '5f5e595d4dcb89d80ee8cbd790ebee0e3ee839d031c031d2ebe389f089fa0ee8cc0a908946f68956f8837ef8'
            '0075c8837ef6007498ebc08cd839c7750c8b56f489f00ee84b0a90ebae8b5ef489f089fa0ee89a0a90eba045'
            '5589e5565789c78b460689de8ec21e57918ed8d1e9f3a513c9f3a45f1f89f85f5e5d4dca020053523d05007d'
            '1589c28b1ebe03d1e28e06c00301d326f647014074035a5bc326804f01400ee8a5c79085c074efc41ebe0301'
            'd326804f01205a5bc3455589e55351565783ec0289c6833e4e05007503e971008ec031db268b078946f685c0'
            '745583c20283fa027303baffffa1500539c2730289c289d7b10483c70fd3ef85ff7503bf0010b1048b46f6d3'
            'e801c781ff0010760a803e5a0101741bbf00103dfe0f732581ff0010751fbffe0f569affff000085c0751a31'
            'f689f08d66f85f5e595b5d4dcb31c6ebf081fffe0f76deebe65031d289f8b90400d1e0d1d2e2fa5250ff3656'
            '059affff000085c074c9b10489f88ec631dbd3e0268907268b5f128b7ef6268b1783ef0401da39fa74202689'
            '5d02268b57042689550426897f0431db31d226897f1289fb89d726ff450e89c229da8ec683ea0431ff268917'
            '268b550a263b177307268b172689550a89c38ec683eb0426c747020000be010026c707ffffe955ff455589e5'
            '5351525657833e4e050074413de0ff773c051c003b0650057303a1500589c6b10483c60fd3ee85f67503be00'
            '1089f031d2ff365205b90400d1e0d1d2e2fa52509affff000089c785c0750ceb0231c05f5e5a595b5d4dcb50'
            '9affff000085d2750485c074638ec231ff26c74506160026c7450e010026c74512160026c74514160026897d'
            '0226897d0426897d08b10426897d0cd3e626897d102689358d84e6ff2689450abf160026c74502100026c745'
            '04100083ee0426890526c704ffff89d026c744020000eb87579affff0000e97cff31c0cb455589e553515256'
            '5783ec0489c785c074543deaff774f05030024fe8946f23d0600724fc646f40031d28b46f23b06440576518b'
            '36420585f6743f85f674568b4c0a8936420539f972118cda89f389f80ee8b8f79089c285c075643b0e440577'
            '2e8b7404ebd531c08d66f65f5e5a595b5d4dcbc746f20600ebaa893644058b364005ebb731c08b364005a344'
            '05ebac890e4405ebcc807ef400741289f80e3ee8dc0785c07419c646f400e97dff89f80e3ee8c70885c074e3'
            'c646f401e96bff89d0eba55589e5565783ec0c505253518ec289c3268b4708bb0c0099f7fbc746f4804b8b7e'
            'eec746f6ffff268b7d0a89d681fffdf47c178b5eee268b4708bb0c0099f7fb01c785f67c1285ff7d14b8ffff'
            '89c28d66fc5f5e5dc204004f83c60cebe48d856c07e8b80185c07403e9e0008d450399b102d3e21bc2d3f8bb'
            '6d019931c98946f289f88956f0990ee84601908b4ef201c18b46f011d08946f28d4563bb640099f7fb9929c1'
            '8b46f219d08946f28d852b01bb900199f7fb9989f301c18b7ef211d7d1e38e46f6035ef48b76ee268b078e46'
            'ec9989fb01c1268b440611d39901c811da89f305ffff83d2ff8946f88956fa268b470431c9bb3c00998b7eee'
            '0ee8d0009089c6268b450289d39901f011dabb3c0031c90e3ee8b8008b5eee89c7268b0789d69901c711d685'
            'f67f02752b83fe017c37750681ff8051722f8346f8018356fa0081c780ae83d6feebe2c746f49a4bc746f6ff'
            'ffe913ff8346f8ff8356faff81c7805183d601ebbe8b46fa85c07d03e9d6fe7507817ef8de6372f4837ee800'
            '7506837eea0074138e46e88b5eea8b46f82689078b46fa26894702837e06007506837e0400740ac45e042689'
            '3f268977028b46f8bb8051b9010005219c8b56fa83d2ff0e3ee8080001f811f2e97ffe009350920bc07402f7'
            'e2910bc07404f7e303c858f7e303d1cb00001f003b005a0078009700b500d400f300110130014e016d010000'
            '1f003c005b0079009800b600d500f400120131014f016e0153515289c3a803751db9640031d2f7f185d2750b'
            '89d8bb9001f7f385d27507b801005a595bc331c0ebf8455589e55351565783ec0650528b1ec406a1c606895e'
            'f48946f685c0750485db743885d27506837ef000742ec476f48b5ef4268b4c02268b1f85c9750485db74198b'
            '76f08b7eee89d889ca0e3ee8520685c074088346f404ebd2eb7089f089fa0ee83e069085c0754289d889ca0e'
            '3ee864060ee88d06908946f289fa89f00ee85406908b56f20ee879069029c275c589d889ca0e3ee88f0689d1'
            '89c389f089fa0ee883069089d789c6eb9c89d889ca0e3ee822063d3d00759b89d889ca0e3ee865068d66f85f'
            '5e595b5d4dcb31c031d2ebf00bd278180bc97947f7d9f7db83d9000e3ee83b00f7daf7d883da00cbf7daf7d8'
            '83da000bc97914f7d9f7db83d9000ee81d0090f7d9f7db83d900cb0e3ee80f00f7d9f7db83d900f7daf7d883'
            'da00cb0bc9751a4b7416433bda77098bc88bc22bd2f7f391f7f38bda8bd12bc9cb3bca721a75103bd8770c2b'
            'c38bd82bc92bd2b80100cb2bc92bdb9387cacb55562bf68bee03db13c97211453bca72f577043bd876eff813'
            'f64d7820d1d9d1db2bc319caf572f003f64d780cd1e9d1db03c313d173f1ebdf03c313d18bd88bca8bc633d2'
            '5e5dcb005689c685d2750485c07413f6c7ff74128ec226883cb8020026885c015ecb31c05ecb8ec2b8010026'
            '881c5ecb455589e5565783ec28535189d1c646d4008d76d589df8c56fa8b5ed08e46fa895ef88d5ef6894ef6'
            '33d236f73736890789d38a8726012688048b46f64685c075e18e46fa83c6ff268a048e46f82688054784c075'
            'ec8b46d28b56d08d66fc5f5e5d4dcb455589e5565789de89cf83fa0a750485c07c0a0ee886ff89f089faebde'
            '8ec1f7d84326c6042debeb455589e55352bb0100b8fc008cdae97bc4455589e5565783ec4c508b7606c646b0'
            '008d7eb18c56f88976f68b7608897ef48976fa8b760a8b46ae8976f28d76f28bfa85c0742d8bd08bc333db36'
            '3b14720a8bd88bc233d236f7349336f7349136f7349736f7343689148bd08bc38bd98bcfeb2e85db74158bd3'
            '8bc133db33c9363b1472db8bc88bc233d2ebcf8bd18bc733c933ff363b1472ca8bf88bc233d2ebbe8b7ef289'
            'c68946ae8a8526018e46f88b7ef4ff46f426880585f67403e974ff85db75f985c975f585d275f18346f4ff8e'
            '46f88b5ef4268a178e46fa8b5ef6ff46f626881784d275e38b46068b56088d66fc5f5e5d4dca0600455589e5'
            '565783ec048b76068b7e088976f8897efa837e0a0a750c85c07c1c750685db750285c9ff760aff76faff76f8'
            '0ee8e4fe8b560889f0ebbb8ec731ff29d789fabf000019cf89f9bf000019df89fbbf000019c789f88d7c0126'
            'c6042d897ef8ebc3535189c189c38ec2268a0784c074102c613c19760343ebf00441268807ebf689c8595bcb'
            '455589e5565783ec285351c646d4008d76d589df8c56fa894ef88e46fa8b5e06895ef68d5ef633c9363b1772'
            '079287d136f7379136f7373689178bd18b5ef68a9f260126881c4685d275d285c075ce8e46fa83c6ff268a04'
            '8e46f82688054784c075ec8b46d28b56d08d66fc5f5e5d4dca0200455589e5565789de89cf837e060a750485'
            'd27c0dff76060ee872ff89f089faebd88ec1f7da26c6042df7d883da0043ebe35389c38a9fbf0580e30130e7'
            '89d85bcb31c00ee8180390c3455589e55389c3b43ecd21e8f8c95b5d4dcb455589e55389c3b468f8cd21e8e5'
            'c9ebeb5389c38ec283eb02268b0724fe2d02005bcb455589e5535183ec0289c18d5efa168cd85389d389ca0e'
            '3ee83a0385c0740231c989c88d66fc595b5d4dcb455589e5565789c78b460689de8ec21e57918ed8d1e9f3a5'
            '13c9f3a45f1f89f85f5e5d4dca0200455589e50ee803005d4dcb455589e5515689c189d68cd839c2751489da'
            '89c80ee88bff9085c0751931d25e595d4dcb89ca89f00ee87404903dffff750431c0ebe789c889f2ebe35289'
            'c2a14e0589164e055acb31c0cb53565789c68b1e400531ff85db740e39de720789df8b5f04ebf1897702897c'
            '02895c0485ff7439897504c744100000c744080000c7440c00008d4410c7440e00008944128b1c89441483eb'
            '16894406895c1601f38d4416c74716ffff5f5e5bc389364005ebc45389c38ec2268b1789d005030024fe39d0'
            '722089c2051e0039d072173b0650057305a1500524fe26890785c07403b801005bc331c05bc3535189c38ec2'
            '31c05026ff379affff0000595bc35589e5535256500e3ee808048d46f88cd2e8d8ff89c385c075088d66fa5e'
            '5a5b5dc38b46f82d0200890789d8e820ff89c6800c01c7470a0000050200ff470c0e3ee820f1b80100ebd145'
            '5589e55250a14e0585c07409833e5001fe750931c08d66fe5a5d4dcb8d46fc8cd2e847ff85c074ed8b46fce8'
            '88ffebe5455589e55389c38ec2268a0784c07507b801005b5d4dcb30e40e3ee8c2fd85c0740e8ec226807f01'
            '007505b80200ebe331c0ebdf455589e5535189c389d18ec2268a0730e489c20e3ee894fd85c0740a8ec188d6'
            '268a470188c289d0595b5d4dcb455589e5f6c4ff7413813e5a05a40374035d4dcb0e3ee8c3045d4dcb0e3ee8'
            'c9045d4dcb455589e55389c38ec2268a0730e40e3ee848fd85c074108ec226807f010074078d47025b5d4dcb'
            '8d4701ebf7535152b901018cda31c031dba35c05a35a05b8be050ee8ffdb905a595bc35589e553515250c746'
            'f80000eb0aff46f8817ef80001731f8a46f830e4509affff000085c074e78b5ef8c7065c050100c687bf0501'
            'ebd78d66fa5a595b5dc3455589e553515289c2e897ff83faff744683fafe744183fafd745183fafc752eb91f'
            '00bb0100b840068cda0e3ee88bdbb91d00bb0100b89f068cda0e3ee87bdbc7065c050100c7065a05a403eb1e'
            '85d27405b80100eb179affff00003d030a72f1e85dff9affff0000a35a0531c05a595b5d4dcb455589e55156'
            '5783ec0a89d68d570380e2fe39da7203e96d00baffff83ee028ec0268b1c80e3fe39da76658e460889f729da'
            '01df8b5e06897ef82689178b5ef88ec026833fff744b26f60701754b8b7ef8268b0d268b7d04897ef68b7ef8'
            '8cda268b7d0231db897ef439d075388b1e40058ec026837f0400742b39f3761e8ec0268b5f04ebeb83fa0673'
            '91ba0600eb8ce9ba00b80200e9a400e9ac00268b1701da39d673d98ec0268b57063b56f8750a89d7268b5502'
            '26895706c47e06263b0d732d8b7ef48ec08b56f62689550489d78b56f42689550226010c26ff4f0ec45e0626'
            '3b0f7355014ef826290fe94eff89ca262b158956f283fa0672c689fb268b170156f88ec08b56f28b5ef88b7e'
            'f426891726897f028b7ef626897f048b5ef48b7ef826897f048b5ef626897f02c45e0689c2268b078ec22601'
            '0431c08d66fa5f5e595d4dca0400b80100ebf089d929d183f90672e589d783cf0126893c89f780c90101d731'
            'db897ef08cda26890d39d075228b1e40058ec026837f0400741539f376088ec0268b5f04ebeb268b1701da39'
            'd673ef8b56f08ec083c20226ff470c0e3ee83102eb93455589e551565783ec0489c189d689df8066f8fe8d56'
            'f61689fb89c85289f20e3ee834fe85c0741c3d0100740b3d020075e2f646f8017410b8ffff8d66fa5f5e595d'
            '4dcb89f0ebf38b56f689c80e3ee8d9f085c074e2804ef801ebb8535152509affff000085c07403b8ffff5a59'
            '5bc3455589e5535152568b36400585f6742a8b048b5c122d16008b4c043b07740489ceebe989f08b5402e8c1'
            'ff85c075f089cb89f00e3ee8b101ebe531c05e5a595b5d4dcb455589e50ee8b5ff5d4dcb5589e55351525657'
            '83ec0a8b16c606a1c40685d2750485c0740a8d66f65f5e5a595b5dc39affff000089c68956ee89c38ec231c9'
            '30c0263a07740a43263a0775fa4143ebf189d829f07503b801000ee82ec7908956f089c38956ec85d2750485'
            'c074b789c8d1e0d1e001c80504000ee80ec79089c78956f285d2750485c07468a3c4068b46ee8946f489d889'
            '16c606a3cc068b46f08b56eca3ce068e46f48d450426803c0074208e46f226891d2689550289c78e46f4268a'
            '048e46ec462688074384c075eeebd48e46f231dba3c0068cc226c70500008c06c20626c7450200000ee8fdd7'
            '90e932ff8b56f089d80e3ee804bfe925ff520ee8e700908b16c606a1c40685d2753185c0752d8b16ca06a1c8'
            '0685d2750485c0740d0e3ee8d8be31c0a3c806a3ca068b16ce06a1cc0685d2751585c075115ac30e3ee8babe'
            '31c0a3c406a3c606ebc40ee8acbe9031c0a3cc06a3ce065ac33d818272083d9a8277032d2100cb3d61007c08'
            '3d7a007f032d2000cb455589e5535189c189d083faff740b8cda39d1750a0ee878eb90595b5d4dcb89ca31db'
            '0ee8c0e990ebf05689d63b06400574323b06420575148936420585f6750c8b16400589364405891642053b06'
            'bc05750531c0a3bc0585f67403895c0485db75085ecb891e4005ebc88977025ecb455589e5535152565783ec'
            '04c746f40000a1c6068b16c40685c0750485d2744731f631c9a1c4068b16c60689c38ec201cb268b3f897ef2'
            '268b7f0285ff7506837ef2007424c43ec00601f726803d00750683c10446ebcd8ec2268b07268b57020e3ee8'
            'b0bdebeaeb2b0ee8a8bd90b804000ee82ac59089c3a3c4068916c60685d2755485c0755031c0c746f4ffffa3'
            'c006a3c2068b16ca06a1c80685d2750485c0744e31c9a1c8068b16ca0689c38ec201cb268b378976f2268b77'
            '0289d785f67506837ef200742b89f2268b070ee844bd9083c104ebce8ec226c707000083c3048916c20626c7'
            '47fe0000891ec006eba3eb230ee81ebd90b804000ee8a0c49089c3a3c8068916ca0685d2751785c07513c746'
            'f4ffff8b46f48d66f65f5e5a595b5d4dcb8ec226c707000026c747020000ebe32b0003014000010001000301'
            '8d000100010003011801010001000301ab01010001000301cc0102004a000301fe0101000100030113020300'
            '0500030124020300840005013002020066000201320202006600030149020100010003017202010001000301'
            '9b020100010003011503010001000301391102005b000301641102001e0003016b1101000500020085110100'
            '361103019611020003000301c811020031000301bc120100010002006c240100804b020075280100804b0200'
            '902801009a4b02007c2901009a4b02000b2a0100804b0200fc2a01009a4b02007b2b0100804b02002f2c0100'
            '804b02003c2d0100804b030147470200150003017e47020010000301414802000f0003015948020012000301'
            'ca48020011000200bc490100804b0200eb4a01009a4b0301ff510200050003016e530200cf000301e6530200'
            '03000301f3530300840003010b560200070003018d5602008300000000000500000000000000000000007700'
            '4f5746494c452e544d50004f5746494c4500666f70656e2077006c696e65206f6e650a6c696e652025640a00'
            '7200666f70656e2072000a004f5746494c452e5458540072656e616d65004f5746494c452e2a006f70656e00'
            '6f7766696c652e747874005c4f5746494c452e545854004f70656e46696c6500c0ff00416e7369546f4f656d'
            '2f444f533343616c6c0072656d6f7665006163636573730025737c256c647c25647c25737c256400636f6e00'
            '4f70656e20576174636f6d004e6f7420656e6f756768206d656d6f727920746f20616c6c6f63617465206669'
            '6c65207374727563747572657300466c6f6174696e672d706f696e7420737570706f7274206e6f74206c6f61'
            '646564000102040810204080303132333435363738396162636465666768696a6b6c6d6e6f70717273747576'
            '7778797a00000000ffff00000000000000000000000c00000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000001000000000000000000000100000000000000000000000000'
            '0000000002000100000000000000000000000000000002000200000000000000000000000000000003000300'
            '0000000000000000000000000000020004000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000001400010002000200030002000000000000000000000000000000'
            '00000000000000000000000000000000ffff00000000000901010b06040505050203ff0e0724060801ff0000'
            '0000000000000000020001000300000000000000000000000000010005000900000000000000000045535400'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000045445400000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000000000000000000000000000000000000000000000ffff0000ffff00005046'
            '00000100100e0100000000000000000000000000000000000000000000000000000000000000000000000000'
            '012e012e0000000000000000ffff0000ffff0000010000200200000002000000000000000020300300000020'
            '662f00000020705000000020685600000020bf3000000014d51b0000001f5d57000005000300be0302009603'
            '030004050200020403000805020083040300460501003b4e03004a0501003b4e'
        )),
    'OWRES': (
        # wcl -bt=windows -l=windows -ms owres.c; wrc owres.rc owres.exe, with
        # owres.rc (the images drawn by a script: a red and blue disc, 32x32 and
        # 16x16, 16 colours; a yellow diamond; a plus cursor, hot spot 15,15; a
        # 48x24 256-colour gradient; a 16x8 monochrome checker):
        #     APPICON ICON app.ico
        #     2 ICON star.ico
        #     HAND CURSOR plus.cur
        #     PIC BITMAP pic.bmp
        #     CHECK BITMAP check.bmp
        #
        # #include <windows.h>
        # #include <stdio.h>
        # #include <string.h>
        #
        # /* resources of its own: its icons (the class's, a second one drawn), its
        #  * cursor (the class's), bitmaps (colour and monochrome); text (ExtTextOut,
        #  * GetTextExtentPoint, TabbedTextOut, GetTextFace), bitmap bits, a
        #  * superclassed BUTTON (GetClassInfo), _hwrite/_hread; what each gave in
        #  * its window */
        # static HINSTANCE inst;
        # static char line1[96], line2[96];
        # static FARPROC button_proc;
        #
        # long FAR PASCAL MyButtonProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        # {
        #     return CallWindowProc(button_proc, h, msg, wp, lp);
        # }
        #
        # long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        # {
        #     switch (msg) {
        #     case WM_PAINT: {
        #         PAINTSTRUCT ps;
        #         HDC dc = BeginPaint(h, &ps), mem = CreateCompatibleDC(dc);
        #         HBITMAP pic = LoadBitmap(inst, "PIC"), check = LoadBitmap(inst, "CHECK");
        #         HBITMAP old;
        #         RECT r;
        #         SIZE sz;
        #         int tabs[1] = { 120 };
        #         char face[32];
        #         old = SelectObject(mem, pic);
        #         BitBlt(dc, 8, 8, 48, 24, mem, 0, 0, SRCCOPY);
        #         SelectObject(mem, check);
        #         BitBlt(dc, 64, 8, 16, 8, mem, 0, 0, SRCCOPY);
        #         SelectObject(mem, old);
        #         DeleteObject(pic);
        #         DeleteObject(check);
        #         DeleteDC(mem);
        #         DrawIcon(dc, 96, 4, LoadIcon(inst, MAKEINTRESOURCE(2)));
        #         DrawIcon(dc, 136, 4, LoadIcon(inst, "APPICON"));
        #         SetRect(&r, 8, 40, 200, 58);
        #         SetBkColor(dc, RGB(255, 255, 0));
        #         ExtTextOut(dc, 10, 41, ETO_OPAQUE, &r, "ExtTextOut", 10, NULL);
        #         SetBkColor(dc, RGB(255, 255, 255));
        #         GetTextExtentPoint(dc, "Extent", 6, &sz);
        #         GetTextFace(dc, sizeof(face), face);
        #         TabbedTextOut(dc, 8, 62, "tab\there", 8, 1, tabs, 8);
        #         TextOut(dc, 8, 82, line1, strlen(line1));
        #         sprintf(line2, "extent %dx%d, face %s", sz.cx, sz.cy, face);
        #         TextOut(dc, 8, 100, line2, strlen(line2));
        #         EndPaint(h, &ps);
        #         return 0;
        #     }
        #     case WM_DESTROY:
        #         PostQuitMessage(0);
        #         return 0;
        #     }
        #     return DefWindowProc(h, msg, wp, lp);
        # }
        #
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     WNDCLASS wc, got;
        #     MSG msg;
        #     HWND w;
        #     HBITMAP bm;
        #     BYTE bits[4] = { 0xf0, 0x00, 0x0f, 0x00 }, back[4];
        #     HFILE f;
        #     char buf[8];
        #     long n;
        #     inst = hInstance;
        #     memset(&wc, 0, sizeof(wc));
        #     wc.lpfnWndProc = WndProc;
        #     wc.hInstance = hInstance;
        #     wc.hIcon = LoadIcon(hInstance, "APPICON");
        #     wc.hCursor = LoadCursor(hInstance, "HAND");
        #     wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #     wc.lpszClassName = "OwRes";
        #     if (!RegisterClass(&wc))
        #         return 1;
        #     /* GetClassInfo: its own, and BUTTON superclassed */
        #     GetClassInfo(hInstance, "OwRes", &got);
        #     GetClassInfo(NULL, "BUTTON", &wc);
        #     button_proc = (FARPROC)wc.lpfnWndProc;
        #     wc.lpfnWndProc = MyButtonProc;
        #     wc.hInstance = hInstance;
        #     wc.lpszClassName = "MyButton";
        #     RegisterClass(&wc);
        #     /* bitmap bits */
        #     bm = CreateBitmap(16, 2, 1, 1, bits);
        #     memset(back, 0, sizeof(back));
        #     GetBitmapBits(bm, 4, back);
        #     DeleteObject(bm);
        #     /* _hwrite, _hread */
        #     f = _lcreat("OWRES.TMP", 0);
        #     _hwrite(f, "huge io", 7);
        #     _lclose(f);
        #     f = _lopen("OWRES.TMP", OF_READ);
        #     n = _hread(f, buf, 7);
        #     _lclose(f);
        #     buf[n > 0 ? n : 0] = 0;
        #     OpenFile("OWRES.TMP", (OFSTRUCT FAR *)back, OF_DELETE);
        #     sprintf(line1, "own %d, bits %02x%02x%02x%02x, %s", got.lpfnWndProc == (WNDPROC)WndProc,
        #             back[0], back[1], back[2], back[3], buf);
        #     w = CreateWindow("OwRes", "Resources", WS_OVERLAPPEDWINDOW, 40, 60, 320, 200,
        #                      NULL, NULL, hInstance, NULL);
        #     CreateWindow("MyButton", "Superclassed", WS_CHILD | WS_VISIBLE, 8, 124, 120, 24, w,
        #                  (HMENU)1, hInstance, NULL);
        #     ShowWindow(w, nCmdShow);
        #     UpdateWindow(w);
        #     while (GetMessage(&msg, NULL, 0, 0)) {
        #         TranslateMessage(&msg);
        #         DispatchMessage(&msg);
        #     }
        #     return msg.wParam;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '2a0102000000000002030200000400205205010000000200020004000d0040005000ff0008011001ac010000'
            '0000010009000208dd007f1000000a03dd00b216400db216530d0a02410ce003010003800300000000005c11'
            '74011010018000000000d012940010100280000000006413740110100380000000000e80020000000000d814'
            '11003010980000000000e9140a0030100280000000000180010000000000f3149a0010100480000000000c80'
            '0100000000008d150a003010a600000000000280020000000000600ed4023000ab0000000000341128003000'
            'a0000000000000000741505049434f4e05434845434b0448414e4403504943054f5752455300000001000600'
            '0a00110000045553455203474449064b45524e454c084b4559424f4152440000096f777265732e6578650000'
            '0000ccebfd90909090001e5890455589e51e8ed8b81600e8c104ff36cc02ff36ca02ff760eff760cff760aff'
            '7608ff76069affff00001f5d4dca0a001e5890455589e51e8ed8b87600e88f04565783ec548b460c3d0f0074'
            '0b3d02007403e9f801e9eb01ff760e8d76a61e569affff000089c6509affff000089c7ff36ce021eb8100050'
            '9affff00008946f2ff36ce021eb81400509affff00008946f8a10e018946f657ff76f29affff00008946f456'
            'b808005050b8300050b81800505731c05050b8cc0050b82000509affff000057ff76f89affff000056b84000'
            '50b8080050b8100050b80800505731c05050b8cc0050b82000509affff000057ff76f49affff0000ff76f29a'
            'ffff0000ff76f89affff0000579affff000056b8600050b8040050ff36ce0231c050b80200509affff000050'
            '9affff000056b8880050b8040050ff36ce021eb81a00509affff0000509affff00008d7ee61e57b8080050b8'
            '280050b8c80050b83a00509affff00005631c050b8ffff509affff000056b80a0050b8290050b80200508d7e'
            'e61e571eb8220050b80a005031c050509affff000056b8ff0050b8ffff509affff0000561eb82d0050b80600'
            '508d7eee1e579affff000056b82000508d7ec61e579affff000056b8080050b83e00501eb8340050b8080050'
            'b80100508d7ef61e57b80800509affff000056b8080050b85200501eb86a0250e80603509affff00008d46c6'
            '50ff76f0ff76eeb83d0050b80a0250e8020383c40a56b8080050b86400501eb80a0250e8d702509affff0000'
            'ff760e8d76a61e569affff000031c031f6eb1e31c0509affff0000ebf0ff760e50ff760aff7608ff76069aff'
            'ff000089d689f28d66fa5f5ee9bffd5589e5b88000e85b02565783ec5a8b760ca110018946f0a112018946f2'
            '8936ce02bb1a0031d28d46bce89602c746beffffc746c0ffff8976c6561eb81a00509affff00008946c8561e'
            'b85300509affff00008946ca31c0509affff00008946ccc746d258008c5ed48d56bc1e529affff000085c075'
            '06b80100e9d901561eb85800508d56a21e529affff000031c0501eb85e00508d56bc1e529affff00008b46be'
            'a3ca028b46c0a3cc02c746beffffc746c0ffff8976c6c746d265008c5ed48d56bc1e529affff0000b8100050'
            'b8020050b8010050508d56f01e529affff000089c7bb040031d28d46f4e8d5015731c050b80400508d56f41e'
            '529affff0000579affff00001eb86e005031c0509affff000089c7501eb878005031c050b80700509affff00'
            '00579affff00001eb86e005031c0509affff00008946f8508d56e81e5231c050b80700509affff000089c789'
            '56faff76f89affff00008b46fa85c07f08750485ff770231ffc643e8001eb86e00508d56f41e52b80002509a'
            'ffff00008d46e8508a46f730e4508a46f6508a46f5508a46f450817ea6ffff750c817ea4ffff7505b80100eb'
            '0230c050b8800050b86a0250e8f50083c4101eb85800501eb8a20050b8cf005031c050b8280050b83c0050b8'
            '400150b8c8005031c050505650509affff000089c71eb86500501eb8ac0050b800505031c050b8080050b87c'
            '0050b8780050b818005057b80100505631c050509affff000057ff76049affff0000579affff00008d56d61e'
            '5231c05050509affff000085c074168d56d61e529affff00008d56d61e529affff0000ebd78b46da8d66fc5f'
            '5e5dc20a00008c161401c33bc473132bc4f7d83b060a0077068cd03b0614017401c30e5ab8fb04bb0100e9f8'
            '01537461636b204f766572666c6f772100515789c78cd88ec083c9ff33c0f2aef7d14989c85f59c35589e553'
            '528b46048d5e088b5606e8eb015a5b5dc3515789c789d988d0571e078ae0d1e9f3ab13c9f3aa5f89f85f59c3'
            'eb0203009affff00000bc0750e5033c0ba0f00e88e0258b44ccd218c061a01575606535253bed10389363501'
            '8c1e3701268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07b9d103bf0a02'
            '2bcf32c0f3aab89c06a33101b8ffffa33301b430cd21a21c0188261d019affff0000a21e0188261f0186e0a3'
            '200154583bc475120f01e0a90100740ab001a22201b003a223011ebf3d0157b89000509affff0000b83d0189'
            '3e39018c1e3b01b8ff00e88f01e863fce91d014f70656e20576174636f6d20432f432b2b31362052756e2d54'
            '696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20436f6e747269'
            '6275746f727320323030322d323032362e20506f7274696f6e7320436f707972696768742028432920537962'
            '6173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df8d9e7aff8cd9'
            '8ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026c607001e8d9e'
            '7aff531eb8ba005031c0509affff000089f8e96800515689c189d6e8570185c0750689c889f2eb9889d8e950'
            '00535689c38b378d440189078814ff470a5e5bc3515689c6b90b07e8640a01c6c604005e59c3c3c3535289c3'
            '833ef401007404ff16f401ff16ce01baff00b81000e8a800ff16ce01ff16d00189d8e904fe5289c2ff16ce01'
            'ff16d00189d0e9f4fd5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b4702'
            '8b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee8effebf0a0289fabbfe0189fe88c839fb'
            '7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcb'
            'e89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee898fe88d5bf0a0289fabb0a0289fe88c839'
            'fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c'
            '007505e827ffebeee83bffebe91f8d66f85f5eeb9f803ed20100750331c0c35250cceb0657564944454fb801'
            '0083c404c3535689c689d3c7470200008a043c2d740b3c23750d804f020146ebef804f0208ebf73c2b750a80'
            '4f0204806702fdebe93c20750cf647020475df804f0202ebd93c307505884719ebd089f05e5bc351565789c6'
            '89d189dac6471920e8a2ff89c7c74706000089c6803d2a74228a043c307c2e3c397f2aba0a008b4706f7ea89'
            'c28a04982d300001c246895706ebde89cf8305028b3d8b45fe89470685c07c3846c74708ffff803c2e754846'
            'c747080000803c2a742b8a043c307c373c397f33ba0a008b4708f7ea89c28a04982d300001c246895708ebde'
            'f75f06804f0208ebbf89cf8305028b3d8b45fe89470885c07c21468a048d54013c68731e3c4c73573c497555'
            '807c01367536807c02347530e97700c74708ffffebd88d7c02770e807c0168755089fe804f0210eb133c7473'
            '153c6c75263a4401751689fe804f028089f05f5e59c3764f3c7a744b3c7775f046804f0240ebe9eb0aeb1c3c'
            '6a75e189d6ebd9762a3c5774123c4e75d389d6804f0302ebcbeb0b3c4675c546804f0304ebbe89d6804f0220'
            'ebb683c603ebad89d6804f0301eba946eba6565789c689d789da8ec731c039d0740c89f34626803f00740340'
            'ebf05f5ec35589e551565783ec0289de89c389d731c983feff75278ec7268b0785c0741489c28d46f883c302'
            'e83f093dffff74e701c1ebe389c88d66fa5f5e595dc38ec7268b0785c0741839f17f1489c28d46f883c302e8'
            '14093dffff74e301c1ebdf39f17ed189f0ebcf5589e5515683ec0a5289d9bb10008d56f2e852098d46f28b5e'
            'f0e861fa89c601cb89c889f24e85d27e0f89c24b4885d27e078a52f28817ebea8b76f001c689c24e4885d27e'
            '05c60430ebf38b5ef001cbc607008d66fce97aff5589e5565783ec0889c68956f8895ef6894efa85c97c288b'
            '5ef8837f08ff7505c747080400bb0a008b46fa89f2e8dd088976f48d4401803c00741489c6ebf4f75efac604'
            '2df75ef6835efa0046ebc88b5ef8837f08007435c6042e31ff89c68b5ef83b7f087d238b46f6bb0a0031d231'
            'c9c746fa0000e8af088956fa8946f68a46fa043047880446ebd5c60400f646f78074423b76f474114e803c2e'
            '75014e803c397538c60430ebeac60431468a048d7c013c30750489feebf33c2e7510c604308d75018805803c'
            '30750346ebf8c6043046c604008d66fc5f5e5dc3fe04ebf55389c3f64702087522807f1930751c8b47062b47'
            '0c2b470e2b47102b47122b47142b471685c07e0301470e5bc35589e5565783ec0689c78956fa894ef8837f10'
            '007eb28e46fa8d46f6268b15e87b0783c70289c13dffff74e43b47107f198d76f64983f9ff74d68a049889c2'
            '89d846ff56f8ff4f10ebeac747100000e976ff0e0d7711591177110e0d9f0e0e0d7711771177117711771177'
            '1177117711961077117711e20e77117711771177115f0f771177117711771177117711771177110e0df70f0b'
            '11ac0f0e0d9f0e0e0d7711ac0f77117711771177117711fe0f961077117711e20e7711ac0f771177115f0f55'
            '89e5565783ec125089d789dec7471600008946f48b471689471489471289471089470e89470c8a47188c5ef6'
            '3c6973393c64756a8a4402a88074618305088b1d8b47fc8946f28b47fa8946f08b47f88b4ffe8946eee9e500'
            '8b46e889f389faff46f4ff16f601e9b50176c93c7875238a4402a88074308305088b1d8b47fc8946f28b47fa'
            '8946f08b47f88b4ffe8946eeeb293c7574d93c6f74d5eb1feb653c6274cd3c58ebf2a840742a8305048b1d8b'
            '47fc8946f88b47fe8946fa8a44182c41c746ec0a003c37775130e489c3d1e32effa7450c8305028b1d8b47fe'
            'c746fa00008946f8f64402207407c746fa0000ebcaf644021074c48a46f830e48946f830c0ebb5a840741683'
            '05048b1d8b47fc8946f88b47fe8946faeb23e9a4038305028b1d8b47fe998946f88a44028956faa82074618b'
            '46f8998946f88956fa30c0f6440280745985c97c10750a837ef2007504837ef00084c0745a8b440c89c28b5e'
            'e84201c389540cc6072df6440280743631c02b46ee8946eeb800001b46f08946f0b800001b46f28946f2b800'
            '0019c889c1e927ffa81074a58a46f898eb98837efa007cb5ebaff75efaf75ef8835efa00e908ff8a4402a804'
            '74148b440c89c28b5ee84201c389540cc6072be9edfea8027503e9e6fe8b440c89c28b5ee84201c389540cc6'
            '0720e9d2fef64402207503e966fe8305048b1d8b47fc89f28946f88b47fe8b5ef88946fa89c18b46e8e820fc'
            'bbffff8b46e88cdae837fb89441089f08b56f6e8eafc8b46f4e9d9fc8b5ee8c607008a4403a804753fa80283'
            '05028b1d8b5ffe85db74068c5ef6895ef4807c1853754ef6440220743a8b46f48b56f68b5c08e8edfa894410'
            '837c08007cb03b44087eab8b4408eba38305048b1d8b47fe8b7ffc85c0750485ff74c2897ef48946f6ebba8b'
            '46f48b56f68b5c08e8d2faebc4f644024075ecebb08a4402a8017441a8807503e9820085c97512837ef20075'
            '0c837ef0007506837eee0074248b440c89c28b5ee84201c389540cc607308b440c8b5ee889c201c3428a4418'
            '89540c8807c746ec1000837c08ff7404c64419208b5ee8035c0c8c5ef6895ef4f6440280743d837c08007539'
            '85c97535837ef200752f837ef0007529837eee0075238e46f631c026c60700eb488b46fa0b46f8eb8ac746ec'
            '0200ebaec746ec0800eba7eb5cff76ec8b46e88b5ef203440c8b56ee5089c88b4ef0e81404807c185875068b'
            '46e8e82005bbffff8b46f48b56f6e8d1f989441089c23b44087c4af64402017503e98afe837eec0a7df785c0'
            '7409c45ef426803f3074eaff440ee971fe837c0800750e8b46fa0b46f875068e46f6e972ff8b5ee88b4eec8b'
            '46f88b56fa035c0ce8e204eb948b440829d089440ee93efe837c0600750bf64403047458c744060900806402'
            'f98305028b1d8b4ffef6440304744883c302891d8b56e88b47febb0400e8c3f98b5ee889c88d5705c647043a'
            'bb0400e8b1f9807c185075068b46e8e86304bbffff8b46f48b56f6e814f989440ce9dafdc744060400eba6bb'
            '04008b56e889c8ebcec744100100f644024075108305028b1d8a47fe8b5ee88807e9aefd8305028b1d8d46ea'
            '8b57fee85c023dffff7503e998fd8b5ee88a46ea880730e4e8960485c074ec8a46eb884701ff440ce97bfd83'
            '05028b1d8b46e88b57fee829023dffff7403e962fdc744100000e95dfdc7440600008b5ee88a44188807c744'
            '0c0100e948fd5589e5565783ec6a5389cfc646fa00c746dc0000c746dec800c746e400008946da8956f88b5e'
            'f88a0784c074588d4f013c25740d9889c28d46da894ef8ffd7ebe38d5eda8d569089c8e8e5f689c38a074388'
            '46f2895ef884c0742a3c6e755c8a46dda8047422834690048b5e908b76908b5ffc8e44fe8a46dca810741f8a'
            '46e4268807eb9fe976018b5e9083c302a8028cd8895e908ec08b5ffeebdaa82074098b46e4268907e97bffa8'
            '4074108b46e49926890726895702e969ffeb1fa88074df8b46e499268907b10f2689570226d3fa2689570426'
            '895706e948ff8d4efa8d5eda8d56908d4692e83efa89c68b46e60346e80346ea0346ec0346ee0346f08956f6'
            '2946e0f646dc0874198d5e92837ee6007e298a079889c28d46da43ffd7ff4ee6ebea807ef32075e1837ee000'
            '7edbba20008d46daffd7ff4ee0ebed837ee8007e0dba30008d46daffd7ff4ee8ebed8a46f23c737540f646dc'
            '40751a837eea007e218e46f6268a049889c28d46da46ffd7ff4eeaebe68d5eda8b56f689f989f0e8e3f8837e'
            'ec007e2bba30008d46daffd7ff4eecebed3c5374dc837eea007ee38e46f6268a049889c28d46da46ffd7ff4e'
            'eaebe6837eee007e148e46f6268a049889c28d46da46ffd7ff4eeeebe6837ef0007e0dba30008d46daffd7ff'
            '4ef0ebedf646dc087503e939fe837ee0007ef7ba20008d46daffd7ff4ee0ebed8b46e4e92bf85389c385c074'
            '0df6c6ff740a8837b802008857015bc3b8010088175bc35589e551565783ec245289d9c646d6008d76d789d7'
            '8d5ef8894ef833d2f737890789d38a87e80088048b46f84685c075e44e8a0488054784c075f68b46d48d66fa'
            '5f5e595dc35689d683fb0a750485c07c07e8abff89f05ec3f7d842c6042debf19350920bc07402f7e2910bc0'
            '7404f7e303c858f7e303d1c35352bb0100b8c6008cdae9c0f25589e5565783ec48538b76048976f88d76b5c6'
            '46b4008976fa8b76068b5eb28976f68d76f68bfa85c074278bd08bc333db3b1472098bd88bc233d2f73493f7'
            '3491f73497f73489148bd08bc38bd98bcfeb2c85db74148bd38bc133db33c93b1472df8bc88bc233d2ebd48b'
            'd18bc733c933ff3b1472ce8bf88bc233d2ebc38b7ef689de895eb28a9de8008b7efaff46fa881d85c0758385'
            'f67403e97cff85c975f985d275f5ff4efa8b76fa8a048b76f8ff46f8880484c075ec8b46048d66fc5f5e5dc2'
            '04005589e5565783ec028b76048976fa837e060a750c85c07c15750685db750285c9ff7606ff76fae812ff89'
            'f0ebca31ff29d789fabf000019cf89f9bf000019df89fbbf000019c789f88d7c01c6042d897efaebcd535289'
            'c289c38a0784c0740f2c613c19760343ebf104418807ebf789d05a5bc35589e5565783ec245351c646d8008d'
            '76d989df8b5ed4895efa8d5efa33c93b1772069287d1f73791f73789178bd18b5efa8a9fe800881c4685d275'
            'd785c075d34e8a0488054784c075f68b46d68d66fc5f5e5dc35689de83f90a750485d27c07e89dff89f05ec3'
            'f7dac6072df7d883da0043ebec5389c38a9fd10280e30130e789d85bc331c0e957005352bb010131c031d2a3'
            'fc01a3fa01b8d002e82eef5a5bc35589e553515250c746f80000eb0aff46f8817ef80001731f8a46f830e450'
            '9affff000085c074e78b5ef8c706fc010100c687d10201ebd78d66fa5a595b5dc353515289c2e8a1ff83faff'
            '743e83fafe743983fafd744983fafc7526bb1f00ba0100b85203e8c4eebb1d00ba0100b8b103e8b8eec706fc'
            '010100c706fa01a403eb1e85d27405b80100eb179affff00003d030a72f1e869ff9affff0000a3fa0131c05a'
            '595bc30045000301fa05030031000301ea060100010003012b160300cf0003019716030003000301a4160400'
            '84000301300001007a0003016b00010027000301730002003400030183000100af00030194000100af000301'
            'a60002002d000301c900020022000301d20002002d000301f500020022000301fe0002002d00030106010200'
            '450003010e0102004500030114010200440003012d010100ae00030133010100540003014a010100ae000301'
            '50010100540003016a0101004800030177010200010003019b0102005f010301a901020001000301bd010200'
            'd7010301cc0102005c000301f0010100c4000301070202002100030136020200210003014302010028000301'
            '5102010006000301650201006b000500a40201003a000200a90201003a000301b5020100ae000301c3020100'
            'ad000301ce02020057000301e302010039000301fd020100940103010f030100940105002203010008000200'
            '27030100080003013a030100390003015103020030000301700302004a000301760302004500030183030300'
            '53000301970303005e0103019d03030051000301aa03030055000301bf0303005d010301cc03030051000301'
            'f20303004a0002000f0401003a000500160401003a00030159040100290003018b0401002900030194040100'
            '2a0003019a0401007c000301a90401006c000301b704010071000301c104010072000301570503005b000301'
            '960503001e0003019d05010005000200b705010052050301c805030003000000000000000000000000000000'
            '00000000000005000000000000000000000050494300434845434b0041505049434f4e00457874546578744f'
            '757400457874656e7400746162096865726500657874656e742025647825642c20666163652025730048414e'
            '44004f7752657300425554544f4e004d79427574746f6e004f575245532e544d50006875676520696f006f77'
            '6e2025642c206269747320253032782530327825303278253032782c202573005265736f7572636573005375'
            '706572636c617373656400004f70656e20576174636f6d00466c6f6174696e672d706f696e7420737570706f'
            '7274206e6f74206c6f6164656400303132333435363738396162636465666768696a6b6c6d6e6f7071727374'
            '75767778797a00007800f0000f000000ffff00000000000000000000000c0000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000003107310700000000000000000000000000000000'
            '000000000000000000000000000000000000000026142614000000000020d40400000020ef15000000000000'
            '0000000000000000000000002800000030000000180000000100080000000000000000000000000000000000'
            '400000000000000000ff000010fb080020f7100030f3180040ef200050eb280060e7300070e3380080df4000'
            '90db4800a0d75000b0d35800c0cf6000d0cb6800e0c77000f0c3780000bf800010bb880020b7900030b39800'
            '40afa00050aba80060a7b00070a3b800809fc000909bc800a097d000b093d800c08fe000d08be800e087f000'
            'f083f800007f0000107b08002077100030731800406f2000506b28006067300070633800805f4000905b4800'
            'a0575000b0535800c04f6000d04b6800e0477000f0437800003f8000103b88002037900030339800402fa000'
            '502ba8006027b0007023b800801fc000901bc800a017d000b013d800c00fe000d00be800e007f000f003f800'
            '1718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f000102'
            '03040506161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d'
            '3e3f00010203040515161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738'
            '393a3b3c3d3e3f00010203041415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313233'
            '3435363738393a3b3c3d3e3f00010203131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e'
            '2f303132333435363738393a3b3c3d3e3f00010212131415161718191a1b1c1d1e1f20212223242526272829'
            '2a2b2c2d2e2f303132333435363738393a3b3c3d3e3f00011112131415161718191a1b1c1d1e1f2021222324'
            '25262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f00101112131415161718191a1b1c1d1e1f'
            '202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f0f101112131415161718191a'
            '1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e0e0f101112131415'
            '161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d0d0e0f10'
            '1112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c'
            '0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031323334353637'
            '38393a3b0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132'
            '333435363738393a0a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d'
            '2e2f30313233343536373839090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728'
            '292a2b2c2d2e2f30313233343536373808090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223'
            '2425262728292a2b2c2d2e2f30313233343536370708090a0b0c0d0e0f101112131415161718191a1b1c1d1e'
            '1f202122232425262728292a2b2c2d2e2f30313233343536060708090a0b0c0d0e0f10111213141516171819'
            '1a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313233343505060708090a0b0c0d0e0f1011121314'
            '15161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313233340405060708090a0b0c0d0e0f'
            '101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313233030405060708090a'
            '0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313202030405'
            '060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031'
            '0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c'
            '2d2e2f30000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021222324252627'
            '28292a2b2c2d2e2f280000001000000008000000010001000000000000000000000000000000000002000000'
            '0000000000000000ffffff00f0f00000f0f00000f0f00000f0f000000f0f00000f0f00000f0f00000f0f0000'
            '2800000020000000400000000100040000000000000000000000000000000000100000000000000000000000'
            '000080000080000000808000800000008000800080800000c0c0c000808080000000ff0000ff000000ffff00'
            'ff000000ff00ff00ffff0000ffffff0000000000000000000000000000000000000000000000999999990000'
            '0000000000000000099999999999999000000000000000009999999999999999000000000000009999999999'
            '9999999999000000000009999999999999999999999000000000999999999999999999999999000000009999'
            '9999999999999999999900000009999999999999999999999999900000999999999999999999999999999900'
            '0099999999cccccccccccc99999999000099999999cccccccccccc99999999000999999999cccccccccccc99'
            '999999900999999999cccccccccccc99999999900999999999cccccccccccc99999999900999999999cccccc'
            'cccccc99999999900999999999cccccccccccc99999999900999999999cccccccccccc999999999009999999'
            '99cccccccccccc99999999900999999999cccccccccccc99999999900099999999cccccccccccc9999999900'
            '0099999999cccccccccccc999999990000999999999999999999999999999900000999999999999999999999'
            '9999900000009999999999999999999999990000000099999999999999999999999900000000099999999999'
            '9999999999900000000000999999999999999999990000000000000099999999999999990000000000000000'
            '0999999999999990000000000000000000009999999900000000000000000000000000000000000000000000'
            'fffffffffff00fffff8001ffff0000fffc00003ff800001ff000000ff000000fe0000007c0000003c0000003'
            'c00000038000000180000001800000018000000180000001800000018000000180000001c0000003c0000003'
            'c0000003e0000007f000000ff000000ff800001ffc00003fff0000ffff8001fffff00fffffffffff28000000'
            '1000000020000000010004000000000000000000000000000000000010000000000000000000000000008000'
            '0080000000808000800000008000800080800000c0c0c000808080000000ff0000ff000000ffff00ff000000'
            'ff00ff00ffff0000ffffff00000000000000000000000aaaaaa000000000aaaaaaaa0000000aaaaaaaaaa000'
            '00aaaaaaaaaaaa000aaaaaaaaaaaaaa00aaaaaaaaaaaaaa00aaaaaaaaaaaaaa00aaaaaaaaaaaaaa00aaaaaaa'
            'aaaaaaa00aaaaaaaaaaaaaa000aaaaaaaaaaaa00000aaaaaaaaaa0000000aaaaaaaa000000000aaaaaa00000'
            '0000000000000000ffff0000f81f0000f00f0000e0070000c003000080010000800100008001000080010000'
            '8001000080010000c0030000e0070000f00f0000f81f0000ffff000028000000200000004000000001000400'
            '0000000000000000000000000000000010000000000000000000000000008000008000000080800080000000'
            '8000800080800000c0c0c000808080000000ff0000ff000000ffff00ff000000ff00ff00ffff0000ffffff00'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000bb0000000'
            '0000000000000000000000bbbb000000000000000000000000000bbbbbb0000000000000000000000000bbbb'
            'bbbb00000000000000000000000bbbbbbbbbb000000000000000000000bbbbbbbbbbbb000000000000000000'
            '0bbbbbbbbbbbbbb00000000000000000bbbbbbbbbbbbbbbb000000000000000bbbbbbbbbbbbbbbbbb0000000'
            '000000bbbbbbbbbbbbbbbbbbbb00000000000bbbbbbbbbbbbbbbbbbbbbb000000000bbbbbbbbbbbbbbbbbbbb'
            'bbbb0000000bbbbbbbbbbbbbbbbbbbbbbbbbb00000bbbbbbbbbbbbbbbbbbbbbbbbbbbb0000bbbbbbbbbbbbbb'
            'bbbbbbbbbbbbbb00000bbbbbbbbbbbbbbbbbbbbbbbbbb0000000bbbbbbbbbbbbbbbbbbbbbbbb000000000bbb'
            'bbbbbbbbbbbbbbbbbbb00000000000bbbbbbbbbbbbbbbbbbbb0000000000000bbbbbbbbbbbbbbbbbb0000000'
            '00000000bbbbbbbbbbbbbbbb00000000000000000bbbbbbbbbbbbbb0000000000000000000bbbbbbbbbbbb00'
            '0000000000000000000bbbbbbbbbb00000000000000000000000bbbbbbbb0000000000000000000000000bbb'
            'bbb000000000000000000000000000bbbb00000000000000000000000000000bb00000000000000000000000'
            '00000000000000000000000000000000000000000000000000000000fffffffffffffffffffe7ffffffc3fff'
            'fff81ffffff00fffffe007ffffc003ffff8001ffff0000fffe00007ffc00003ff800001ff000000fe0000007'
            'c0000003c0000003e0000007f000000ff800001ffc00003ffe00007fff0000ffff8001ffffc003ffffe007ff'
            'fff00ffffff81ffffffc3ffffffe7fffffffffffffffffff0000010002002020100001000400e80200000100'
            '10101000010004002801000002000000010001002020100001000400e802000003000f000f00280000002000'
            '0000400000000100010000000000000000000000000000000000020000000000000000000000ffffff000000'
            '0000000000000000000000010000000100000001000000010000000100000001000000010000000100000001'
            '0000000100000001000000010000000100001ffffff800010000000100000001000000010000000100000001'
            '0000000100000001000000010000000100000001000000010000000000000000000000000000ffffffffffff'
            'fffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff8'
            '3ffffff83fffc0000003c0000003c0000003c0000003c0000003fff83ffffff83ffffff83ffffff83ffffff8'
            '3ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83fffffffffffffffffff00000200010020004000'
            '01000100340100000400'
        )),
}

if len(sys.argv) != 3 or sys.argv[1] not in PROGRAMS:
    raise SystemExit('usage: mkne_watcom.py ' + '|'.join(PROGRAMS) + ' OUTPUT.EXE')
with open(sys.argv[2], 'wb') as f:
    f.write(PROGRAMS[sys.argv[1]])
