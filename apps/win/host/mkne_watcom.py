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
}

if len(sys.argv) != 3 or sys.argv[1] not in PROGRAMS:
    raise SystemExit('usage: mkne_watcom.py ' + '|'.join(PROGRAMS) + ' OUTPUT.EXE')
with open(sys.argv[2], 'wb') as f:
    f.write(PROGRAMS[sys.argv[1]])
