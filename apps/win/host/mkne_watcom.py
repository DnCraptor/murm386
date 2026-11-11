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

OWKERN: KERNEL and the modules around it: GetWinFlags, GetModuleHandle and
    GetProcAddress (MessageBox through it), LoadLibrary, Catch and Throw, an
    RCDATA resource, GetTempFileName, a palette, DPtoLP, the error mode;
    COMMDLG's Open and Find (its messages) dialogs, SHELL's ShellAbout:
    "flags 415, user 1, box 1, dlg 1", "caught 42, custom resource, 16
    bytes, temp ~owk", "palette 200,100,50 30, dp->lp 15,25, mode 1"

OWUSE, OWDLL, OWDLL2 (OWDLL.DLL, OWDLL2.DLL): a program and its DLLs:
    OWDLL imported by name, by ordinal and by name through LoadLibrary and
    GetProcAddress; OWDLL2, which OWDLL needs, started first; a DLL's data,
    local heap, string resources and a window class with its procedure in
    the DLL: "add 5, 30, 2, lib 1", "calls 307, 'a string of the DLL's',
    'from its heap'", "registered 1", a child "painted by the DLL"

OWFPU: the coprocessor through WIN87EM (__FPMATH, the OS fixups of its
    instructions): arithmetic, sqrt, sin, floor, exp, conversions, the
    control word, printf's formatting: "10.000 0.5000 2499998 0.33333
    2.71828 cw 127f"

OWAPI: KERNEL and USER beyond the basics: an alias selector (AllocSelector,
    Set/GetSelectorBase and Limit, FreeSelector), lstrcpyn, hmemcpy,
    IsBad*Ptr, GlobalDOSAlloc, MulDiv, global atoms, AnsiUpperBuff, the
    character classes; class words (its extra bytes), CreateWindowEx,
    DeferWindowPos, MapWindowPoints, keyboard state, hooks, IsMenu,
    AnyPopup, DialogBoxParam and DialogBoxIndirectParam (a template in
    memory) with their lParam and MapDialogRect, GetQueueStatus, a dialog
    of a class of its own (its window procedure calling DefDlgProc, which
    calls the dialog procedure: a command, WM_CLOSE's IDCANCEL), a
    resource freed while ES still has its selector (ES 0 after it), hooks
    USER calls (WH_CBT's HCBT_CREATEWND with the CREATESTRUCT, WH_CALLWNDPROC
    subclassing the window at its WM_NCCREATE), an overlapped window's
    first WM_SIZE (from ShowWindow, not CreateWindow), GrayString: "trunc|huge copy|through an alias|limit 31", "bad: null 1,
    ok 0, code-write 1, str 0, free 0", "dos 1, muldiv 67 -11 -32768, ver
    30a, chars 101", "atom WinnAtom 1, buff MIXeD, heap 1", "class 1234,
    mapped 530,170, keys 0, hook 1, menu 0, popup 0", "dialogs 7 7, queue
    20, own class 2 2, freed es 0", "hooks: made 1 50, sent 50, subclassed 1,
    got 1; size 0 1"

OWGDI: GDI beyond the basics: Arc, Pie, Chord, PolyPolygon (WINDING),
    FloodFill; elliptic, rounded and polygonal regions, FrameRgn,
    InvertRgn; MM_ISOTROPIC (a circle, y up) and MM_LOENGLISH (an inch);
    DIBs (SetDIBitsToDevice, StretchDIBits, CreateDIBitmap, GetDIBits, a
    DIB pattern brush); EnumFonts, GetCharWidth, LineDDA: "dib back: 09 aa
    b0, top-left blue 1", "iso 327,133, map 1, horz 169mm, aspect 10",
    "fonts 5 System,MS Sans Serif,Fixedsys, W 8 X 8, dda 40"

OWAPI2: KERNEL and USER, more: the clipboard (its text and a format of its
    own through WINN's), DlgDirList and DlgDirSelect (C:\\HOSTWIN\\*.DLL),
    window placement, EnumProps, LoadMenuIndirect, ChangeMenu,
    ScrollWindowEx, CreateCursor, CreateIcon, CopyIcon, lstrcatn,
    AllocSelectorArray, SelectorAccessRights, GetCodeHandle: "clip
    \"clipboard text\" \"own\" 2 formats", "array 1, rights f2, catn abcde,
    code 1", "place 30,30 show 1, props 11, menu 2 11", "cursor 1 icon 1
    copy 1, scrolled 4", "dir: 4 entries, spec *.DLL, first owdll.dll"; then
    (SPACE) OpenComm, no ports: "OpenComm -10"; then UserSeeUserDo,
    which WINN has not: "OWAPI2.EXE called USER.216, which WINN does not
    have yet."

OWMETA: metafiles: one recorded (pens, a hatch and a pattern brush, a
    font, a region, shapes, text, a blit and a stretch from a memory DC, a
    DIB), played, played into half the size (MM_ANISOTROPIC), so again
    record by record (EnumMetaFile, PlayMetaFileRecord); its bits and back,
    a file and back: "bits 1, header 1, valid 1 1, file 1, records 47,
    pens+brushes 5", "played 47"

OWSYS: Windows' own DLLs, WINN's: MMSYSTEM (no devices, MCI none,
    timeGetTime, timeSetEvent's procedure, a RIFF file written and read
    back through mmio), TOOLHELP (TimerCount, MemManInfo, SystemHeapInfo,
    the tasks, the modules, MemoryRead), VER (its own VERSIONINFO,
    VerQueryValue, VerLanguageName, VerFindFile, VerInstallFile expanding)
    and LZEXPAND (a file as COMPRESS.EXE makes them: LZOpenFile by its
    name, LZRead, LZSeek, LZCopy, GetExpandedName): "devs 0 0 0 0, snd 0,
    riff 0 136 100 1, ...", "time 1, event 1", "timer 1, page 4096, heap
    80%, tasks 1 mine 1, modules 11 KERNEL, user 1, read 1234", "ver 1,
    file 10002, \"WINN system DLLs test\", English (United States), find 1
    1" (VerFindFile: in its own directory), "lz \"ABCABCABCABC\" \"ABC\"
    12, C:\\HOSTWIN\\OWSYS.TXT, copy 12, install 0 12", "ticks 1" (0.3 s
    of timeSetEvent's 50 ms, at least 4 calls, however late the messages),
    "env 1, count 1, path 1, realloc \"kept\" 2000" (the DOS environment
    through the PSP's 2Ch: its strings, the count, the program's path; a
    local block made larger keeps its bytes), "bios vga 1, equip 22, mem
    640, clock 1, cp 866" (INT 10h 1A00h, INT 11h, 12h, 1Ah, INT 21h
    6601h: DOS's code page, 866 on the host), "library 1, handle 1, proc
    1 1, start 1, none 1" (LZEXPAND, a native DLL, as a library:
    LoadLibrary, GetModuleHandle, GetProcAddress by name and ordinal the
    import's function, one it has not, FreeLibrary)

OWFONT: a raster font file's faces (WINNTEST.FON beside it, mkfon.py:
    "Winn Test" 12 and 24 bold; AddFontResource): each size, the nearest
    for 20, twice 24, bold and italic made, underlined and struck out; the
    VGA glyphs twice as large (System 32) and italic; with GetTextFace and
    GetTextMetrics' height, ascent, internal leading and GetTextExtent:
    "added 2, faces 6, sizes 2", "Small ...: Winn Test 12/10/2 61", ...
    "x2: Winn Test 48/40/8 ...", "VGA x2: System 32/26/4 90"; (SPACE) a
    dialog whose template has the font (FONT 9, "Winn Test"): its controls
    in it, its units the font's: "Units 9 x 12, font 1"

OWDLG: COMMDLG.DLL's Color (opened full: a basic colour picked, Add to
    Custom Colors), Font (System, Bold Italic, underlined, maroon), Print
    (PD_RETURNDEFAULT: no printer) and GetFileTitle: "color 1 0000FF err
    0, custom 0000FF FFFFFF", "font 1 err 0: System -15, w 700 i 1 u 1,
    110/10 pt, Bold Italic, type A300, 000080", "print 0 err 1008, title 0
    WIN.INI"

OWMISC: what WRITE.EXE needed: moveable local memory (its handle an entry
    with the block's pointer, read as *h), LocalHandleDelta, KEYBOARD's
    VkKeyScan, MapVirtualKey, GetKeyNameText and ToAscii, OLECLI: "local:
    handle 1, flags 1, 'moveable' 1, size 3000, discarded 4000 0, delta 20
    A0", "keyboard: 141 41 1BF, 1E 41, A Esc, 1 !", "olecli: 0 12 0"

OWSND: SOUND.DLL (a scale and a tone queued, played, waited for; a note
    out of range) and OLESVR.DLL (a server and a document registered and
    revoked, their Release methods called): "sound: open 1, size 0, accent
    0, note 0, tone 0, bad -5, count 6, start 0, wait 0, count 0", "olesvr:
    0 0 0 0 0, more 0, released 1 1, version 1"; the tones on stderr

OW31: the rest of Windows 3.1's USER, GDI and KERNEL: "clipboard: draws 2,
    viewer 0, format 1; cursor 109,219 in 10,20,110,220; timer res 1000",
    "dc: was 9, bounds 3 5,6,25,16 then 1; pens 20 brushes 22; relabs 1 2;
    window 1; font 1", "gdi: pen 1, pal 1, none 0; palette 5; rasterizer 6
    0; priority 3; font data -1", "system timers 2"
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
        #     OFSTRUCT of;
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
        #     OpenFile("OWRES.TMP", &of, OF_DELETE);
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
            '2a0102000000000002030200000400205805010000000200020004000d0040005000ff0008011001ac010000'
            '0000010009000208dd00821000000a03dd00b816400db816560d0a02410ce003010003800300000000005f11'
            '74011010018000000000d312940010100280000000006713740110100380000000000e80020000000000db14'
            '11003010980000000000ec140a0030100280000000000180010000000000f6149a0010100480000000000c80'
            '01000000000090150a003010a600000000000280020000000000630ed4023000ab0000000000371128003000'
            'a0000000000000000741505049434f4e05434845434b0448414e4403504943054f5752455300000001000600'
            '0a00110000045553455203474449064b45524e454c084b4559424f4152440000096f777265732e6578650000'
            '0000ccebfd90909090001e5890455589e51e8ed8b81600e8c704ff36cc02ff36ca02ff760eff760cff760aff'
            '7608ff76069affff00001f5d4dca0a001e5890455589e51e8ed8b87600e89504565783ec548b460c3d0f0074'
            '0b3d02007403e9f801e9eb01ff760e8d76a61e569affff000089c6509affff000089c7ff36ce021eb8100050'
            '9affff00008946f2ff36ce021eb81400509affff00008946f8a10e018946f657ff76f29affff00008946f456'
            'b808005050b8300050b81800505731c05050b8cc0050b82000509affff000057ff76f89affff000056b84000'
            '50b8080050b8100050b80800505731c05050b8cc0050b82000509affff000057ff76f49affff0000ff76f29a'
            'ffff0000ff76f89affff0000579affff000056b8600050b8040050ff36ce0231c050b80200509affff000050'
            '9affff000056b8880050b8040050ff36ce021eb81a00509affff0000509affff00008d7ee61e57b8080050b8'
            '280050b8c80050b83a00509affff00005631c050b8ffff509affff000056b80a0050b8290050b80200508d7e'
            'e61e571eb8220050b80a005031c050509affff000056b8ff0050b8ffff509affff0000561eb82d0050b80600'
            '508d7eee1e579affff000056b82000508d7ec61e579affff000056b8080050b83e00501eb8340050b8080050'
            'b80100508d7ef61e57b80800509affff000056b8080050b85200501eb86a0250e80c03509affff00008d46c6'
            '50ff76f0ff76eeb83d0050b80a0250e8080383c40a56b8080050b86400501eb80a0250e8dd02509affff0000'
            'ff760e8d76a61e569affff000031c031f6eb1e31c0509affff0000ebf0ff760e50ff760aff7608ff76069aff'
            'ff000089d689f28d66fa5f5ee9bffd5589e5b80801e86102565781ece2008b760ca110018946f4a112018946'
            'f68936ce02bb1a0031d28d46bce89b02c746beffffc746c0ffff8976c6561eb81a00509affff00008946c856'
            '1eb85300509affff00008946ca31c0509affff00008946ccc746d258008c5ed48d56bc1e529affff000085c0'
            '7506b80100e9de01561eb85800508d56a21e529affff000031c0501eb85e00508d56bc1e529affff00008b46'
            'bea3ca028b46c0a3cc02c746beffffc746c0ffff8976c6c746d265008c5ed48d56bc1e529affff0000b81000'
            '50b8020050b8010050508d56f41e529affff000089c7bb040031d28d46f0e8da015731c050b80400508d56f0'
            '1e529affff0000579affff00001eb86e005031c0509affff000089c7501eb878005031c050b80700509affff'
            '0000579affff00001eb86e005031c0509affff00008946f8508d56e81e5231c050b80700509affff00008946'
            'fa89d7ff76f89affff000085ff7f08750b837efa0076058b7efaeb0231ffc643e8001eb86e00508d961aff1e'
            '52b80002509affff00008d46e8508a46f330e4508a46f2508a46f1508a46f050817ea6ffff750c817ea4ffff'
            '7505b80100eb0230c050b8800050b86a0250e8f50083c4101eb85800501eb8a20050b8cf005031c050b82800'
            '50b83c0050b8400150b8c8005031c050505650509affff000089c71eb86500501eb8ac0050b800505031c050'
            'b8080050b87c0050b8780050b818005057b80100505631c050509affff000057ff76049affff0000579affff'
            '00008d56d61e5231c05050509affff000085c074168d56d61e529affff00008d56d61e529affff0000ebd78b'
            '46da8d66fc5f5e5dc20a00008c161401c33bc473132bc4f7d83b060a0077068cd03b0614017401c30e5ab801'
            '05bb0100e9f801537461636b204f766572666c6f772100515789c78cd88ec083c9ff33c0f2aef7d14989c85f'
            '59c35589e553528b46048d5e088b5606e8eb015a5b5dc3515789c789d988d0571e078ae0d1e9f3ab13c9f3aa'
            '5f89f85f59c3eb0203009affff00000bc0750e5033c0ba0f00e88e0258b44ccd218c061a01575606535253be'
            'd103893635018c1e3701268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07'
            'b9d103bf0a022bcf32c0f3aab8a206a33101b8ffffa33301b430cd21a21c0188261d019affff0000a21e0188'
            '261f0186e0a3200154583bc475120f01e0a90100740ab001a22201b003a223011ebf3d0157b89000509affff'
            '0000b83d01893e39018c1e3b01b8ff00e88f01e85dfce91d014f70656e20576174636f6d20432f432b2b3136'
            '2052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20'
            '436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f707972696768742028'
            '4329205379626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df'
            '8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026'
            'c607001e8d9e7aff531eb8ba005031c0509affff000089f8e96800515689c189d6e8570185c0750689c889f2'
            'eb9889d8e95000535689c38b378d440189078814ff470a5e5bc3515689c6b91107e8640a01c6c604005e59c3'
            'c3c3535289c3833ef401007404ff16f401ff16ce01baff00b81000e8a800ff16ce01ff16d00189d8e904fe52'
            '89c2ff16ce01ff16d00189d0e9f4fd5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e553'
            '5289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee8effebf0a0289fabbfe01'
            '89fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88a'
            'ffc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee898fe88d5bf0a0289fabb0a'
            '0289fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402eb'
            'd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803ed20100750331c0c35250cceb065756'
            '4944454fb8010083c404c3535689c689d3c7470200008a043c2d740b3c23750d804f020146ebef804f0208eb'
            'f73c2b750a804f0204806702fdebe93c20750cf647020475df804f0202ebd93c307505884719ebd089f05e5b'
            'c351565789c689d189dac6471920e8a2ff89c7c74706000089c6803d2a74228a043c307c2e3c397f2aba0a00'
            '8b4706f7ea89c28a04982d300001c246895706ebde89cf8305028b3d8b45fe89470685c07c3846c74708ffff'
            '803c2e754846c747080000803c2a742b8a043c307c373c397f33ba0a008b4708f7ea89c28a04982d300001c2'
            '46895708ebdef75f06804f0208ebbf89cf8305028b3d8b45fe89470885c07c21468a048d54013c68731e3c4c'
            '73573c497555807c01367536807c02347530e97700c74708ffffebd88d7c02770e807c0168755089fe804f02'
            '10eb133c7473153c6c75263a4401751689fe804f028089f05f5e59c3764f3c7a744b3c7775f046804f0240eb'
            'e9eb0aeb1c3c6a75e189d6ebd9762a3c5774123c4e75d389d6804f0302ebcbeb0b3c4675c546804f0304ebbe'
            '89d6804f0220ebb683c603ebad89d6804f0301eba946eba6565789c689d789da8ec731c039d0740c89f34626'
            '803f00740340ebf05f5ec35589e551565783ec0289de89c389d731c983feff75278ec7268b0785c0741489c2'
            '8d46f883c302e83f093dffff74e701c1ebe389c88d66fa5f5e595dc38ec7268b0785c0741839f17f1489c28d'
            '46f883c302e814093dffff74e301c1ebdf39f17ed189f0ebcf5589e5515683ec0a5289d9bb10008d56f2e852'
            '098d46f28b5ef0e861fa89c601cb89c889f24e85d27e0f89c24b4885d27e078a52f28817ebea8b76f001c689'
            'c24e4885d27e05c60430ebf38b5ef001cbc607008d66fce97aff5589e5565783ec0889c68956f8895ef6894e'
            'fa85c97c288b5ef8837f08ff7505c747080400bb0a008b46fa89f2e8dd088976f48d4401803c00741489c6eb'
            'f4f75efac6042df75ef6835efa0046ebc88b5ef8837f08007435c6042e31ff89c68b5ef83b7f087d238b46f6'
            'bb0a0031d231c9c746fa0000e8af088956fa8946f68a46fa043047880446ebd5c60400f646f78074423b76f4'
            '74114e803c2e75014e803c397538c60430ebeac60431468a048d7c013c30750489feebf33c2e7510c604308d'
            '75018805803c30750346ebf8c6043046c604008d66fc5f5e5dc3fe04ebf55389c3f64702087522807f193075'
            '1c8b47062b470c2b470e2b47102b47122b47142b471685c07e0301470e5bc35589e5565783ec0689c78956fa'
            '894ef8837f10007eb28e46fa8d46f6268b15e87b0783c70289c13dffff74e43b47107f198d76f64983f9ff74'
            'd68a049889c289d846ff56f8ff4f10ebeac747100000e976ff140d7d115f117d11140da50e140d7d117d117d'
            '117d117d117d117d117d119c107d117d11e80e7d117d117d117d11650f7d117d117d117d117d117d117d117d'
            '11140dfd0f1111b20f140da50e140d7d11b20f7d117d117d117d117d1104109c107d117d11e80e7d11b20f7d'
            '117d11650f5589e5565783ec125089d789dec7471600008946f48b471689471489471289471089470e89470c'
            '8a47188c5ef63c6973393c64756a8a4402a88074618305088b1d8b47fc8946f28b47fa8946f08b47f88b4ffe'
            '8946eee9e5008b46e889f389faff46f4ff16f601e9b50176c93c7875238a4402a88074308305088b1d8b47fc'
            '8946f28b47fa8946f08b47f88b4ffe8946eeeb293c7574d93c6f74d5eb1feb653c6274cd3c58ebf2a840742a'
            '8305048b1d8b47fc8946f88b47fe8946fa8a44182c41c746ec0a003c37775130e489c3d1e32effa74b0c8305'
            '028b1d8b47fec746fa00008946f8f64402207407c746fa0000ebcaf644021074c48a46f830e48946f830c0eb'
            'b5a84074168305048b1d8b47fc8946f88b47fe8946faeb23e9a4038305028b1d8b47fe998946f88a44028956'
            'faa82074618b46f8998946f88956fa30c0f6440280745985c97c10750a837ef2007504837ef00084c0745a8b'
            '440c89c28b5ee84201c389540cc6072df6440280743631c02b46ee8946eeb800001b46f08946f0b800001b46'
            'f28946f2b8000019c889c1e927ffa81074a58a46f898eb98837efa007cb5ebaff75efaf75ef8835efa00e908'
            'ff8a4402a80474148b440c89c28b5ee84201c389540cc6072be9edfea8027503e9e6fe8b440c89c28b5ee842'
            '01c389540cc60720e9d2fef64402207503e966fe8305048b1d8b47fc89f28946f88b47fe8b5ef88946fa89c1'
            '8b46e8e820fcbbffff8b46e88cdae837fb89441089f08b56f6e8eafc8b46f4e9d9fc8b5ee8c607008a4403a8'
            '04753fa8028305028b1d8b5ffe85db74068c5ef6895ef4807c1853754ef6440220743a8b46f48b56f68b5c08'
            'e8edfa894410837c08007cb03b44087eab8b4408eba38305048b1d8b47fe8b7ffc85c0750485ff74c2897ef4'
            '8946f6ebba8b46f48b56f68b5c08e8d2faebc4f644024075ecebb08a4402a8017441a8807503e9820085c975'
            '12837ef200750c837ef0007506837eee0074248b440c89c28b5ee84201c389540cc607308b440c8b5ee889c2'
            '01c3428a441889540c8807c746ec1000837c08ff7404c64419208b5ee8035c0c8c5ef6895ef4f6440280743d'
            '837c0800753985c97535837ef200752f837ef0007529837eee0075238e46f631c026c60700eb488b46fa0b46'
            'f8eb8ac746ec0200ebaec746ec0800eba7eb5cff76ec8b46e88b5ef203440c8b56ee5089c88b4ef0e8140480'
            '7c185875068b46e8e82005bbffff8b46f48b56f6e8d1f989441089c23b44087c4af64402017503e98afe837e'
            'ec0a7df785c07409c45ef426803f3074eaff440ee971fe837c0800750e8b46fa0b46f875068e46f6e972ff8b'
            '5ee88b4eec8b46f88b56fa035c0ce8e204eb948b440829d089440ee93efe837c0600750bf64403047458c744'
            '060900806402f98305028b1d8b4ffef6440304744883c302891d8b56e88b47febb0400e8c3f98b5ee889c88d'
            '5705c647043abb0400e8b1f9807c185075068b46e8e86304bbffff8b46f48b56f6e814f989440ce9dafdc744'
            '060400eba6bb04008b56e889c8ebcec744100100f644024075108305028b1d8a47fe8b5ee88807e9aefd8305'
            '028b1d8d46ea8b57fee85c023dffff7503e998fd8b5ee88a46ea880730e4e8960485c074ec8a46eb884701ff'
            '440ce97bfd8305028b1d8b46e88b57fee829023dffff7403e962fdc744100000e95dfdc7440600008b5ee88a'
            '44188807c7440c0100e948fd5589e5565783ec6a5389cfc646fa00c746dc0000c746dec800c746e400008946'
            'da8956f88b5ef88a0784c074588d4f013c25740d9889c28d46da894ef8ffd7ebe38d5eda8d569089c8e8e5f6'
            '89c38a07438846f2895ef884c0742a3c6e755c8a46dda8047422834690048b5e908b76908b5ffc8e44fe8a46'
            'dca810741f8a46e4268807eb9fe976018b5e9083c302a8028cd8895e908ec08b5ffeebdaa82074098b46e426'
            '8907e97bffa84074108b46e49926890726895702e969ffeb1fa88074df8b46e499268907b10f2689570226d3'
            'fa2689570426895706e948ff8d4efa8d5eda8d56908d4692e83efa89c68b46e60346e80346ea0346ec0346ee'
            '0346f08956f62946e0f646dc0874198d5e92837ee6007e298a079889c28d46da43ffd7ff4ee6ebea807ef320'
            '75e1837ee0007edbba20008d46daffd7ff4ee0ebed837ee8007e0dba30008d46daffd7ff4ee8ebed8a46f23c'
            '737540f646dc40751a837eea007e218e46f6268a049889c28d46da46ffd7ff4eeaebe68d5eda8b56f689f989'
            'f0e8e3f8837eec007e2bba30008d46daffd7ff4eecebed3c5374dc837eea007ee38e46f6268a049889c28d46'
            'da46ffd7ff4eeaebe6837eee007e148e46f6268a049889c28d46da46ffd7ff4eeeebe6837ef0007e0dba3000'
            '8d46daffd7ff4ef0ebedf646dc087503e939fe837ee0007ef7ba20008d46daffd7ff4ee0ebed8b46e4e92bf8'
            '5389c385c0740df6c6ff740a8837b802008857015bc3b8010088175bc35589e551565783ec245289d9c646d6'
            '008d76d789d78d5ef8894ef833d2f737890789d38a87e80088048b46f84685c075e44e8a0488054784c075f6'
            '8b46d48d66fa5f5e595dc35689d683fb0a750485c07c07e8abff89f05ec3f7d842c6042debf19350920bc074'
            '02f7e2910bc07404f7e303c858f7e303d1c35352bb0100b8c6008cdae9c0f25589e5565783ec48538b760489'
            '76f88d76b5c646b4008976fa8b76068b5eb28976f68d76f68bfa85c074278bd08bc333db3b1472098bd88bc2'
            '33d2f73493f73491f73497f73489148bd08bc38bd98bcfeb2c85db74148bd38bc133db33c93b1472df8bc88b'
            'c233d2ebd48bd18bc733c933ff3b1472ce8bf88bc233d2ebc38b7ef689de895eb28a9de8008b7efaff46fa88'
            '1d85c0758385f67403e97cff85c975f985d275f5ff4efa8b76fa8a048b76f8ff46f8880484c075ec8b46048d'
            '66fc5f5e5dc204005589e5565783ec028b76048976fa837e060a750c85c07c15750685db750285c9ff7606ff'
            '76fae812ff89f0ebca31ff29d789fabf000019cf89f9bf000019df89fbbf000019c789f88d7c01c6042d897e'
            'faebcd535289c289c38a0784c0740f2c613c19760343ebf104418807ebf789d05a5bc35589e5565783ec2453'
            '51c646d8008d76d989df8b5ed4895efa8d5efa33c93b1772069287d1f73791f73789178bd18b5efa8a9fe800'
            '881c4685d275d785c075d34e8a0488054784c075f68b46d68d66fc5f5e5dc35689de83f90a750485d27c07e8'
            '9dff89f05ec3f7dac6072df7d883da0043ebec5389c38a9fd10280e30130e789d85bc331c0e957005352bb01'
            '0131c031d2a3fc01a3fa01b8d002e82eef5a5bc35589e553515250c746f80000eb0aff46f8817ef80001731f'
            '8a46f830e4509affff000085c074e78b5ef8c706fc010100c687d10201ebd78d66fa5a595b5dc353515289c2'
            'e8a1ff83faff743e83fafe743983fafd744983fafc7526bb1f00ba0100b85203e8c4eebb1d00ba0100b8b103'
            'e8b8eec706fc010100c706fa01a403eb1e85d27405b80100eb179affff00003d030a72f1e869ff9affff0000'
            'a3fa0131c05a595bc300450003010006030031000301f00601000100030131160300cf0003019d1603000300'
            '0301aa16040084000301300001007a0003016b00010027000301730002003400030183000100af0003019400'
            '0100af000301a60002002d000301c900020022000301d20002002d000301f500020022000301fe0002002d00'
            '030106010200450003010e0102004500030114010200440003012d010100ae00030133010100540003014a01'
            '0100ae00030150010100540003016a0101004800030177010200010003019b0102005f010301a90102000100'
            '0301bd010200d7010301cc0102005c000301f0010100c4000301070202002100030136020200210003014302'
            '0100280003015102010006000301650201006b000500a50201003a000200aa0201003a000301b6020100ae00'
            '0301c4020100ad000301cf02020057000301e402010039000301fe0201009401030110030100940105002303'
            '01000800020028030100080003013b030100390003015203020030000301710302004a000301770302004500'
            '03018403030053000301980303005e0103019e03030051000301ab03030055000301c00303005d010301cd03'
            '030051000301f80303004a000200150401003a0005001c0401003a0003015f04010029000301910401002900'
            '03019a0401002a000301a00401007c000301af0401006c000301bd04010071000301c7040100720003015d05'
            '03005b0003019c0503001e000301a305010005000200bd05010058050301ce05030003000000000000000000'
            '00000000000000000000000005000000000000000000000050494300434845434b0041505049434f4e004578'
            '74546578744f757400457874656e7400746162096865726500657874656e742025647825642c206661636520'
            '25730048414e44004f7752657300425554544f4e004d79427574746f6e004f575245532e544d500068756765'
            '20696f006f776e2025642c206269747320253032782530327825303278253032782c202573005265736f7572'
            '636573005375706572636c617373656400004f70656e20576174636f6d00466c6f6174696e672d706f696e74'
            '20737570706f7274206e6f74206c6f6164656400303132333435363738396162636465666768696a6b6c6d6e'
            '6f707172737475767778797a00007800f0000f000000ffff00000000000000000000000c0000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000003707370700000000000000000000'
            '00000000000000000000000000000000000000000000000000002c142c14000000000020da0400000020f515'
            '0000000000000000000000000000000000002800000030000000180000000100080000000000000000000000'
            '000000000000400000000000000000ff000010fb080020f7100030f3180040ef200050eb280060e7300070e3'
            '380080df400090db4800a0d75000b0d35800c0cf6000d0cb6800e0c77000f0c3780000bf800010bb880020b7'
            '900030b3980040afa00050aba80060a7b00070a3b800809fc000909bc800a097d000b093d800c08fe000d08b'
            'e800e087f000f083f800007f0000107b08002077100030731800406f2000506b28006067300070633800805f'
            '4000905b4800a0575000b0535800c04f6000d04b6800e0477000f0437800003f8000103b8800203790003033'
            '9800402fa000502ba8006027b0007023b800801fc000901bc800a017d000b013d800c00fe000d00be800e007'
            'f000f003f8001718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c'
            '3d3e3f00010203040506161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031323334353637'
            '38393a3b3c3d3e3f00010203040515161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132'
            '333435363738393a3b3c3d3e3f00010203041415161718191a1b1c1d1e1f202122232425262728292a2b2c2d'
            '2e2f303132333435363738393a3b3c3d3e3f00010203131415161718191a1b1c1d1e1f202122232425262728'
            '292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f00010212131415161718191a1b1c1d1e1f20212223'
            '2425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f00011112131415161718191a1b1c1d1e'
            '1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f0010111213141516171819'
            '1a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f0f1011121314'
            '15161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e0e0f'
            '101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b'
            '3c3d0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313233343536'
            '3738393a3b3c0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031'
            '32333435363738393a3b0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c'
            '2d2e2f303132333435363738393a0a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021222324252627'
            '28292a2b2c2d2e2f30313233343536373839090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122'
            '232425262728292a2b2c2d2e2f30313233343536373808090a0b0c0d0e0f101112131415161718191a1b1c1d'
            '1e1f202122232425262728292a2b2c2d2e2f30313233343536370708090a0b0c0d0e0f101112131415161718'
            '191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313233343536060708090a0b0c0d0e0f10111213'
            '1415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30313233343505060708090a0b0c0d0e'
            '0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f3031323334040506070809'
            '0a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132330304'
            '05060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f30'
            '313202030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b'
            '2c2d2e2f30310102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223242526'
            '2728292a2b2c2d2e2f30000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021'
            '22232425262728292a2b2c2d2e2f280000001000000008000000010001000000000000000000000000000000'
            '0000020000000000000000000000ffffff00f0f00000f0f00000f0f00000f0f000000f0f00000f0f00000f0f'
            '00000f0f00002800000020000000400000000100040000000000000000000000000000000000100000000000'
            '000000000000000080000080000000808000800000008000800080800000c0c0c000808080000000ff0000ff'
            '000000ffff00ff000000ff00ff00ffff0000ffffff0000000000000000000000000000000000000000000000'
            '9999999900000000000000000000099999999999999000000000000000009999999999999999000000000000'
            '0099999999999999999999000000000009999999999999999999999000000000999999999999999999999999'
            '0000000099999999999999999999999900000009999999999999999999999999900000999999999999999999'
            '9999999999000099999999cccccccccccc99999999000099999999cccccccccccc99999999000999999999cc'
            'cccccccccc99999999900999999999cccccccccccc99999999900999999999cccccccccccc99999999900999'
            '999999cccccccccccc99999999900999999999cccccccccccc99999999900999999999cccccccccccc999999'
            '99900999999999cccccccccccc99999999900999999999cccccccccccc99999999900099999999cccccccccc'
            'cc99999999000099999999cccccccccccc999999990000999999999999999999999999999900000999999999'
            '9999999999999999900000009999999999999999999999990000000099999999999999999999999900000000'
            '0999999999999999999999900000000000999999999999999999990000000000000099999999999999990000'
            '0000000000000999999999999990000000000000000000009999999900000000000000000000000000000000'
            '000000000000fffffffffff00fffff8001ffff0000fffc00003ff800001ff000000ff000000fe0000007c000'
            '0003c0000003c00000038000000180000001800000018000000180000001800000018000000180000001c000'
            '0003c0000003c0000003e0000007f000000ff000000ff800001ffc00003fff0000ffff8001fffff00fffffff'
            'ffff280000001000000020000000010004000000000000000000000000000000000010000000000000000000'
            '0000000080000080000000808000800000008000800080800000c0c0c000808080000000ff0000ff000000ff'
            'ff00ff000000ff00ff00ffff0000ffffff00000000000000000000000aaaaaa000000000aaaaaaaa0000000a'
            'aaaaaaaaa00000aaaaaaaaaaaa000aaaaaaaaaaaaaa00aaaaaaaaaaaaaa00aaaaaaaaaaaaaa00aaaaaaaaaaa'
            'aaa00aaaaaaaaaaaaaa00aaaaaaaaaaaaaa000aaaaaaaaaaaa00000aaaaaaaaaa0000000aaaaaaaa00000000'
            '0aaaaaa000000000000000000000ffff0000f81f0000f00f0000e0070000c003000080010000800100008001'
            '0000800100008001000080010000c0030000e0070000f00f0000f81f0000ffff000028000000200000004000'
            '0000010004000000000000000000000000000000000010000000000000000000000000008000008000000080'
            '8000800000008000800080800000c0c0c000808080000000ff0000ff000000ffff00ff000000ff00ff00ffff'
            '0000ffffff000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '000bb00000000000000000000000000000bbbb000000000000000000000000000bbbbbb00000000000000000'
            '00000000bbbbbbbb00000000000000000000000bbbbbbbbbb000000000000000000000bbbbbbbbbbbb000000'
            '0000000000000bbbbbbbbbbbbbb00000000000000000bbbbbbbbbbbbbbbb000000000000000bbbbbbbbbbbbb'
            'bbbbb0000000000000bbbbbbbbbbbbbbbbbbbb00000000000bbbbbbbbbbbbbbbbbbbbbb000000000bbbbbbbb'
            'bbbbbbbbbbbbbbbb0000000bbbbbbbbbbbbbbbbbbbbbbbbbb00000bbbbbbbbbbbbbbbbbbbbbbbbbbbb0000bb'
            'bbbbbbbbbbbbbbbbbbbbbbbbbb00000bbbbbbbbbbbbbbbbbbbbbbbbbb0000000bbbbbbbbbbbbbbbbbbbbbbbb'
            '000000000bbbbbbbbbbbbbbbbbbbbbb00000000000bbbbbbbbbbbbbbbbbbbb0000000000000bbbbbbbbbbbbb'
            'bbbbb000000000000000bbbbbbbbbbbbbbbb00000000000000000bbbbbbbbbbbbbb0000000000000000000bb'
            'bbbbbbbbbb000000000000000000000bbbbbbbbbb00000000000000000000000bbbbbbbb0000000000000000'
            '000000000bbbbbb000000000000000000000000000bbbb00000000000000000000000000000bb00000000000'
            '00000000000000000000000000000000000000000000000000000000000000000000fffffffffffffffffffe'
            '7ffffffc3ffffff81ffffff00fffffe007ffffc003ffff8001ffff0000fffe00007ffc00003ff800001ff000'
            '000fe0000007c0000003c0000003e0000007f000000ff800001ffc00003ffe00007fff0000ffff8001ffffc0'
            '03ffffe007fffff00ffffff81ffffffc3ffffffe7fffffffffffffffffff0000010002002020100001000400'
            'e8020000010010101000010004002801000002000000010001002020100001000400e802000003000f000f00'
            '2800000020000000400000000100010000000000000000000000000000000000020000000000000000000000'
            'ffffff0000000000000000000000000000010000000100000001000000010000000100000001000000010000'
            '0001000000010000000100000001000000010000000100001ffffff800010000000100000001000000010000'
            '0001000000010000000100000001000000010000000100000001000000010000000000000000000000000000'
            'fffffffffffffffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83fff'
            'fff83ffffff83ffffff83fffc0000003c0000003c0000003c0000003c0000003fff83ffffff83ffffff83fff'
            'fff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83ffffff83fffffffffffffffffff00000200'
            '01002000400001000100340100000400'
        )),
    'OWKERN': (
        # wcl -bt=windows -l=windows -ms owkern.c commdlg.lib shell.lib;
        # wrc owkern.rc owkern.exe, with owkern.rc:
        #     #include <windows.h>
        #     DATA RCDATA { "custom resource", "\0" }
        #     MAIN MENU
        #     BEGIN
        #         POPUP "&File"
        #         BEGIN
        #             MENUITEM "&Open...", 1
        #             MENUITEM "&Find...", 2
        #             MENUITEM "&About", 3
        #         END
        #     END
        #
        # #include <windows.h>
        # #include <commdlg.h>
        # /* not in Open Watcom's Win16 headers */
        # #define FINDMSGSTRING "commdlg_FindReplace"
        # int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);
        # #include <stdio.h>
        # #include <string.h>
        #
        # /* KERNEL and the modules around it: GetWinFlags, GetModuleHandle and
        #  * GetProcAddress (MessageBox called through it), LoadLibrary, Catch and
        #  * Throw, a resource of its own (RCDATA), GetTempFileName, a palette,
        #  * DPtoLP; COMMDLG's Open and Find dialogs, SHELL's ShellAbout. What each
        #  * gave in its window. */
        # typedef int (FAR PASCAL *MSGBOX)(HWND, LPCSTR, LPCSTR, UINT);
        # static char lines[6][96];
        # static UINT find_msg;
        # static FINDREPLACE fr;
        # static char what[64] = "needle";
        # static HWND find_dlg;
        # static CATCHBUF jump;
        #
        # static int deep(int n)
        # {
        #     if (n == 3)
        #         Throw(jump, 42);
        #     return deep(n + 1) + 1;
        # }
        #
        # long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        # {
        #     if (msg == find_msg) {
        #         LPFINDREPLACE f = (LPFINDREPLACE)lp;
        #         char text[32];
        #         _fstrncpy(text, f->lpstrFindWhat, sizeof(text) - 1);
        #         text[sizeof(text) - 1] = 0;
        #         sprintf(lines[5], "find '%s' %s%s", text,
        #                 f->Flags & FR_FINDNEXT ? "next" : "", f->Flags & FR_DIALOGTERM ? "end" : "");
        #         if (f->Flags & FR_DIALOGTERM)
        #             find_dlg = 0;
        #         InvalidateRect(h, NULL, TRUE);
        #         return 0;
        #     }
        #     switch (msg) {
        #     case WM_COMMAND:
        #         if (wp == 1) {
        #             OPENFILENAME ofn;
        #             char file[128] = "", title[32] = "";
        #             memset(&ofn, 0, sizeof(ofn));
        #             ofn.lStructSize = sizeof(ofn);
        #             ofn.hwndOwner = h;
        #             ofn.lpstrFilter = "Text (*.TXT)\0*.TXT\0All\0*.*\0";
        #             ofn.nFilterIndex = 1;
        #             ofn.lpstrFile = file;
        #             ofn.nMaxFile = sizeof(file);
        #             ofn.lpstrFileTitle = title;
        #             ofn.nMaxFileTitle = sizeof(title);
        #             ofn.Flags = OFN_FILEMUSTEXIST;
        #             if (GetOpenFileName(&ofn))
        #                 sprintf(lines[4], "open %s (%s) %u", file, title, ofn.nFileOffset);
        #             else
        #                 sprintf(lines[4], "open cancelled %lx", CommDlgExtendedError());
        #             InvalidateRect(h, NULL, TRUE);
        #         } else if (wp == 2 && !find_dlg) {
        #             memset(&fr, 0, sizeof(fr));
        #             fr.lStructSize = sizeof(fr);
        #             fr.hwndOwner = h;
        #             fr.lpstrFindWhat = what;
        #             fr.wFindWhatLen = sizeof(what);
        #             fr.Flags = FR_DOWN;
        #             find_dlg = FindText(&fr);
        #         } else if (wp == 3) {
        #             ShellAbout(h, "OwKern#OwKern", "KERNEL, COMMDLG, SHELL", LoadIcon(NULL, IDI_APPLICATION));
        #         }
        #         return 0;
        #     case WM_PAINT: {
        #         PAINTSTRUCT ps;
        #         HDC dc = BeginPaint(h, &ps);
        #         int i;
        #         for (i = 0; i < 6; i++)
        #             TextOut(dc, 4, 4 + 18 * i, lines[i], strlen(lines[i]));
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
        #     WNDCLASS wc;
        #     MSG msg;
        #     HWND w;
        #     HMODULE user = GetModuleHandle("USER");
        #     MSGBOX box = (MSGBOX)GetProcAddress(user, "MessageBox");
        #     HINSTANCE dlg = LoadLibrary("COMMDLG.DLL");
        #     HRSRC rs = FindResource(hInstance, "DATA", RT_RCDATA);
        #     HGLOBAL rg = LoadResource(hInstance, rs);
        #     LPSTR rp = LockResource(rg);
        #     char tmp[144], *base, rtext[32];
        #     int caught;
        #     PALETTEENTRY pe[2] = { { 10, 20, 30, 0 }, { 200, 100, 50, 0 } }, back[2];
        #     struct { WORD v, n; PALETTEENTRY e[2]; } lp = { 0x300, 2 };
        #     HPALETTE pal;
        #     POINT pt;
        #     HDC dc;
        #     OFSTRUCT of;
        #
        #     sprintf(lines[0], "flags %lx, user %x, box %d, dlg %d", GetWinFlags(), user != 0,
        #             box ? box(NULL, "through GetProcAddress", "OwKern", MB_OK) : -1, dlg >= 32);
        #     caught = Catch(jump);
        #     if (!caught)
        #         deep(0);
        #     GetTempFileName(0, "owk", 0, tmp);
        #     base = strrchr(tmp, '\\');
        #     _fstrncpy(rtext, rp ? rp : "-", sizeof(rtext) - 1);
        #     rtext[sizeof(rtext) - 1] = 0;
        #     sprintf(lines[1], "caught %d, %s, %lu bytes, temp %.4s", caught, rtext,
        #             SizeofResource(hInstance, rs), base ? base + 1 : tmp);
        #     OpenFile(tmp, &of, OF_DELETE);
        #     FreeResource(rg);
        #     memcpy(lp.e, pe, sizeof(pe));
        #     pal = CreatePalette((LOGPALETTE FAR *)&lp);
        #     memset(back, 0, sizeof(back));
        #     GetPaletteEntries(pal, 0, 2, back);
        #     DeleteObject(pal);
        #     dc = GetDC(NULL);
        #     SetWindowOrg(dc, 10, 20);
        #     pt.x = 5;
        #     pt.y = 5;
        #     DPtoLP(dc, &pt, 1);
        #     ReleaseDC(NULL, dc);
        #     sprintf(lines[2], "palette %d,%d,%d %d, dp->lp %d,%d, mode %d", back[1].peRed, back[1].peGreen,
        #             back[1].peBlue, back[0].peBlue, pt.x, pt.y, GetMapMode(GetDC(NULL)));
        #     sprintf(lines[3], "error mode %u", SetErrorMode(1));
        #     find_msg = RegisterWindowMessage(FINDMSGSTRING);
        #     memset(&wc, 0, sizeof(wc));
        #     wc.lpfnWndProc = WndProc;
        #     wc.hInstance = hInstance;
        #     wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #     wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #     wc.lpszMenuName = "MAIN";
        #     wc.lpszClassName = "OwKern";
        #     RegisterClass(&wc);
        #     w = CreateWindow("OwKern", "Kernel", WS_OVERLAPPEDWINDOW, 20, 40, 440, 190, NULL, NULL,
        #                      hInstance, NULL);
        #     ShowWindow(w, nCmdShow);
        #     UpdateWindow(w);
        #     while (GetMessage(&msg, NULL, 0, 0)) {
        #         if (find_dlg && IsDialogMessage(find_dlg, &msg))
        #             continue;
        #         TranslateMessage(&msg);
        #         DispatchMessage(&msg);
        #     }
        #     if (dlg >= 32)
        #         FreeLibrary(dlg);
        #     return msg.wParam;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            'c4000200000000000203020000040020fa06010000000200020006000e0040005000860090009c0046010000'
            '0000010002000208aa000a0f00000a03aa005a18400d5a18cc0db003410c300701000a80010000000000ac0f'
            '080030002c00000000000480010000000000b40f1900301031000000000000000444415441044d41494e064f'
            '574b45524e000000010008000d0015001b001f0000064b45524e454c045553455207434f4d4d444c47055348'
            '454c4c03474449084b4559424f41524400000a6f776b65726e2e657865000000ccebfd909090900050b81000'
            'e8fe05585351525689c63d0300750e1eb8140650b82a00509affff00008d4401e8d9ff405e5a595bc31e5890'
            '455589e51e8ed8b84201e8c805565781ec2a018b76068b5e088b560a8b460c3b062806754189f7895ef8b81f'
            '00508ec3268b5c0c268b4c0e8cda8d4698e8c705c646b7008e46f826f64408407405b81000eb03b81400508e'
            '46f826f64508087407b81500eb05eb3cb81400508d469850b81a0050b8900550e8c10583c40a8e46f826f645'
            '0840740531c0a32606ff760e31c05050b80100509affff000031c031d2e9e5013d110174133d0f0074083d02'
            '007406e9c701e96501e9b60183fa017403e9dc00b940008cd88ec08dbed0febe0602f3a5b910008d7eb8be86'
            '02f3a5bb480031d28d8650ffe86605c78650ff4800c78652ff00008b460e898654ffc78658ff29008c9e5aff'
            'c78664ff0100c78666ff00008d96d0fe899668ff8c9e6affc7866cff8000c7866eff00008d56b8899670ff8c'
            '9e72ffc78674ff2000c78676ff0000c746800010c7468200008d9650ff1e529affff000085c0741cff76848d'
            '46b8508d86d0fe50b8450050b8300550e8c90483c40aeb159affff00005250b8550050b8300550e8b20483c4'
            '08ff760e31c05050b80100509affff0000e9fdfe83fa02754e833e2606007547bb240031d2b8f005e89a04c7'
            '06f005240031c0a3f2058b460ea3f405c706fc05c6018c1efe05c70604064000c706f805010031c0a3fa051e'
            'b8f005509affff0000a32606e9aafe83fa037403e9a2feff760e1eb87f00501eb868005031c05050b8007f50'
            '9affff0000509affff0000e97ffeff760e8d56d81e529affff000089c731f657b8040050b10389f0d3e001f0'
            'd1e00504005089f0d1e0d1e029f0b105d3e005b0031e50e81604509affff00004683fe067ccdff760e8d56d8'
            '1e529affff0000e92bfe31c0509affff0000e920feff760e505253569affff00008d66fa5f5e1f5d4dca0a00'
            '5589e5b8bc01e83803565781ec96011eb88d00509affff00008946f2501eb89200509affff00008946ea8956'
            'ec1eb89d00509affff00008946ee8946f6ff760c1eb8a9005031c050b80a00509affff00008946f0ff760c50'
            '9affff00008946f4509affff00008946f88956fa8cd88ec08d7edebea602a5a5a5a58d7ecabeae02a5a5a5a5'
            'a5a5837eee207205b80100eb0231c050837eec007506837eea00741531c0501eb8b500501eb8ae005031c050'
            'ff5eeaeb03b8ffff508b46f285c07403b80100509affff00005250b8cc0050b8b00350e8d20283c40e1eb814'
            '06509affff000089c785c07503e850fc31c0501eb8ef005031c0508d9666fe1e529affff0000ba5c008d8666'
            'fee8e30289c6b81f0050837efa007506837ef80074088b5ef88b4efaeb05bbf3008cd98cda8d867effe83f02'
            'c6469d0085f674058d4401eb048d8666fe50ff760cff76f09affff000052508d867eff5057b8f50050b81004'
            '50e8440283c40e8d9666fe1e528d96f6fe1e52b80002509affff0000ff76f49affff0000bb08008d56de8d46'
            'cee881028d56ca1e529affff000089c6bb080031d28d46d6e816025631c050b80200508d56d61e529affff00'
            '00569affff000031c0509affff000089c650b80a0050b81400509affff0000b805008946e68946e8568d56e6'
            '1e52b80100509affff000031c050569affff000031c0509affff0000509affff000050ff76e8ff76e68a46d8'
            '30e4508a46dc508a46db508a46da50b8190150b8700450e87e0183c412b80100509affff000050b8440150b8'
            'd00450e8660183c4061eb85201509affff0000a32806bb1a0031d28d469ee86001c746a0ffffc746a2ffff8b'
            '460c8946a831c05050b8007f509affff00008946ac31c0509affff00008946aec746b066018c5eb2c746b4ae'
            '008c5eb68d569e1e529affff00001eb8ae00501eb86b0150b8cf005031c050b8140050b8280050b8b80150b8'
            'be005031c05050ff760c50509affff000089c650ff76049affff0000569affff00008d56b81e5231c0505050'
            '9affff000085c0742ca1260685c0740f508d56b81e529affff000085c075d78d56b81e529affff00008d56b8'
            '1e529affff0000ebc18b46f63d20007206509affff00008b46bc8d66fc5f5e5dc20a00008c16ba02c33bc473'
            '132bc4f7d83b060a0077068cd03b06ba027401c30e5ab82f06bb0100e96c02537461636b204f766572666c6f'
            '7721005589e5565789c789ce8b4e0489d885c9750889f85f5e5dc202008ec21e968ed857acaa84c0e0fa33c0'
            'd1e9f3ab13c9f3aa581febdf5589e553528b46048d5e088b5606e841025a5b5dc3515789c789d988d0571e07'
            '8ae0d1e9f3ab13c9f3aa5f89f85f59c3515789c78cd88ec083c9ff33c0f2aef7d14989c85f59c3515789c78c'
            'd88ec0b9ffff32c0f2aef7d14f8ac2fdf2aefc75038bcf4189c85f59c351565789d989d689c7578cd88ec0d1'
            'e9f3a513c9f3a45f89f85f5e59c3eb0203009affff00000bc0750e5033c0ba0f00e88e0258b44ccd218c06c0'
            '02575606535253be2b078936db028c1edd02268a078804464384c075f589360a005b2bc0509affff00005757'
            '9affff0000fc1e07b92b07bfb0032bcf32c0f3aab84408a3d702b8ffffa3d902b430cd21a2c2028826c3029a'
            'ffff0000a2c4028826c50286e0a3c60254583bc475120f01e0a90100740ab001a2c802b003a2c9021ebfe302'
            '57b89000509affff0000b8e302893edf028c1ee102b8ff00e88f01e812fbe91d014f70656e20576174636f6d'
            '20432f432b2b31362052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e'
            '20576174636f6d20436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f70'
            '79726967687420284329205379626173652c20496e632e20313938382d323030322e00c3cb005589e5515657'
            '81ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743'
            'ebe031c08ec15026c607001e8d9e7aff531eb872015031c0509affff000089f8e96800515689c189d6e85701'
            '85c0750689c889f2eb9889d8e95000535689c38b378d440189078814ff470a5e5bc3515689c6b9b308e8640a'
            '01c6c604005e59c3c3c3535289c3833e9a03007404ff169a03ff167403baff00b81000e8a800ff167403ff16'
            '760389d8e904fe5289c2ff167403ff16760389d0e9f4fd5589e5535289c3833f0075078d66fc5a5b5dc31eff'
            '171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee8effe'
            'bfb00389fabba40389fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d44'
            '02803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee898fe88'
            'd5bfb00389fabbb00389fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a'
            '6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803e780300750331c0'
            'c35250cceb0657564944454fb8010083c404c3535689c689d3c7470200008a043c2d740b3c23750d804f0201'
            '46ebef804f0208ebf73c2b750a804f0204806702fdebe93c20750cf647020475df804f0202ebd93c30750588'
            '4719ebd089f05e5bc351565789c689d189dac6471920e8a2ff89c7c74706000089c6803d2a74228a043c307c'
            '2e3c397f2aba0a008b4706f7ea89c28a04982d300001c246895706ebde89cf8305028b3d8b45fe89470685c0'
            '7c3846c74708ffff803c2e754846c747080000803c2a742b8a043c307c373c397f33ba0a008b4708f7ea89c2'
            '8a04982d300001c246895708ebdef75f06804f0208ebbf89cf8305028b3d8b45fe89470885c07c21468a048d'
            '54013c68731e3c4c73573c497555807c01367536807c02347530e97700c74708ffffebd88d7c02770e807c01'
            '68755089fe804f0210eb133c7473153c6c75263a4401751689fe804f028089f05f5e59c3764f3c7a744b3c77'
            '75f046804f0240ebe9eb0aeb1c3c6a75e189d6ebd9762a3c5774123c4e75d389d6804f0302ebcbeb0b3c4675'
            'c546804f0304ebbe89d6804f0220ebb683c603ebad89d6804f0301eba946eba6565789c689d789da8ec731c0'
            '39d0740c89f34626803f00740340ebf05f5ec35589e551565783ec0289de89c389d731c983feff75278ec726'
            '8b0785c0741489c28d46f883c302e83f093dffff74e701c1ebe389c88d66fa5f5e595dc38ec7268b0785c074'
            '1839f17f1489c28d46f883c302e814093dffff74e301c1ebdf39f17ed189f0ebcf5589e5515683ec0a5289d9'
            'bb10008d56f2e852098d46f28b5ef0e852fa89c601cb89c889f24e85d27e0f89c24b4885d27e078a52f28817'
            'ebea8b76f001c689c24e4885d27e05c60430ebf38b5ef001cbc607008d66fce97aff5589e5565783ec0889c6'
            '8956f8895ef6894efa85c97c288b5ef8837f08ff7505c747080400bb0a008b46fa89f2e8dd088976f48d4401'
            '803c00741489c6ebf4f75efac6042df75ef6835efa0046ebc88b5ef8837f08007435c6042e31ff89c68b5ef8'
            '3b7f087d238b46f6bb0a0031d231c9c746fa0000e8af088956fa8946f68a46fa043047880446ebd5c60400f6'
            '46f78074423b76f474114e803c2e75014e803c397538c60430ebeac60431468a048d7c013c30750489feebf3'
            '3c2e7510c604308d75018805803c30750346ebf8c6043046c604008d66fc5f5e5dc3fe04ebf55389c3f64702'
            '087522807f1930751c8b47062b470c2b470e2b47102b47122b47142b471685c07e0301470e5bc35589e55657'
            '83ec0689c78956fa894ef8837f10007eb28e46fa8d46f6268b15e87b0783c70289c13dffff74e43b47107f19'
            '8d76f64983f9ff74d68a049889c289d846ff56f8ff4f10ebeac747100000e976ffb60e1f1301131f13b60e47'
            '10b60e1f131f131f131f131f131f131f131f133e121f131f138a101f131f131f131f1307111f131f131f131f'
            '131f131f131f131f13b60e9f11b3125411b60e4710b60e1f1354111f131f131f131f131f13a6113e121f131f'
            '138a101f1354111f131f1307115589e5565783ec125089d789dec7471600008946f48b471689471489471289'
            '471089470e89470c8a47188c5ef63c6973393c64756a8a4402a88074618305088b1d8b47fc8946f28b47fa89'
            '46f08b47f88b4ffe8946eee9e5008b46e889f389faff46f4ff169c03e9b50176c93c7875238a4402a8807430'
            '8305088b1d8b47fc8946f28b47fa8946f08b47f88b4ffe8946eeeb293c7574d93c6f74d5eb1feb653c6274cd'
            '3c58ebf2a840742a8305048b1d8b47fc8946f88b47fe8946fa8a44182c41c746ec0a003c37775130e489c3d1'
            'e32effa7ed0d8305028b1d8b47fec746fa00008946f8f64402207407c746fa0000ebcaf644021074c48a46f8'
            '30e48946f830c0ebb5a84074168305048b1d8b47fc8946f88b47fe8946faeb23e9a4038305028b1d8b47fe99'
            '8946f88a44028956faa82074618b46f8998946f88956fa30c0f6440280745985c97c10750a837ef200750483'
            '7ef00084c0745a8b440c89c28b5ee84201c389540cc6072df6440280743631c02b46ee8946eeb800001b46f0'
            '8946f0b800001b46f28946f2b8000019c889c1e927ffa81074a58a46f898eb98837efa007cb5ebaff75efaf7'
            '5ef8835efa00e908ff8a4402a80474148b440c89c28b5ee84201c389540cc6072be9edfea8027503e9e6fe8b'
            '440c89c28b5ee84201c389540cc60720e9d2fef64402207503e966fe8305048b1d8b47fc89f28946f88b47fe'
            '8b5ef88946fa89c18b46e8e820fcbbffff8b46e88cdae837fb89441089f08b56f6e8eafc8b46f4e9d9fc8b5e'
            'e8c607008a4403a804753fa8028305028b1d8b5ffe85db74068c5ef6895ef4807c1853754ef6440220743a8b'
            '46f48b56f68b5c08e8edfa894410837c08007cb03b44087eab8b4408eba38305048b1d8b47fe8b7ffc85c075'
            '0485ff74c2897ef48946f6ebba8b46f48b56f68b5c08e8d2faebc4f644024075ecebb08a4402a8017441a880'
            '7503e9820085c97512837ef200750c837ef0007506837eee0074248b440c89c28b5ee84201c389540cc60730'
            '8b440c8b5ee889c201c3428a441889540c8807c746ec1000837c08ff7404c64419208b5ee8035c0c8c5ef689'
            '5ef4f6440280743d837c0800753985c97535837ef200752f837ef0007529837eee0075238e46f631c026c607'
            '00eb488b46fa0b46f8eb8ac746ec0200ebaec746ec0800eba7eb5cff76ec8b46e88b5ef203440c8b56ee5089'
            'c88b4ef0e81404807c185875068b46e8e82005bbffff8b46f48b56f6e8d1f989441089c23b44087c4af64402'
            '017503e98afe837eec0a7df785c07409c45ef426803f3074eaff440ee971fe837c0800750e8b46fa0b46f875'
            '068e46f6e972ff8b5ee88b4eec8b46f88b56fa035c0ce8e204eb948b440829d089440ee93efe837c0600750b'
            'f64403047458c744060900806402f98305028b1d8b4ffef6440304744883c302891d8b56e88b47febb0400e8'
            'c3f98b5ee889c88d5705c647043abb0400e8b1f9807c185075068b46e8e86304bbffff8b46f48b56f6e814f9'
            '89440ce9dafdc744060400eba6bb04008b56e889c8ebcec744100100f644024075108305028b1d8a47fe8b5e'
            'e88807e9aefd8305028b1d8d46ea8b57fee85c023dffff7503e998fd8b5ee88a46ea880730e4e8960485c074'
            'ec8a46eb884701ff440ce97bfd8305028b1d8b46e88b57fee829023dffff7403e962fdc744100000e95dfdc7'
            '440600008b5ee88a44188807c7440c0100e948fd5589e5565783ec6a5389cfc646fa00c746dc0000c746dec8'
            '00c746e400008946da8956f88b5ef88a0784c074588d4f013c25740d9889c28d46da894ef8ffd7ebe38d5eda'
            '8d569089c8e8e5f689c38a07438846f2895ef884c0742a3c6e755c8a46dda8047422834690048b5e908b7690'
            '8b5ffc8e44fe8a46dca810741f8a46e4268807eb9fe976018b5e9083c302a8028cd8895e908ec08b5ffeebda'
            'a82074098b46e4268907e97bffa84074108b46e49926890726895702e969ffeb1fa88074df8b46e499268907'
            'b10f2689570226d3fa2689570426895706e948ff8d4efa8d5eda8d56908d4692e83efa89c68b46e60346e803'
            '46ea0346ec0346ee0346f08956f62946e0f646dc0874198d5e92837ee6007e298a079889c28d46da43ffd7ff'
            '4ee6ebea807ef32075e1837ee0007edbba20008d46daffd7ff4ee0ebed837ee8007e0dba30008d46daffd7ff'
            '4ee8ebed8a46f23c737540f646dc40751a837eea007e218e46f6268a049889c28d46da46ffd7ff4eeaebe68d'
            '5eda8b56f689f989f0e8e3f8837eec007e2bba30008d46daffd7ff4eecebed3c5374dc837eea007ee38e46f6'
            '268a049889c28d46da46ffd7ff4eeaebe6837eee007e148e46f6268a049889c28d46da46ffd7ff4eeeebe683'
            '7ef0007e0dba30008d46daffd7ff4ef0ebedf646dc087503e939fe837ee0007ef7ba20008d46daffd7ff4ee0'
            'ebed8b46e4e92bf85389c385c0740df6c6ff740a8837b802008857015bc3b8010088175bc35589e551565783'
            'ec245289d9c646d6008d76d789d78d5ef8894ef833d2f737890789d38a87a00188048b46f84685c075e44e8a'
            '0488054784c075f68b46d48d66fa5f5e595dc35689d683fb0a750485c07c07e8abff89f05ec3f7d842c6042d'
            'ebf19350920bc07402f7e2910bc07404f7e303c858f7e303d1c35352bb0100b87e018cdae9c0f25589e55657'
            '83ec48538b76048976f88d76b5c646b4008976fa8b76068b5eb28976f68d76f68bfa85c074278bd08bc333db'
            '3b1472098bd88bc233d2f73493f73491f73497f73489148bd08bc38bd98bcfeb2c85db74148bd38bc133db33'
            'c93b1472df8bc88bc233d2ebd48bd18bc733c933ff3b1472ce8bf88bc233d2ebc38b7ef689de895eb28a9da0'
            '018b7efaff46fa881d85c0758385f67403e97cff85c975f985d275f5ff4efa8b76fa8a048b76f8ff46f88804'
            '84c075ec8b46048d66fc5f5e5dc204005589e5565783ec028b76048976fa837e060a750c85c07c15750685db'
            '750285c9ff7606ff76fae812ff89f0ebca31ff29d789fabf000019cf89f9bf000019df89fbbf000019c789f8'
            '8d7c01c6042d897efaebcd535289c289c38a0784c0740f2c613c19760343ebf104418807ebf789d05a5bc355'
            '89e5565783ec245351c646d8008d76d989df8b5ed4895efa8d5efa33c93b1772069287d1f73791f73789178b'
            'd18b5efa8a9fa001881c4685d275d785c075d34e8a0488054784c075f68b46d68d66fc5f5e5dc35689de83f9'
            '0a750485d27c07e89dff89f05ec3f7dac6072df7d883da0043ebec5389c38a9f2b0680e30130e789d85bc331'
            'c0e957005352bb010131c031d2a3a203a3a003b82a06e8d8ee5a5bc35589e553515250c746f80000eb0aff46'
            'f8817ef80001731f8a46f830e4509affff000085c074e78b5ef8c706a2030100c6872b0601ebd78d66fa5a59'
            '5b5dc353515289c2e8a1ff83faff743e83fafe743983fafd744983fafc7526bb1f00ba0100b8ac06e86eeebb'
            '1d00ba0100b80b07e862eec706a2030100c706a003a403eb1e85d27405b80100eb179affff00003d030a72f1'
            'e869ff9affff0000a3a00331c05a595bc3003b0003012500010038000301d10002007d0003018c0103000100'
            '0301b10103001a000301d10102007d000301210203000b00030149020200ae0003014f020400160003015f02'
            '0200270003019002050021000301a302020028000301ae02020006000301bd0202006b000301e10201002f00'
            '0301ef02010032000301ff0201005f000301190301003c000301250301003d0003012e0301003e0003019103'
            '010084000301ab03010037000301ca030100610003011904010041000301440401004a0003014c0401003f00'
            '03016204050068010301810405006b01030187040500450003018f040200420003019f0405000b000301b704'
            '050043000301c004020044000301c804020042000301ce04050051000301fe0401006b000301170502007600'
            '05002c05010035000200310501003500030142050200ad0003014d050500570003016a050200390003019905'
            '020029000301a40502002a000301aa0502007c000301b90502006c000301cf0502005a000301dd0502007100'
            '0301e705020072000301f705010060000301ff0601005b0003013e0701001e00030145070200050002005f07'
            '0100fa0603017007010003000301a2070100310003019208020001000301d3170100cf0003013f1801000300'
            '03014c18060084000000000000000000000000000000000000000000050000000000000000000000656e6400'
            '006e6578740066696e6420272573272025732573005465787420282a2e54585429002a2e54585400416c6c00'
            '2a2e2a00006f70656e2025732028257329202575006f70656e2063616e63656c6c656420256c78004b45524e'
            '454c2c20434f4d4d444c472c205348454c4c004f774b65726e234f774b65726e0055534552004d6573736167'
            '65426f7800434f4d4d444c472e444c4c0044415441004f774b65726e007468726f7567682047657450726f63'
            '4164647265737300666c61677320256c782c20757365722025782c20626f782025642c20646c67202564006f'
            '776b002d006361756768742025642c2025732c20256c752062797465732c2074656d7020252e34730070616c'
            '657474652025642c25642c25642025642c2064702d3e6c702025642c25642c206d6f6465202564006572726f'
            '72206d6f646520257500636f6d6d646c675f46696e645265706c616365004d41494e004b65726e656c004f70'
            '656e20576174636f6d00466c6f6174696e672d706f696e7420737570706f7274206e6f74206c6f6164656400'
            '303132333435363738396162636465666768696a6b6c6d6e6f707172737475767778797a00006e6565646c65'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000a14'
            '1e00c86432000003020000000000000000000000ffff00000000000000000000000c00000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000000000000000000000000000000000d908d908000000000000000000000000'
            '000000000000000000000000000000000000000000000000ce15ce1500000000002008060000002097170000'
            '00000000000000000000000000000000637573746f6d207265736f75726365000000000090002646696c6500'
            '00000100264f70656e2e2e2e00000002002646696e642e2e2e00800003002641626f75740000'
        )),
    'OWDLL2': (
        # wcl -bt=windows -l=windows_dll -bd -zu -ms owdll2.c; wlib -n owdll2.lib +owdll2.dll
        # owdll2.c:
        #     #include <windows.h>
        #
        #     /* the DLL OWDLL needs: started before it */
        #     static int ready;
        #
        #     int FAR PASCAL LibMain(HINSTANCE inst, WORD ds, WORD heap, LPSTR cmd)
        #     {
        #         ready = 7;
        #         return 1;
        #     }
        #
        #     int FAR PASCAL __export Dll2Ready(void)
        #     {
        #         return ready;
        #     }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742064796e616d6963206c696e6b206c6962726172790d0a2400004e450501'
            '77000a000000000001830200000400000000010000000000020002000e0040005000500066006a0001010000'
            '0100010000000200000000000000000388006c02700d6c02cb015200710c5400064f57444c4c32000009444c'
            '4c3252454144590100000100080000064b45524e454c045553455201ff03cd3f01a60000000a6f77646c6c32'
            '2e646c6c000000008cd89045558bec1e8ed8571e510656e31533c01e50519affff00000bc075075e07591f5f'
            'eb4a810e34000020810e380000209affff0000a218008826190086e0a31a0054583bc475120f01e0a9010074'
            '0ab001a21c00b003a21d00b88500a32b008c0e2d00b8ff00e885000e3ee817008d66fe1f5d4dcb5033c0ba0f'
            '00e8c80058b44ccd21cbc31e5890455589e51e8ed8b80800e81401c70652000700b801001f5d4dca0a001e58'
            '90455589e51e8ed8b80800e8f500a152001f5d4dcb5589e5535289c3833f0075078d66fc5a5b5dc31eff171f'
            'ebf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee888ffbf46'
            '0089fabb400089fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d440280'
            '3c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee831ff88d5bf'
            '460089fabb460089fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c01'
            '7305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f008c163c00c33bc473132b'
            'c4f7d83b060a0077068cd03b063c007401c30e5ab8cd01bb0100e96600537461636b204f766572666c6f7721'
            '00005589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a'
            '048ec14626880743ebe031c08ec15026c607001e8d9e7aff531eb846005031c0509affff000089f8e9190051'
            '5689c189d6e8130085c0750689c889f2eb9889d8e9010000e9fdff803e3e0000750331c0c35250cceb065756'
            '4944454fb8010083c404c30003000301170001000400030133000100030003012a0202000100000000000500'
            '00000000000000000000ffff00000000000000000000000c0000000000000000000000000000000000000000'
            '00000200000002000000000000000020a60100004f70656e20576174636f6d00'
        )),
    'OWDLL': (
        # wcl -bt=windows -l=windows_dll -bd -zu -ms owdll.c owdll2.lib; wrc owdll.rc owdll.dll;
        # wlib -n owdll.lib +owdll.dll
        # owdll.c:
        #     #include <windows.h>
        #     #include <string.h>
        #
        #     /* A DLL of a program's: its own data (a counter, its instance), its local
        #      * heap (LocalAlloc in its DGROUP), its resources (a string), a window
        #      * class with its procedure here, and OWDLL2, which it needs started first.
        #      * Its stack is the program's (SS != DS: -zu). */
        #     int FAR PASCAL Dll2Ready(void);
        #
        #     static HINSTANCE self;
        #     static int calls, seen2;
        #
        #     int FAR PASCAL LibMain(HINSTANCE inst, WORD ds, WORD heap, LPSTR cmd)
        #     {
        #         self = inst;
        #         seen2 = Dll2Ready();
        #         return 1;
        #     }
        #
        #     int FAR PASCAL __export DllAdd(int a, int b)
        #     {
        #         calls++;
        #         return a + b;
        #     }
        #
        #     int FAR PASCAL __export DllCalls(void)
        #     {
        #         return calls * 100 + seen2;
        #     }
        #
        #     int FAR PASCAL __export DllString(LPSTR buf, int n)
        #     {
        #         return LoadString(self, 1, buf, n);
        #     }
        #
        #     LPSTR FAR PASCAL __export DllAlloc(void)
        #     {
        #         HLOCAL h = LocalAlloc(LMEM_FIXED, 32);
        #         char *p = (char *)LocalLock(h);
        #         if (!p)
        #             return NULL;
        #         strcpy(p, "from its heap");
        #         return (LPSTR)p;
        #     }
        #
        #     long FAR PASCAL __export DllWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_PAINT) {
        #             PAINTSTRUCT ps;
        #             HDC dc = BeginPaint(h, &ps);
        #             char text[48];
        #             LoadString(self, 2, text, sizeof(text));
        #             TextOut(dc, 2, 2, text, lstrlen(text));
        #             EndPaint(h, &ps);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     BOOL FAR PASCAL __export DllRegister(void)
        #     {
        #         WNDCLASS wc;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = DllWndProc;
        #         wc.hInstance = self;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(LTGRAY_BRUSH);
        #         wc.lpszClassName = "DllWindow";
        #         return RegisterClass(&wc);
        #     }
        # owdll.rc:
        #     STRINGTABLE
        #     BEGIN
        #         1, "a string of the DLL's"
        #         2, "painted by the DLL"
        #     END
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742064796e616d6963206c696e6b206c6962726172790d0a2400004e450501'
            'e10028000000000001830200000400000000010000000000020004000d00400050006800b700bf0089010000'
            '0600010001000208cb00b50200000a03cb004e04700d4e0443036a00710c7000010006800100000000008003'
            '1c0030100180000000000000054f57444c4c000006444c4c414444010008444c4c43414c4c53020009444c4c'
            '535452494e47030008444c4c414c4c4f4304000a444c4c574e4450524f4305000b444c4c5245474953544552'
            '0600000b00120019001e000009444c4c325245414459064b45524e454c064f57444c4c320455534552034744'
            '4906ff13cd3f01ae0003cd3f01ce001bcd3f01fc0003cd3f0128012bcd3f01600103cd3f01e4010000096f77'
            '646c6c2e646c6c0000008cd89045558bec1e8ed8571e510656e31533c01e50519affff00000bc075075e0759'
            '1f5feb4a810e34000020810e380000209affff0000a218008826190086e0a31a0054583bc475120f01e0a901'
            '00740ab001a21c00b003a21d00b88500a32b008c0e2d00b8ff00e810020e3ee817008d66fe1f5d4dcb5033c0'
            'ba0f00e8530258b44ccd21cbc31e5890455589e51e8ed8b80800e89e028b460ea36e009affff0000a36a00b8'
            '01001f5d4dca0a001e5890455589e51e8ed8b80800e87702ff066c008b46080346061f5d4dca04001e589045'
            '5589e51e8ed8b80800e857028b166c0089d0d1e0d1e029d0b103d3e001d0d1e0d1e003066a001f5d4dcb1e58'
            '90455589e51e8ed8b81200e82902ff366e00b8010050ff760aff7608ff76069affff00001f5d4dca06001e58'
            '90455589e51e8ed8b80c00e8fd0131c050b82000509affff0000509affff000089c385c0750431d2eb08ba46'
            '00e80d028cda89d8eb981e5890455589e51e8ed8b86800e8c501565783ec508b460c8b7e0e3d0f00754c578d'
            '56da16529affff000089c6ff366e00b80200508d56aa1652b83000509affff000056b8020050508d56aa1652'
            '16529affff0000509affff0000578d56da16529affff000031c031d2eb105750ff760aff7608ff76069affff'
            '00008d66fa5f5ee9c4fe1e5890455589e51e8ed8b82800e8410183ec1ab91a0031db8cd28d46e4e8a401c746'
            'e6ffffc746e8ffffa16e008946ee31c05050b8007f509affff00008946f2b80100509affff00008946f4c746'
            'fa54008c5efc8d56e416529affff00008d66fee9b0fe5589e5535289c3833f0075078d66fc5a5b5dc31eff17'
            '1febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee8fdfdbf'
            '460089fabb400089fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402'
            '803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee8a6fd88d5'
            'bf460089fabb460089fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c'
            '017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f8c163c00c33bc473132b'
            'c4f7d83b060a0077068cd03b063c007401c30e5ab85703bb0100e9be00537461636b204f766572666c6f7721'
            '00565789d689c757f7c601007408ac8805473c0074268b0484c0741e890583c70284e474178b440284c0740e'
            '890583c60483c70284e475de740288055f89f85f5ec35789c788d88ec2578ae0d1e9f3ab13c9f3aa5f89f85f'
            'c3005589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a'
            '048ec14626880743ebe031c08ec15026c607001e8d9e7aff531eb85e005031c0509affff000089f8e9190051'
            '5689c189d6e8130085c0750689c889f2eb9889d8e9010000e9fdff803e3e0000750331c0c35250cceb065756'
            '4944454fb8010083c404c30012000301170001000400030133000100030003029e000200010003011e010300'
            'b00003014001010005000301460101000800030187010300270003019f010300b0000301b10101005a000301'
            'b701040021000301c201030028000301d80103006b0005000702ff00050002000c02ff00050003011d020300'
            'ad00030129020400570003013e020300390003010c0403000100000000000000000000000000000000000000'
            '0000050000000000000000000000ffff00000000000000000000000c00000000000000000000000000000000'
            '00000000000002000000020000000000000000203003000066726f6d20697473206865617000446c6c57696e'
            '646f77004f70656e20576174636f6d000000000000000000000000000000000000156120737472696e67206f'
            '662074686520444c4c2773127061696e7465642062792074686520444c4c0000000000000000000000000000'
        )),
    'OWUSE': (
        # wcl -bt=windows -l=windows -ms owuse.c owdll.lib
        # owuse.c:
        #     #include <windows.h>
        #     #include <stdio.h>
        #     #include <string.h>
        #
        #     /* A program and its DLLs: OWDLL (imported: by name, by ordinal through
        #      * GetProcAddress, after LoadLibrary), which needs OWDLL2; their data,
        #      * heap, resources, a window class of the DLL's. What each gave in its
        #      * window. */
        #     int FAR PASCAL DllAdd(int a, int b);
        #     int FAR PASCAL DllCalls(void);
        #     int FAR PASCAL DllString(LPSTR buf, int n);
        #     LPSTR FAR PASCAL DllAlloc(void);
        #     BOOL FAR PASCAL DllRegister(void);
        #     typedef int (FAR PASCAL *ADD)(int, int);
        #
        #     static char lines[4][96];
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_PAINT) {
        #             PAINTSTRUCT ps;
        #             HDC dc = BeginPaint(h, &ps);
        #             int i;
        #             for (i = 0; i < 4; i++)
        #                 TextOut(dc, 4, 4 + 18 * i, lines[i], strlen(lines[i]));
        #             EndPaint(h, &ps);
        #             return 0;
        #         }
        #         if (msg == WM_DESTROY) {
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        #     {
        #         WNDCLASS wc;
        #         MSG msg;
        #         HWND w;
        #         HINSTANCE lib = LoadLibrary("OWDLL.DLL");
        #         ADD add = (ADD)GetProcAddress(lib, "DLLADD");
        #         ADD by_ordinal = (ADD)GetProcAddress(lib, MAKEINTRESOURCE(1));
        #         char text[48], heap[32];
        #         LPSTR p;
        #         int sum = DllAdd(2, 3);
        #         sprintf(lines[0], "add %d, %d, %d, lib %d", sum, add ? add(10, 20) : -1,
        #                 by_ordinal ? by_ordinal(1, 1) : -1, lib >= 32);
        #         DllString(text, sizeof(text));
        #         p = DllAlloc();
        #         _fstrncpy(heap, p ? p : "-", sizeof(heap) - 1);
        #         heap[sizeof(heap) - 1] = 0;
        #         sprintf(lines[1], "calls %d, '%s', '%s'", DllCalls(), text, heap);
        #         sprintf(lines[2], "registered %d", DllRegister() != 0);
        #         memset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwUse";
        #         RegisterClass(&wc);
        #         w = CreateWindow("OwUse", "DLLs", WS_OVERLAPPEDWINDOW, 30, 50, 400, 170, NULL, NULL,
        #                          hInstance, NULL);
        #         CreateWindow("DllWindow", "", WS_CHILD | WS_VISIBLE | WS_BORDER, 4, 80, 200, 24, w,
        #                      (HMENU)1, hInstance, NULL);
        #         ShowWindow(w, nCmdShow);
        #         UpdateWindow(w);
        #         while (GetMessage(&msg, NULL, 0, 0)) {
        #             TranslateMessage(&msg);
        #             DispatchMessage(&msg);
        #         }
        #         FreeLibrary(lib);
        #         return msg.wParam;
        #     }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            'b20002000000000002030200000400206403010000000200020005000d004000500050005900630034010000'
            '00000100000002000000000000000003a100c414400dc414940bbe01410c4004054f57555345000000300035'
            '003900400046000006444c4c41444409444c4c535452494e4708444c4c414c4c4f4308444c4c43414c4c530b'
            '444c4c5245474953544552045553455203474449064b45524e454c054f57444c4c084b4559424f4152440000'
            '096f777573652e65786500000000ccebfd90909090001e5890455589e51e8ed8b83800e89d02565783ec208b'
            '460c3d0f007557ff760e8d56da1e529affff000089c731f657b8040050b10389f0d3e001f0d1e00504005089'
            'f0d1e0d1e029f0b105d3e005be011e50e88a02509affff00004683fe047ccdff760e8d56da1e529affff0000'
            '31c031d2eb213d0200750a31c0509affff0000ebebff760e50ff760aff7608ff76069affff00008d66fa5f5e'
            '1f5d4dca0a005589e5b8ac00e80402565781ec86001eb81000509affff000089c68946fa501eb81a00509aff'
            'ff00008946f28956f45631c050b80100509affff00008946f68956f8b8020050b80300509affff000089c783'
            'fe207205b80100eb0231c050837ef8007506837ef600740ab801005050ff5ef6eb03b8ffff50837ef4007506'
            '837ef200740db80a0050b8140050ff5ef2eb03b8ffff5057b8210050b8be0150e8b50183c40c8d9676ff1e52'
            'b83000509affff00009affff000089c389d1b81f005085d2750985db7505bb38008cd98cda8d46a6e89601c6'
            '46c5008d46a6508d8676ff509affff000050b83a0050b81e0250e8630183c40a9affff000085c07403b80100'
            '50b84f0050b87e0250e8480183c406bb1a0031d28d46c6e88401c746c8ffffc746caffff8b460c8946d031c0'
            '5050b8007f509affff00008946d431c0509affff00008946d6c746dc5d008c5ede8d56c61e529affff00001e'
            'b85d00501eb8630050b8cf005031c050b81e0050b8320050b8900150b8aa005031c05050ff760c50509affff'
            '000089c61eb86900501eb8680050b880505031c050b8040050b8500050b8c80050b818005056b8010050ff76'
            '0c31c050509affff000056ff76049affff0000569affff00008d56e01e5231c05050509affff000085c07416'
            '8d56e01e529affff00008d56e01e529affff0000ebd7ff76fa9affff00008b46e48d66fc5f5e5dc20a008c16'
            'c800c33bc473132bc4f7d83b060a0077068cd03b06c8007401c30e5ab8d702bb0100e92e02537461636b204f'
            '766572666c6f772100515789c78cd88ec083c9ff33c0f2aef7d14989c85f59c35589e553528b46048d5e088b'
            '5606e821025a5b5dc35589e5565789c789ce8b4e0489d885c9750889f85f5e5dc202008ec21e968ed857acaa'
            '84c0e0fa33c0d1e9f3ab13c9f3aa581febdf515789c789d988d0571e078ae0d1e9f3ab13c9f3aa5f89f85f59'
            'c300eb0203009affff00000bc0750e5033c0ba0f00e88e0258b44ccd218c06ce00575606535253be3f048936'
            'e9008c1eeb00268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07b93f04bf'
            'be012bcf32c0f3aab8ae04a3e500b8ffffa3e700b430cd21a2d0008826d1009affff0000a2d2008826d30086'
            'e0a3d40054583bc475120f01e0a90100740ab001a2d600b003a2d7001ebff10057b89000509affff0000b8f1'
            '00893eed008c1eef00b8ff00e88f01e884fce91d014f70656e20576174636f6d20432f432b2b31362052756e'
            '2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20436f6e74'
            '72696275746f727320323030322d323032362e20506f7274696f6e7320436f70797269676874202843292053'
            '79626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df8d9e7aff'
            '8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026c607001e'
            '8d9e7aff531eb874005031c0509affff000089f8e96800515689c189d6e8570185c0750689c889f2eb9889d8'
            'e95000535689c38b378d440189078814ff470a5e5bc3515689c6b91d05e8640a01c6c604005e59c3c3c35352'
            '89c3833ea801007404ff16a801ff168201baff00b81000e8a800ff168201ff16840189d8e904fe5289c2ff16'
            '8201ff16840189d0e9f4fd5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b'
            '47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee8effebfbe0189fabbb20189fe88c8'
            '39fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402'
            'ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee898fe88d5bfbe0189fabbbe0189fe88'
            'c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402'
            '803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803e860100750331c0c35250cceb0657564944454f'
            'b8010083c404c3535689c689d3c7470200008a043c2d740b3c23750d804f020146ebef804f0208ebf73c2b75'
            '0a804f0204806702fdebe93c20750cf647020475df804f0202ebd93c307505884719ebd089f05e5bc3515657'
            '89c689d189dac6471920e8a2ff89c7c74706000089c6803d2a74228a043c307c2e3c397f2aba0a008b4706f7'
            'ea89c28a04982d300001c246895706ebde89cf8305028b3d8b45fe89470685c07c3846c74708ffff803c2e75'
            '4846c747080000803c2a742b8a043c307c373c397f33ba0a008b4708f7ea89c28a04982d300001c246895708'
            'ebdef75f06804f0208ebbf89cf8305028b3d8b45fe89470885c07c21468a048d54013c68731e3c4c73573c49'
            '7555807c01367536807c02347530e97700c74708ffffebd88d7c02770e807c0168755089fe804f0210eb133c'
            '7473153c6c75263a4401751689fe804f028089f05f5e59c3764f3c7a744b3c7775f046804f0240ebe9eb0aeb'
            '1c3c6a75e189d6ebd9762a3c5774123c4e75d389d6804f0302ebcbeb0b3c4675c546804f0304ebbe89d6804f'
            '0220ebb683c603ebad89d6804f0301eba946eba6565789c689d789da8ec731c039d0740c89f34626803f0074'
            '0340ebf05f5ec35589e551565783ec0289de89c389d731c983feff75278ec7268b0785c0741489c28d46f883'
            'c302e83f093dffff74e701c1ebe389c88d66fa5f5e595dc38ec7268b0785c0741839f17f1489c28d46f883c3'
            '02e814093dffff74e301c1ebdf39f17ed189f0ebcf5589e5515683ec0a5289d9bb10008d56f2e852098d46f2'
            '8b5ef0e82bfa89c601cb89c889f24e85d27e0f89c24b4885d27e078a52f28817ebea8b76f001c689c24e4885'
            'd27e05c60430ebf38b5ef001cbc607008d66fce97aff5589e5565783ec0889c68956f8895ef6894efa85c97c'
            '288b5ef8837f08ff7505c747080400bb0a008b46fa89f2e8dd088976f48d4401803c00741489c6ebf4f75efa'
            'c6042df75ef6835efa0046ebc88b5ef8837f08007435c6042e31ff89c68b5ef83b7f087d238b46f6bb0a0031'
            'd231c9c746fa0000e8af088956fa8946f68a46fa043047880446ebd5c60400f646f78074423b76f474114e80'
            '3c2e75014e803c397538c60430ebeac60431468a048d7c013c30750489feebf33c2e7510c604308d75018805'
            '803c30750346ebf8c6043046c604008d66fc5f5e5dc3fe04ebf55389c3f64702087522807f1930751c8b4706'
            '2b470c2b470e2b47102b47122b47142b471685c07e0301470e5bc35589e5565783ec0689c78956fa894ef883'
            '7f10007eb28e46fa8d46f6268b15e87b0783c70289c13dffff74e43b47107f198d76f64983f9ff74d68a0498'
            '89c289d846ff56f8ff4f10ebeac747100000e976ff200b890f6b0f890f200bb10c200b890f890f890f890f89'
            '0f890f890f890fa80e890f890ff40c890f890f890f890f710d890f890f890f890f890f890f890f890f200b09'
            '0e1d0fbe0d200bb10c200b890fbe0d890f890f890f890f890f100ea80e890f890ff40c890fbe0d890f890f71'
            '0d5589e5565783ec125089d789dec7471600008946f48b471689471489471289471089470e89470c8a47188c'
            '5ef63c6973393c64756a8a4402a88074618305088b1d8b47fc8946f28b47fa8946f08b47f88b4ffe8946eee9'
            'e5008b46e889f389faff46f4ff16aa01e9b50176c93c7875238a4402a88074308305088b1d8b47fc8946f28b'
            '47fa8946f08b47f88b4ffe8946eeeb293c7574d93c6f74d5eb1feb653c6274cd3c58ebf2a840742a8305048b'
            '1d8b47fc8946f88b47fe8946fa8a44182c41c746ec0a003c37775130e489c3d1e32effa7570a8305028b1d8b'
            '47fec746fa00008946f8f64402207407c746fa0000ebcaf644021074c48a46f830e48946f830c0ebb5a84074'
            '168305048b1d8b47fc8946f88b47fe8946faeb23e9a4038305028b1d8b47fe998946f88a44028956faa82074'
            '618b46f8998946f88956fa30c0f6440280745985c97c10750a837ef2007504837ef00084c0745a8b440c89c2'
            '8b5ee84201c389540cc6072df6440280743631c02b46ee8946eeb800001b46f08946f0b800001b46f28946f2'
            'b8000019c889c1e927ffa81074a58a46f898eb98837efa007cb5ebaff75efaf75ef8835efa00e908ff8a4402'
            'a80474148b440c89c28b5ee84201c389540cc6072be9edfea8027503e9e6fe8b440c89c28b5ee84201c38954'
            '0cc60720e9d2fef64402207503e966fe8305048b1d8b47fc89f28946f88b47fe8b5ef88946fa89c18b46e8e8'
            '20fcbbffff8b46e88cdae837fb89441089f08b56f6e8eafc8b46f4e9d9fc8b5ee8c607008a4403a804753fa8'
            '028305028b1d8b5ffe85db74068c5ef6895ef4807c1853754ef6440220743a8b46f48b56f68b5c08e8edfa89'
            '4410837c08007cb03b44087eab8b4408eba38305048b1d8b47fe8b7ffc85c0750485ff74c2897ef48946f6eb'
            'ba8b46f48b56f68b5c08e8d2faebc4f644024075ecebb08a4402a8017441a8807503e9820085c97512837ef2'
            '00750c837ef0007506837eee0074248b440c89c28b5ee84201c389540cc607308b440c8b5ee889c201c3428a'
            '441889540c8807c746ec1000837c08ff7404c64419208b5ee8035c0c8c5ef6895ef4f6440280743d837c0800'
            '753985c97535837ef200752f837ef0007529837eee0075238e46f631c026c60700eb488b46fa0b46f8eb8ac7'
            '46ec0200ebaec746ec0800eba7eb5cff76ec8b46e88b5ef203440c8b56ee5089c88b4ef0e81404807c185875'
            '068b46e8e82005bbffff8b46f48b56f6e8d1f989441089c23b44087c4af64402017503e98afe837eec0a7df7'
            '85c07409c45ef426803f3074eaff440ee971fe837c0800750e8b46fa0b46f875068e46f6e972ff8b5ee88b4e'
            'ec8b46f88b56fa035c0ce8e204eb948b440829d089440ee93efe837c0600750bf64403047458c74406090080'
            '6402f98305028b1d8b4ffef6440304744883c302891d8b56e88b47febb0400e8c3f98b5ee889c88d5705c647'
            '043abb0400e8b1f9807c185075068b46e8e86304bbffff8b46f48b56f6e814f989440ce9dafdc744060400eb'
            'a6bb04008b56e889c8ebcec744100100f644024075108305028b1d8a47fe8b5ee88807e9aefd8305028b1d8d'
            '46ea8b57fee85c023dffff7503e998fd8b5ee88a46ea880730e4e8960485c074ec8a46eb884701ff440ce97b'
            'fd8305028b1d8b46e88b57fee829023dffff7403e962fdc744100000e95dfdc7440600008b5ee88a44188807'
            'c7440c0100e948fd5589e5565783ec6a5389cfc646fa00c746dc0000c746dec800c746e400008946da8956f8'
            '8b5ef88a0784c074588d4f013c25740d9889c28d46da894ef8ffd7ebe38d5eda8d569089c8e8e5f689c38a07'
            '438846f2895ef884c0742a3c6e755c8a46dda8047422834690048b5e908b76908b5ffc8e44fe8a46dca81074'
            '1f8a46e4268807eb9fe976018b5e9083c302a8028cd8895e908ec08b5ffeebdaa82074098b46e4268907e97b'
            'ffa84074108b46e49926890726895702e969ffeb1fa88074df8b46e499268907b10f2689570226d3fa268957'
            '0426895706e948ff8d4efa8d5eda8d56908d4692e83efa89c68b46e60346e80346ea0346ec0346ee0346f089'
            '56f62946e0f646dc0874198d5e92837ee6007e298a079889c28d46da43ffd7ff4ee6ebea807ef32075e1837e'
            'e0007edbba20008d46daffd7ff4ee0ebed837ee8007e0dba30008d46daffd7ff4ee8ebed8a46f23c737540f6'
            '46dc40751a837eea007e218e46f6268a049889c28d46da46ffd7ff4eeaebe68d5eda8b56f689f989f0e8e3f8'
            '837eec007e2bba30008d46daffd7ff4eecebed3c5374dc837eea007ee38e46f6268a049889c28d46da46ffd7'
            'ff4eeaebe6837eee007e148e46f6268a049889c28d46da46ffd7ff4eeeebe6837ef0007e0dba30008d46daff'
            'd7ff4ef0ebedf646dc087503e939fe837ee0007ef7ba20008d46daffd7ff4ee0ebed8b46e4e92bf85389c385'
            'c0740df6c6ff740a8837b802008857015bc3b8010088175bc35589e551565783ec245289d9c646d6008d76d7'
            '89d78d5ef8894ef833d2f737890789d38a87a20088048b46f84685c075e44e8a0488054784c075f68b46d48d'
            '66fa5f5e595dc35689d683fb0a750485c07c07e8abff89f05ec3f7d842c6042debf19350920bc07402f7e291'
            '0bc07404f7e303c858f7e303d1c35352bb0100b880008cdae9c0f25589e5565783ec48538b76048976f88d76'
            'b5c646b4008976fa8b76068b5eb28976f68d76f68bfa85c074278bd08bc333db3b1472098bd88bc233d2f734'
            '93f73491f73497f73489148bd08bc38bd98bcfeb2c85db74148bd38bc133db33c93b1472df8bc88bc233d2eb'
            'd48bd18bc733c933ff3b1472ce8bf88bc233d2ebc38b7ef689de895eb28a9da2008b7efaff46fa881d85c075'
            '8385f67403e97cff85c975f985d275f5ff4efa8b76fa8a048b76f8ff46f8880484c075ec8b46048d66fc5f5e'
            '5dc204005589e5565783ec028b76048976fa837e060a750c85c07c15750685db750285c9ff7606ff76fae812'
            'ff89f0ebca31ff29d789fabf000019cf89f9bf000019df89fbbf000019c789f88d7c01c6042d897efaebcd53'
            '5289c289c38a0784c0740f2c613c19760343ebf104418807ebf789d05a5bc35589e5565783ec245351c646d8'
            '008d76d989df8b5ed4895efa8d5efa33c93b1772069287d1f73791f73789178bd18b5efa8a9fa200881c4685'
            'd275d785c075d34e8a0488054784c075f68b46d68d66fc5f5e5dc35689de83f90a750485d27c07e89dff89f0'
            '5ec3f7dac6072df7d883da0043ebec5389c38a9f3f0380e30130e789d85bc331c0e957005352bb010131c031'
            'd2a3b001a3ae01b83e03e82def5a5bc35589e553515250c746f80000eb0aff46f8817ef80001731f8a46f830'
            'e4509affff000085c074e78b5ef8c706b0010100c6873f0301ebd78d66fa5a595b5dc353515289c2e8a1ff83'
            'faff743e83fafe743983fafd744983fafc7526bb1f00ba0100b8c003e8c3eebb1d00ba0100b81f04e8b7eec7'
            '06b0010100c706ae01a403eb1e85d27405b80100eb179affff00003d030a72f1e869ff9affff0000a3ae0131'
            'c05a595bc300240003012e000100270003015f00020021000301720001002800030185000100060003019900'
            '01006b000301bd0003005f000301cd00030032000301e000030032000302f300040001000302570104000800'
            '03025c010400120003028b0104001b0003029f01040024000500c701010008000200cc01010008000301dd01'
            '0100ad000301e801020057000301fd010100390003012c020100290003016002010029000301690201002a00'
            '03016f0201007c0003017e0201006c0003018c020100710003019602010072000301a0020300600003016903'
            '03005b000301a80303001e000301af03010005000200c903010064030301da030300030003010c0403003100'
            '0301fc040100010003013d140300cf000301a914030003000301b61405008400000000000500000000000000'
            '000000004f57444c4c2e444c4c00444c4c414444006164642025642c2025642c2025642c206c696220256400'
            '2d0063616c6c732025642c20272573272c20272573270072656769737465726564202564004f775573650044'
            '4c4c730000446c6c57696e646f7700004f70656e20576174636f6d00466c6f6174696e672d706f696e742073'
            '7570706f7274206e6f74206c6f6164656400303132333435363738396162636465666768696a6b6c6d6e6f70'
            '7172737475767778797a00000000ffff00000000000000000000000c00000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000043054305000000000000000000000000000000000000'
            '00000000000000000000000000000000000038123812000000000020b0020000002001140000'
        )),
    'OWFPU': (
        # wcl -bt=windows -l=windows -ms owfpu.c
        # #include <windows.h>
        # #include <stdio.h>
        # #include <math.h>
        # #include <float.h>
        #
        # /* the coprocessor through WIN87EM: arithmetic, the library's functions,
        #  * conversions and formatting; the results in a message box */
        # static volatile double a = 2.5, b = 4.0;
        #
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
        # {
        #     char text[128];
        #     double r = sqrt(a * b * 10.0);                  /* 10 */
        #     double s = sin(3.14159265358979 / 6.0);         /* 0.5 */
        #     long n = (long)(a * 1000000.0) + (long)floor(-b / 3.0);
        #     float f = (float)(1.0 / 3.0);
        #     unsigned cw = _control87(0, 0);
        #     sprintf(text, "%.3f %.4f %ld %.5f %g cw %04x", r, s, n, (double)f, exp(1.0),
        #             cw);
        #     MessageBox(NULL, text, "Coprocessor", MB_OK);
        #     return (int)(r + 0.5);
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '7f000200000000000203020000040020b405010000000200020004000d004000500050005900610001010000'
            '000001000000020000000000000000038700fa45400dfa45b127e206410cf007054f57465055000000010006'
            '000e0015000004555345520757494e3837454d064b45524e454c084b4559424f4152440000096f776670752e'
            '657865000000ccebfd909090900000000000000000005589e5b8ca00e8f0005681ec9a009bdd068a039bdc0e'
            '92039bdc0e4000e8a4019bdd5ee49bdd064800e80e029bdd5eec9bdd068a039bdc0e5000e8a3039bdb5ef49b'
            'dd0692039bd9e09bdc36580083ec0889e39bdd1f909be8ac03e882039bdb5ef88b4ef4909b034ef88b5ef613'
            '5efa31d231c0e8d903509bd9e8e84a0483ec0889e69bdd1cb8d53f50b8555550b800605031c0505351909bff'
            '76f2ff76f0ff76eeff76ecff76eaff76e8ff76e6ff76e4b81000508d8664ff50e8d10483c42a31c0508d9e64'
            'ff1e531eb82e005031c0509affff00009bdd46e49bdc066000e8fe029bdf5efc909b8b46fc8d66fe5e5dc20a'
            '00008c169a03c33bc473132bc4f7d83b060a0077068cd03b069a037401c30e5ab82b01bb0100e92a06537461'
            '636b204f766572666c6f772100001e5251532bdbb88a018cca9affff0000b88a018ccabb03009affff0000e8'
            '1606a25604e80c069affff0000f6c4047405a05604eb0232c0a257045b595a1fc3525153bb02009affff0000'
            '5b595ac3558bec8b46065d1e8ed8e838061fcb00558becf6460b8074268b460a25ff7f0b46080b46060b4604'
            '7415b8030050ff760aff7608ff7606ff7604e81106eb079bdd46049bd9fa5dc20800b003558bec83ec0a9bd9'
            'e49bdd7efe909b8a66ff9e73169bdd5ef63c0374049bdd5ef68946fee8db05b001eb059bd9fa32c08be55dc3'
            '35c26821a2da0fc9014035c26821a2da0fc9ff3f35c26821a2da0fc9fe3fe102eb02eb02e102de02e802e802'
            'de02803e560403b002751b9bd9ffe8ab0073f8c3803e560403b00075099bd9fee8990073f8c3558bec538bd8'
            '509b2edb2e1e029bd9c99bd9e49bdd7efc9b8b46fc9e730a9bd9e080fb007502b3049bd9f89bdd7efc9b8b46'
            'fca9000475f032c0f6c40274080c019bd8e19bd9e0f6c44074020c02f6c401740204049bddd99bd9f29bd9c0'
            '9bdcc89bd9c29bdcc89bdec19bd9fa02d880e307d0e332ff8d9f28022e8b075b5b5dffe09bd9e09bddd99bde'
            'f9c39bd9e09bdef99bddd9c3558bec509bdd7efe9b8a66ff80cc019e7b1b9b2edb2e0a029bd9c99bd9f89bdd'
            '7efe9b8a66ff9e7af29bddd9f8585dc3558bec9bdd4604e80cff5dc20800558bec9bdd4604e810ff5dc20800'
            '803e5604037513f60658040175059bd9f2eb03e824059bddd8c3558bec50505351529bd9eb2bdb2bc99bd9c9'
            '9bd9e49bdd7efe9b8b46fe9e73059bd9e0f7d19bd9f89bdd7efe9b8b46fe9e7af29b2edb2e14029bddda9bd8'
            'd19bdd7efe9b8b46fe9e76079bd8e1f7d3f7d19b2edb2e1e029bd8d99bdd7efe9b8b46fe9e73059bd8e9f7d3'
            '9bddd99bd9f20bdb74059bdef1eb039bdef90bc974039bd9e05a595b585d5dc3558bec9bdd4604e852ff5dc2'
            '080050558bec509bd97efe909b8b46fec646ff1f9bd96efe9bd9fc8946fe9bd96efe909b8be55d58c35589e5'
            '5083ec0a8d46f450ff760aff7608ff7606ff7604e8e9049bd9ee9bded99bdd7efc909b8b46fc9e760f9bdd46'
            'f49bdc0668009bdd5ef4909b9bdd46f4909b8d66fe585dc208005589e5535183ec0289c3c746fa0000803e56'
            '0400750a8b46fa8d66fc595b5dc38d46fa959bd97e009b9553bb05009affffffff5b8946fa85d274db89d1f7'
            'd121da21c809d08946fa8d46fa959bd96e00958b46fa53bb04009affffffff5bebb6ee39fafe422e8640ee39'
            'fafe422e86c0558bec83ec0aeb2eb004558bec83ec0a9b2edc16c0049bdd7efe909b8a66ff9e76143c07740a'
            '9bdd5ef68946fee8d802b0018be55dc39b2edc16c8049bdd7efe909b8a66ff9e77059bd9eeeb6a803e560403'
            '75209bd9ea9bdec99bd9c09bd9fc9bd9c99bd8e19bd9f09bd9e89bdec19bd9fdeb439bd9ea9bdec99bd9e89b'
            'd9e09bd9c19bd97efe909b8a46ff8066fff3804eff049bd96efe9bd9fc8846ff9bd96efe9bd9ca9bd8e29bd9'
            'fd9bd9f09bdee19bdcc89bd9fd9bddd932c08be55dc3558bec9bdd4604e83eff5dc208005589e553528b4604'
            '8d5e088b5606e89c035a5b5dc300eb0203009affff00000bc0750e5033c0ba0f00e85a0458b44ccd218c06a0'
            '03575606535253bef0078936bb038c1ebd03268a078804464384c075f589360a005b2bc0509affff00005757'
            '9affff0000fc1e07b9f007bfe2062bcf32c0f3aab8fe06a3b703b8ffffa3b903b430cd21a2a2038826a3039a'
            'ffff0000a2a4038826a50386e0a3a60354583bc475120f01e0a90100740ab001a2a803b003a2a9031ebfc303'
            '57b89000509affff0000b8c303893ebf038c1ec103b8ff00e85b03e89cf9e9e9024f70656e20576174636f6d'
            '20432f432b2b31362052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e'
            '20576174636f6d20436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f70'
            '79726967687420284329205379626173652c20496e632e20313938382d323030322e00c3cb005589e5515657'
            '81ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743'
            'ebe031c08ec15026c607001e8d9e7aff531eb870005031c0509affff000089f8e93402515689c189d6e8a503'
            '85c0750689c889f2eb9889d8e91c0200e84003c3558bec2bc050dbe3d97efe8b46fe32c0b4033866ff754181'
            '66fe7fff9bd96efe9bdbe19bd97efe9bb001f746fe800075249bdbe39bd9e89bd9ee9bdef99bd9c09bd9e09b'
            'ded99bdd7efe9bb0028a66ff9e7402b0039bdbe3988be55dc3b88c000ee84f0390c35589e550535283ec0a8a'
            '460c88c330ff89da80cf813c0675369bd9ee9bdc5e049bdd7ef8909b8b46f89e760580ce1189d38d560489d8'
            'e881049bdd5ef0909b9bdd46f0909b8d66fa5a5b585dc20a003c0475239bd9ee9bdc5e049bdd7ef8909b8b46'
            'f89e76cb31c08946f68946f48946f28946f0ebc93c0172b73c0377b380cb4030ff80cf20eba9000100000400'
            '000700000a00000d00000000703f0000005f555083ec148becdb7e008b460825ff7f0f8482003dff7f747d8b'
            '460603c07376db6e00d9e12ed81e74089bdfe09e7366db6e00d9fbdb7e00db6e008b460603c035000ea9000e'
            '740adef9d9e883c414585dc3c1e80c538bd82e38a760085b750adef9d9e883c414585dc39b9bd97e108b4610'
            '0d3f0389460cd96e0c2ed80e7008d9c92ed80e7008d9c9d96e10def9d9e883c414585dc3db6e00d9f283c414'
            '585dc300558bec538b5e0c9bdd46049bd9c0e8c5fa9bdce99bdd1f5b909b5dc20a00535689c38b378d440189'
            '078814ff470a5e5bc3515689c6b93809e8f50d01c6c604005e59c300c3c3535289c3833e9e04007404ff169e'
            '04ff165c04baff00b81000e8a800ff165c04ff165e0489d8e938fc5289c2ff165c04ff165e0489d0e928fc55'
            '89e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df'
            '1eff1f1febd95589e5535152565789c11ee823fdbfd60689fabbac0689fe88c839fb7316803f02750583c306'
            'ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f'
            '5e5a595b5dc35589e55351565789c11ee8ccfc88d5bfe20689fabbd60689fe88c839fb7316803f02750583c3'
            '06ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83b'
            'ffebe91f8d66f85f5eeb9fc706a0045b19c706a204301bc35389c39bdd379b5bc35389c39bdd279b5bc3803e'
            '5604007501c3dbe39b9bd92e6204c3803e5704007508803e56040075e9c3c706a4048e0ac706a604970aebea'
            '535152803e56040074045a595bc39affff0000f6c404740ce889fca25704a25604e8bfff803eae030074df30'
            'c0a25604a25704ebd5803e600400750331c0c35250cceb0657564944454fb8010083c404c3b80100e9da0045'
            '5589e5535289c253bb08009affffffff5ba801741bba81008b1e680483fb01740a83fb02740583fb0375325a'
            '5b5d4dcba8027405ba8200ebdfa8047405ba8300ebd6a8087405ba8400ebcda8107405ba8500ebc4a82074c0'
            'ba8600ebbbb80200a36804ffd3ebc45389c33d01007c243d0c007f1fc706a8041f0b3d0400752983fa02751a'
            'e82d10d1e38b876404899764045bc3c706e2060900b803005bc383fa0374e4e89d10ebdf3d070075da83fa02'
            '740a83fa0374d0e8d310ebcbe84610ebc61c0c480c700c210c700c700c510c700c700c700c700c700c535256'
            '5789c389c6d1e6488b9464043d0b00775889c7d1e72effa5e70b83fa02744c83fa01741b83fa02741683fa03'
            '741189ded1e6c78464040200e89d0f89d8ffd231c05f5e5a5bc3b88c000ee8d6feebf083fa0174eb83fa0274'
            'e683fa0374e1c78464040200e8c20febd2eb20e9db0e83fa0174cc83fa0274c783fa0374c289ded1e689d8c7'
            '8464040200ebb2b8ffffebb15389d3e802005bc35589e551565783ec1c89c189d78b45068946e88b45048946'
            'e68b45028946e48b058946e28b47068946f08b47048946ee8b47028946ec8b078946eaf6c140743dbe010089'
            'c830ec241f8976dee847108946e0f6c510755af6c5207553f6c5407560f6c580745da12a028946f8a1280289'
            '46f6a126028946f4a12402eb53f6c1807405be0200ebbcf6c5017405be0300ebb2f6c5087405be0400eba8f6'
            'c5027405be0600eb9ef6c5047499be0500eb94eb149bdd0624029bd9e09bdd5ef2909beb12eb4aeb5131c089'
            '46f88946f68946f48946f2ff76f8ff76f6ff76f4ff76f2ff76f0ff76eeff76ecff76eaff76e8ff76e6ff76e4'
            '8b56e0ff76e289f0e842109bdd46f2909b8d66fa5f5e595dc331c0c746f8f03febb48b47068946f88b470489'
            '46f68b47028946f48b07eba45589e583ec12c746f45041c746f27e01c746f000c0c746ee0000c746fc4741c7'
            '46faffffc746f80080c746f60000803e57040372279bdd46ee9bd9c09bdd46f69bd9c99bd8f19bdec99bdee9'
            '9bdc1e7c009bdd7efe9b8b46fe9e770489ec5dc3800e580401ebf5535689c689d3c7470200008a043c2d740b'
            '3c23750d804f020146ebef804f0208ebf73c2b750a804f0204806702fdebe93c20750cf647020475df804f02'
            '02ebd93c307505884719ebd089f05e5bc351565789c689d189dac6471920e8a2ff89c7c74706000089c6803d'
            '2a74228a043c307c2e3c397f2aba0a008b4706f7ea89c28a04982d300001c246895706ebde89cf8305028b3d'
            '8b45fe89470685c07c3846c74708ffff803c2e754846c747080000803c2a742b8a043c307c373c397f33ba0a'
            '008b4708f7ea89c28a04982d300001c246895708ebdef75f06804f0208ebbf89cf8305028b3d8b45fe894708'
            '85c07c21468a048d54013c68731e3c4c73573c497555807c01367536807c02347530e97700c74708ffffebd8'
            '8d7c02770e807c0168755089fe804f0210eb133c7473153c6c75263a4401751689fe804f028089f05f5e59c3'
            '764f3c7a744b3c7775f046804f0240ebe9eb0aeb1c3c6a75e189d6ebd9762a3c5774123c4e75d389d6804f03'
            '02ebcbeb0b3c4675c546804f0304ebbe89d6804f0220ebb683c603ebad89d6804f0301eba946eba6565789c6'
            '89d789da8ec731c039d0740c89f34626803f00740340ebf05f5ec35589e551565783ec0289de89c389d731c9'
            '83feff75278ec7268b0785c0741489c28d46f883c302e84f0e3dffff74e701c1ebe389c88d66fa5f5e595dc3'
            '8ec7268b0785c0741839f17f1489c28d46f883c302e8240e3dffff74e301c1ebdf39f17ed189f0ebcf5589e5'
            '515683ec0a5289d9bb10008d56f2e8620e8d46f28b5ef0e8740e89c601cb89c889f24e85d27e0f89c24b4885'
            'd27e078a52f28817ebea8b76f001c689c24e4885d27e05c60430ebf38b5ef001cbc607008d66fce97aff5589'
            'e5565783ec0889c68956f8895ef6894efa85c97c288b5ef8837f08ff7505c747080400bb0a008b46fa89f2e8'
            'ed0d8976f48d4401803c00741489c6ebf4f75efac6042df75ef6835efa0046ebc88b5ef8837f08007435c604'
            '2e31ff89c68b5ef83b7f087d238b46f6bb0a0031d231c9c746fa0000e8d70d8956fa8946f68a46fa04304788'
            '0446ebd5c60400f646f78074423b76f474114e803c2e75014e803c397538c60430ebeac60431468a048d7c01'
            '3c30750489feebf33c2e7510c604308d75018805803c30750346ebf8c6043046c604008d66fc5f5e5dc3fe04'
            'ebf55389c3f64702087522807f1930751c8b47062b470c2b470e2b47102b47122b47142b471685c07e030147'
            '0e5bc35589e5565783ec0689c78956fa894ef8837f10007eb28e46fa8d46f6268b15e88b0c83c70289c13dff'
            'ff74e43b47107f198d76f64983f9ff74d68a049889c289d846ff56f8ff4f10ebeac747100000e976ffcc1235'
            '1717173517cc125d14cc1235173517351735173517351735173517541635173517a01435173517351735171d'
            '1535173517351735173517351735173517cc12b515c9166a15cc125d14cc1235176a15351735173517351735'
            '17bc15541635173517a01435176a15351735171d155589e5565783ec125089d789dec7471600008946f48b47'
            '1689471489471289471089470e89470c8a47188c5ef63c6973393c64756a8a4402a88074618305088b1d8b47'
            'fc8946f28b47fa8946f08b47f88b4ffe8946eee9e5008b46e889f389faff46f4ff16a004e9b50176c93c7875'
            '238a4402a88074308305088b1d8b47fc8946f28b47fa8946f08b47f88b4ffe8946eeeb293c7574d93c6f74d5'
            'eb1feb653c6274cd3c58ebf2a840742a8305048b1d8b47fc8946f88b47fe8946fa8a44182c41c746ec0a003c'
            '37775130e489c3d1e32effa703128305028b1d8b47fec746fa00008946f8f64402207407c746fa0000ebcaf6'
            '44021074c48a46f830e48946f830c0ebb5a84074168305048b1d8b47fc8946f88b47fe8946faeb23e9a40383'
            '05028b1d8b47fe998946f88a44028956faa82074618b46f8998946f88956fa30c0f6440280745985c97c1075'
            '0a837ef2007504837ef00084c0745a8b440c89c28b5ee84201c389540cc6072df6440280743631c02b46ee89'
            '46eeb800001b46f08946f0b800001b46f28946f2b8000019c889c1e927ffa81074a58a46f898eb98837efa00'
            '7cb5ebaff75efaf75ef8835efa00e908ff8a4402a80474148b440c89c28b5ee84201c389540cc6072be9edfe'
            'a8027503e9e6fe8b440c89c28b5ee84201c389540cc60720e9d2fef64402207503e966fe8305048b1d8b47fc'
            '89f28946f88b47fe8b5ef88946fa89c18b46e8e820fcbbffff8b46e88cdae837fb89441089f08b56f6e8eafc'
            '8b46f4e9d9fc8b5ee8c607008a4403a804753fa8028305028b1d8b5ffe85db74068c5ef6895ef4807c185375'
            '4ef6440220743a8b46f48b56f68b5c08e8edfa894410837c08007cb03b44087eab8b4408eba38305048b1d8b'
            '47fe8b7ffc85c0750485ff74c2897ef48946f6ebba8b46f48b56f68b5c08e8d2faebc4f644024075ecebb08a'
            '4402a8017441a8807503e9820085c97512837ef200750c837ef0007506837eee0074248b440c89c28b5ee842'
            '01c389540cc607308b440c8b5ee889c201c3428a441889540c8807c746ec1000837c08ff7404c64419208b5e'
            'e8035c0c8c5ef6895ef4f6440280743d837c0800753985c97535837ef200752f837ef0007529837eee007523'
            '8e46f631c026c60700eb488b46fa0b46f8eb8ac746ec0200ebaec746ec0800eba7eb5cff76ec8b46e88b5ef2'
            '03440c8b56ee5089c88b4ef0e82f09807c185875068b46e8e83b0abbffff8b46f48b56f6e8d1f989441089c2'
            '3b44087c4af64402017503e98afe837eec0a7df785c07409c45ef426803f3074eaff440ee971fe837c080075'
            '0e8b46fa0b46f875068e46f6e972ff8b5ee88b4eec8b46f88b56fa035c0ce8fd09eb948b440829d089440ee9'
            '3efe837c0600750bf64403047458c744060900806402f98305028b1d8b4ffef6440304744883c302891d8b56'
            'e88b47febb0400e8c3f98b5ee889c88d5705c647043abb0400e8b1f9807c185075068b46e8e87e09bbffff8b'
            '46f48b56f6e814f989440ce9dafdc744060400eba6bb04008b56e889c8ebcec744100100f644024075108305'
            '028b1d8a47fe8b5ee88807e9aefd8305028b1d8d46ea8b57fee86c073dffff7503e998fd8b5ee88a46ea8807'
            '30e4e8b10985c074ec8a46eb884701ff440ce97bfd8305028b1d8b46e88b57fee839073dffff7403e962fdc7'
            '44100000e95dfdc7440600008b5ee88a44188807c7440c0100e948fd5589e5565783ec6a5389cfc646fa00c7'
            '46dc0000c746dec800c746e400008946da8956f88b5ef88a0784c074588d4f013c25740d9889c28d46da894e'
            'f8ffd7ebe38d5eda8d569089c8e8e5f689c38a07438846f2895ef884c0742a3c6e755c8a46dda80474228346'
            '90048b5e908b76908b5ffc8e44fe8a46dca810741f8a46e4268807eb9fe976018b5e9083c302a8028cd8895e'
            '908ec08b5ffeebdaa82074098b46e4268907e97bffa84074108b46e49926890726895702e969ffeb1fa88074'
            'df8b46e499268907b10f2689570226d3fa2689570426895706e948ff8d4efa8d5eda8d56908d4692e83efa89'
            'c68b46e60346e80346ea0346ec0346ee0346f08956f62946e0f646dc0874198d5e92837ee6007e298a079889'
            'c28d46da43ffd7ff4ee6ebea807ef32075e1837ee0007edbba20008d46daffd7ff4ee0ebed837ee8007e0dba'
            '30008d46daffd7ff4ee8ebed8a46f23c737540f646dc40751a837eea007e218e46f6268a049889c28d46da46'
            'ffd7ff4eeaebe68d5eda8b56f689f989f0e8e3f8837eec007e2bba30008d46daffd7ff4eecebed3c5374dc83'
            '7eea007ee38e46f6268a049889c28d46da46ffd7ff4eeaebe6837eee007e148e46f6268a049889c28d46da46'
            'ffd7ff4eeeebe6837ef0007e0dba30008d46daffd7ff4ef0ebedf646dc087503e939fe837ee0007ef7ba2000'
            '8d46daffd7ff4ee0ebed8b46e4e92bf85352bb0100b884008cdae9faed5589e551565783ec2a89c189d6895e'
            'f88b7ef88b5f08c746d20100c746d400008a45183c477203e9c9003c467403e90001c746d48000804ed402c7'
            '46d20000f646d5027503e90b01f646d4807503e9fc00b850008946d68b7ef8f64502017404804ed4108b7ef8'
            'f64503017503e9f500e8ff0689c7f6050174f38d7ee683040a8cd88b348ec08d74f6a5a5a5a5a5804ed42083'
            'fbff7503bb06008d56d0895ed089cb8d46e643c746d80200e8d20c8b5ef88b46de8947108b46e08947128b46'
            'e28947148b46e4894716f646d5017404c6471920837eda007c638b5ef88a4702a804755ba80275648cda89c8'
            '8d66fa5f5e595dc37713c746d4800085db7503bb0100804ed404e937ff3c65730e3c617403e92cff804ed502'
            'e925ff7707804ed401e91cff3c6774d33c667503e908ffe90eff3c45750bc746d48000ebe0eb5beb6f3c4175'
            'ccc746d48000ebc8eb75b87000e901fff646d4807406b84500e9f5feb86500e9effe8304088b348b44fe8946'
            'f68b44fc8946f48b44fa8946f28b44f88d56e68946f08d46f0558be89bdd46008bea9bdb7e005de9f9fe8b5e'
            'f88b470c89c24289570c89cb01c3c6072de938ff8b470c89c24289570c89cb01c3c6072be925ff8b470c89c2'
            '4289570c89cb01c3c60720e912ff5389d331d2e86c179bdd1f909b5bc3c3833ea804007404ff16a8045352bb'
            'ff00b8a6008cdae9fdeb50515253545556571e06505089e5fcb8ffff8ed8833eac03007523a15c043d5f0974'
            '1b3d2b1c7416e8a418b80400e876f05858071f5f5e5d5b5b5a5958cfa1aa048b16ac04e98a18505152535455'
            '56571e06505089e5fcb8ffff8ed8833eac03007515a15c043d5f09740d3dda1b7408e85c18b80700ebb6a1ae'
            '048b16b004e9501853518b1eac048b0eaa0485db750485c97429a15c043d281c74243dda1b7506c7065c045f'
            '09b823008b1eaa048b0eac04e83d1831c0a3aa04a3ac04595bc3c7065c042b1cebdfc7065c04da1beb28e8af'
            'ff53518b1eb0048b0eae0485db750485c97429a15c043d281c74db3d2b1c7506c7065c045f09b81b008b1eae'
            '048b0eb004e8ec1731c0a3ae04a3b004595bc3535152a1ac048b16aa0485c0753085d2752cb82300bbffffb9'
            'ffffe8d017a3aa04b823008916ac04e8b617a15c043d5f09740f3d2b1c7506c7065c04281c5a595bc3c7065c'
            '04da1bebf4535152a1b0048b16ae0485c075e685d275e2b81b00bbffffb9ffffe88617a3ae04b81b008916b0'
            '04e86c17a15c043d5f0974053dda1bebb4c7065c042b1cebb4b8e206c3a3b204c353515689c689d1b80200e8'
            '5d1789c3d1e689da8b84e601e86b17b8b20189dae8631789da89c8e85c17b80a0089dae8cc175e595bc35389'
            'c3d1e38b87f4015bc3535289c3833eb2040075198b57028b07e8adff833f017516e852189bdd4714909b5a5b'
            'c3ff16b20485c074dfebede84318ebe8fca11dd61dc71dcc1dd11dd11d535289c2f60644050174053d010073'
            '1ff60644050274154a83fa05770f89d3d1e32effa76f1db80100e84f185a5bc376103d040077dae83fff89c3'
            'c7070e00ebcfe834ff89c3c7070d00ebc4b80800ebd8b81000ebd3b82000ebceb80400ebc95589e5535183ec'
            '1c89c389d1e891ff31d2833eb204007407f606440504750b89d08d66fc595b5dc218008b460a8946ea8b4608'
            '8946e88b46068946e68b46048946e48b46128946f28b46108946f08b460e8946ee8b460c8946ec8b461a8946'
            'fa8b46188946f88b46168946f68b4614895ee08946f48d46e0894ee2ff16b20489c2eb9c5389c385c0740df6'
            'c6ff740a8837b802008857015bc3b8010088175bc35589e551565783ec245289d9c646d6008d76d789d78d5e'
            'f8894ef833d2f737890789d38a873a0288048b46f84685c075e44e8a0488054784c075f68b46d48d66fa5f5e'
            '595dc35689d683fb0a750485c07c07e8abff89f05ec3f7d842c6042debf1515789c78cd88ec083c9ff33c0f2'
            'aef7d14989c85f59c3009350920bc07402f7e2910bc07404f7e303c858f7e303d1c35589e5565783ec48538b'
            '76048976f88d76b5c646b4008976fa8b76068b5eb28976f68d76f68bfa85c074278bd08bc333db3b1472098b'
            'd88bc233d2f73493f73491f73497f73489148bd08bc38bd98bcfeb2c85db74148bd38bc133db33c93b1472df'
            '8bc88bc233d2ebd48bd18bc733c933ff3b1472ce8bf88bc233d2ebc38b7ef689de895eb28a9d3a028b7efaff'
            '46fa881d85c0758385f67403e97cff85c975f985d275f5ff4efa8b76fa8a048b76f8ff46f8880484c075ec8b'
            '46048d66fc5f5e5dc204005589e5565783ec028b76048976fa837e060a750c85c07c15750685db750285c9ff'
            '7606ff76fae812ff89f0ebca31ff29d789fabf000019cf89f9bf000019df89fbbf000019c789f88d7c01c604'
            '2d897efaebcd535289c289c38a0784c0740f2c613c19760343ebf104418807ebf789d05a5bc35589e5565783'
            'ec245351c646d8008d76d989df8b5ed4895efa8d5efa33c93b1772069287d1f73791f73789178bd18b5efa8a'
            '9f3a02881c4685d275d785c075d34e8a0488054784c075f68b46d68d66fc5f5e5dc35689de83f90a750485d2'
            '7c07e89dff89f05ec3f7dac6072df7d883da0043ebec5389c38a9fe50680e30130e789d85bc331c0e9b616b8'
            'b404c35589e55351565783ec0a89c789d181fa00207d13beb60485c97e4ef6c101750cd1f983c60aebf0b900'
            '20ebe88b44088946f68b44048946f28b44068946f48b048946ee8d56ee8b440289fb8946f089f8558be89bdb'
            '6e008bea9bdb6e009bdec98beb9bdb7e005debb78d66f85f5e595b5dc35589e553515683ec0a89c185d27505'
            '8d66faebe750558bec9bd97e02909b5d5889c680cc0350558bec9bd96e025d58c746f8ff3fc746f4000031c0'
            'c746f600808946f08946f285d27c328d46f0e83eff89cb8d56f089c8558be89bdb6e008bea9bdb6e009bdec9'
            '8beb9bdb7e005d89f050558bec9bd96e025d58eb938d46f0f7dae80aff89cb8d56f089c8558be89bdb6e008b'
            'ea9bdb6e009bdef98bebebca535189c189d381fa00107f1081fa00f07c1689da89c8e848ff595bc3ba0010e8'
            '3fff81eb0010ebeaba00f0e833ff80c710ebdf5589e5565783ec0250525189c68b14ff46f48956fa31d2f644'
            '0404741d3b5efa7d09f64404107503895efa8b76f42976fa837efa007d03e991008b46f485c07f03e99a0039'
            'c37d03e923018b76f68b7e0489c1578cd88ec0d1e9f3a513c9f3a45f8b76f48b7ef801f289750c29f3f64504'
            '08755f837efa007e5c89d603760442c6042e8b46fa39c37e0289c38b76f68b7e0489d90376f401d7578cd88e'
            'c0d1e9f3a513c9f3a45f8b76f801da89540e8b76fa29de8b5ef88977108b5e0401d3c607008d66fc5f5e5dc2'
            '0200c746fa0000e967ffe9fb00f6450410759eeba58b76f8f644040874618b76f889540e8b76f48b46faf7de'
            '39c67e068946f4f75ef48b7ef88b76f489750c0176fa8975108b46faf75d1039c37e0289c38b7e048b76f689'
            'd901d7578cd88ec0d1e9f3a513c9f3a45f8b76f8895c128b76fa01da29de8b5ef8897714e972ff89d6037604'
            '42c60430837efa007e0b89d603760442c6042eeb858b76f8f644041075ece979ff8b76f68b7e0489d9578cd8'
            '8ec0d1e9f3a513c9f3a45f8b76f8895c0e8b76f401da29de8b5ef88977108b76f88b5ef4895c0cf644040875'
            '17837efa007e1d89d3035e04c6072e8b5ef842c7471201008b76f88b5efa895c14e9f1fef644041075ddebec'
            '8b7604803c307403e9a7fec7450c0000e99ffe5a25bc26c526ce265589e5565783ec0450525189da89c689c3'
            '8b44028b1f85c07f03e93b0229c3438b76f68a4404c746fa0000a804741239da7d06a810750289d34b85db7d'
            '03e91c028b76f68b440285c07f03e914028946f839c27d038956f88b7e048b4ef88b76f4037efa578cd88ec0'
            'd1e9f3a513c9f3a45f8b76f88b7ef60176fa0176f48b450229f239c67d238b7e0489c6b0302b76f8037efa89'
            'f18976f8571e078ae0d1e9f3ab13c9f3aa5f0176fa8b7ef68b76fa8a450489750ca808751385db7f03e9b001'
            '8b76fa037604ff46fac6042e8b76f68b440285c07d03e9a10185db7e3439d37d0289da85d2741c8b7e048b76'
            'f489d1037efa578cd88ec0d1e9f3a513c9f3a45f0156fa8b76fa8b7ef629d389750e895d108b5ef6837f0600'
            '74118b5efa8b76f6035e048a5406ff46fa88178b56fa42837ef2007c208b5efa035e048956fac6072b8b76f6'
            '8b740883fe03771989f3d1e32effa7f923e94a01817ef2e8037d03e95201be04008b5ef689770883fe047c42'
            '8b46f2c746f800003de8037c24bbe80399f7fb89c3d1e3d1e3b10329c3d3e301c3d3e389dad1e3d1e301d389'
            '46f8295ef28a56f88b5efa80c230035e04ff46fa881783fe037c3c8b46f2c746f800003d64007c1ebb640099'
            'f7fb89c2d1e2d1e2b10329c2d3e201c2d1e2d1e28946f82956f28a56f88b5efa80c230035e04ff46fa881783'
            'fe027c348b46f2c746f800003d0a007c16bb0a0099f7fb89c2d1e2d1e201c2d1e28946f82956f28a56f88b5e'
            'fa80c230035e04ff46fa88178b5efa8a56f2035e0480c23088178b5ef6ff46fa8b570e8b5efa8b76f629d389'
            '5c128b5e04035efae97bfc01c3e9c3fd31dbe9dffd8b76fa037604ff46fac60430e939fea8107403e949fee9'
            '52fe8946f88b7e04f75ef8b030037efa8b4ef88b76f8571e078ae0d1e9f3ab13c9f3aa5f0176fae937fe8b5e'
            'faf75ef2035e048956fac6072de98dfebe0300e9abfe837ef20a7c03be0200837ef2647c03be0300817ef2e8'
            '037d03e98ffee989fe5589e551565781ec800089c68956f6895eee50558bec9bd97e02909b5d588946ec80cc'
            '0389d350558bec9bd96e025d58c7470a00008b44088946cc8b44048946c88b44068946ca8b048946c48b4402'
            '8946c6f646cd807409c7470affff8066cd7f8b5ef6c7470e0000c7471000008d46c4c74712000031f6c74714'
            '000031ffc7470c0000e8871089c23d0300756e83fa03755d8b5ef6f64704807457b84e008b5eee88078b5ef6'
            'f64704807449b841008b5eee8847018b5ef6f6470480743ab84e008b5eee8847028b5eeec64703008b5ef6c7'
            '470e0300804f05018b46ec50558bec9bd96e025d588d66fa5f5e595dc3e96f01e95201e95501e958013d0400'
            '748df64705027503e95602c646f8303d02007403e98e01c646f8318b46c48946e28b46c68946e48b46c88946'
            'e68b46ca8b5ef68946e8f64704207503e996018b46cc2dff3f8946f48a46e9b10430e4d3f8250f003d090077'
            '03e9730189c283ea0af64704807503e95f01b8410001d08b5ee68b4ee48b56e2be04008846f88b46e8836ef4'
            '04e8e00f8946e8895ee6894ee48956e28b5eeec6073043895ef28b5ef6f64704807503e95e01b858008b5ef2'
            'ff46f288078b5ef28a46f88807ff46f2837ee8007515837ee600750f837ee4007509837ee2007503e939018b'
            '5ef2ff46f2c6072e837ee8007512837ee600750c837ee4007506837ee20074dc8a46e9b10430e4d3f830e424'
            '0f3d0900764d89c28b5ef683ea0af64704807442b8410001d08b5ef28807be04008b46e88b5ee68b4ee48b56'
            'e2ff46f2e82d0f8946e8895ee6894ee48956e2eb9bb86e00e951feb86100e95cfeb86e00e968fee9b000b861'
            '00ebbc8b5ef6f64704807429b849008b5eee88078b5ef6f6470480741db84e008b5eee8847018b5ef6f64704'
            '807410b84600e92efeb86900ebd5b86e00ebe1b86600e91efe3d01007503e96efe85c07403e9e0fe89470a89'
            '46f48946e88946e68946e48946e2e9cbfeb86100e99efe053000e99afe8d56e28d46c4b104be0c00558be89b'
            'db6e008bea9bdd5e005d8b46e88b5ee68b56e2d3e88b4ee40501fc8946f48b46e8e97dfeb87800e99ffe0530'
            '00e90dff8b5ef68a47068b5ef2ff46f28807837ef4007c098b5ef2ff46f2c6072b8b46f4b90a008b5ef2998b'
            '7ef2e87df68cd88ec083c9ff33c0f2aef7d149014ef28b76f28b5ef62b76ee89770ee97bfd3d020075398b46'
            'ccbb97752dfe3f31c98946f499e8a2f4bba086b90100e8f70d8946f42d04008946f4744785c07c2b8b46cc3d'
            '19407320c746f40000eb343d010075088976f489770aeb2785c075238946f489470aeb1be9b001f7d8050300'
            '24fc8946f4f75ef48b56f48d46c4f7dae835f78b5ef6f64704027503e9a3028b0f034ef48b470283c10a85c0'
            '7e0201c18b5ef6b81400f64704207403b817008b5ef6f64704407402d1e005040039c17e03e97602c6867aff'
            '3030c08d9e7bff88867bff30e4895ef08946ea85c97e5c83e90885ff750485f674548b5ef089f089fa8346ea'
            '0851e80200eb31b91027f7f152e8010058b964002bd23bc192720392f7f152e8010058b10a3ac186e0720486'
            'e0f6f10530303689074343c35932c0368807895ef031f631ffeba0e90700f746ccff7f75178b46ea8db67bff'
            '8346f407803c30750aff4ef44846ebf4e9e0018b5ef68b7ef68b1ff6450402744b8b55020156f48b56f44201'
            'd385db7c3e39c37e0289c38b7ef6ba1400f64504207403ba17008b7ef6f64504407402d1e239d37e0289d3b2'
            '3039d87f1589df89d801f74f483a15750c4bebf7e92b02e90e00e94a0280fa397502fe0585c07c3885db7e37'
            '8b7ef68a4504a8027519a804742c8b46f43dfcff7c043b057c098b7ef6f64504087417ff76ee8b4ef48b46f6'
            '89f2e8eaf5e988fbe91502e91a02ff76ee8b4ef48b46f689f2e8bff7e971fb75148b46ca3dbcbe7303e920fe'
            '7507817ec8002072f48b46cc3d34407203e9c500c746e01940c746dc00208d5ece31c08d56d88946d88946da'
            '8d46c4c746debcbe558be89bdb6e008bea9bdb6e009bdef98beb9bdb7e005d8d46ce8d5ece558be89bdb6e00'
            '5250508bec9bd97e00909b585080cc0c8946029bd96e029bdb5e029bd96e0058585a5d89c689d75552508bec'
            '9bdb4600585a8beb9bdb7e005d8d5ed88d56d88d46ce558be89bdb6e008bea9bdb6e009bdec98beb9bdb7e00'
            '5d8d5ec48d56d88d46c4c746f40800558be89bdb6e008bea9bdb6e009bdee98beb9bdb7e005de982fd75288b'
            '46ca3d1b8e7303e92eff7507817ec8bfc972f43d1b8e750f817ec8bfc975088b46c63d000472e08066f4fce9'
            '46fd8b0f83c108e966fd89c1e985fd8d46c4558be89bdb6e005250508bec9bd97e00909b585080cc0c894602'
            '9bd96e029bdb5e029bd96e0058585a5d89c689d785c97f03e973fd8d5eba5552508bec9bdb4600585a8beb9b'
            'db7e005d8d5ec48d56ba8d46c4558be89bdb6e008bea9bdb6e009bdee98beb9bdb7e005dc746c21940c746be'
            '00208d5ec431c08d56ba8946ba8946bc8d46c4c746c0bcbe558be89bdb6e008bea9bdb6e009bdec98beb9bdb'
            '7e005de904fdf64504017503e98afd837d02007e11438b56f48b7ef6422b55028956f4e973fd035d02ebeb89'
            'f701df803d357d03e992fdb239e98dfdff46f44e43e9a8fdbb0100c6867aff308b76f6c746f40000c7440a00'
            '008db67affe990fd5351565789c389d130f68a1788d030e489c6f684610202740343ebee8d470180fa2b7532'
            '89c38a073c6e753a438a073c61752d438a073c6e75284380ce20803f2875214389de8a0430e489c7f6856102'
            'e0745646ebf080fa2d75cb80ce01ebc4eb49eb58eb4b3c4e74c23c697555438a073c6e7550438a073c667558'
            '8d77018a0480ce403c69754e468a043c6e7549468a043c697544468a043c74753f468a043c79755589f343eb'
            '08eb1c3c417484b68089ce88f0891c5f5e595bc33c4e75efe977ffeb19eb343c5f74883c2975e28d5c01ebdd'
            'eb2ceb31eb36eb3beb403c49748c3c3075cb8a47013c7874043c5875c080ce1083c302ebb8eb2a3c4e75b0e9'
            '73ff3c4675a9e973ff3c497599e978ff3c4e7592e978ff3c49758be978ff3c547584e978ff3c597503e978ff'
            'e977ff56575589e583ec168846fe8956f6895ef8894ef48b7ef889d630db31c031c98946fc8946ee897ef0b4'
            '308a04463c2e750af6c308751e80cb08ebef807efe007503e9ad0088c230f689d7f6856102107403e9ad00f6'
            'c3047503e94401807efe007403e9d4003c657403e9c6008d44ff8946f28a048d54013c2b7403e9cd0089d680'
            'e3fb807efe007462b810278946ec8a3c88f830e489c7f6856102207550f6c3027402f7d9f6c30475038b76f2'
            '807efe007463b810008946fa745dbf04008b46eef7ef29c18b46fc897eea3b46fa7f5d8b7efc037ef8837efc'
            '007e53807dff30754dff4efc034eea4febebe96a00e96d0088c230f689d7f6856102207503e953fff6c30874'
            '03ff46ee08c480fc30750a80cb04e914ffeb67eb6b837efc207d058b7ef08805ff46f0ff46fcebe3eb5ce971'
            '003c457503e933ff807efe0074083c7074f33c5074ef4ee962ff3c2d7403e92eff80cb02e926ffb8d007e92e'
            'ff3b4eec7c0780cb0446e925ffba0a0089c8f7ea89c288f89889c101d183e930ebe4b81300e931ffbf0100e9'
            '33ff8b46faf7ef8b56fc29c28b46fa01d18946fce930ff8b76f68b7e08890d8b7e0a8b46fc89058b7ef488d8'
            '893589ec5d5f5ec204005589e551565783ec3a89c789d6895ef48d56ee8946eee8f9fc8846f888c480e4108b'
            '7eee8866f6a8017455b880008946f28a46f8a8607451c74408ff7fc744040000c744060000c7040000c74402'
            '0000a8207430bf0100804c0780f646f8017404804c0980837ef40074088b5ef48b46ee89078b46f209f88d66'
            'fa5f5e595dc331c0ebaabf0200ebd28d46f08d4eee508d46ec8d5ec0508a46f689fa98e8a1fd0846f8837ef4'
            '0074088b5ef48b46ee89078b46f085c0745889c7c643c000807ef60074608d5eec8d56e28d46c0e81b07f646'
            'f8017404804eeb808b46ea8944088b46e68944048b46e88944068b46e289048b46e48944028b46ec0346f048'
            '3d44137f3b3dbcec7d3e8b46f20c04e968ff89440889440489440689048944028b46f2e954ff8d56e28d46c0'
            'e8bb078b46ec85c0749c89c28d46e2e84eefeb928b46f20c05e932ffb80300e92cff5589e55383ec2289d38d'
            '56dce8a5fe89c380e37f89c285db751685db750f895eec895eea895ee8895ee6e9a400e9800083fb0174e583'
            'fb0274e08b46e480e47f3dff437d633dcd3b7d603dccff7c5e8d56e68d46dc558be89bdb6e008bea9bdd5e00'
            '5d8b46ec8946f48b46ea8946f28b46e88946f08b46e68946eef746ecff7f7510837eea00750a837ee8007504'
            '85c07407f746f4f07f7503e86f029bdd46e6909b8d66fe5b5dc3eb4ce98c00e9790083fb0175268b16780389'
            '56ec8b1676038956ea8b1674038956e88b1672038956e6a88074c78076ed80ebc18b1670038956ec8b166e03'
            '8956ea8b166c038956e88b166a03ebd8e81202f6c28074109bdd0624029bd9e09bdd5ee6909beb8ea12a0289'
            '46eca128028946eaa126028946e8a124028946e6e973ffe8df0131c08946ec8946ea8946e8ebea8d56e68d46'
            'dc558be89bdb6e008bea9bdd5e005d83fb057503e944ff8b46ec8946fc8b46ea8946fa8b46e88946f88b46e6'
            '8946f6f746fcf07f7503e894018b46fc25f07f3df07fe914fffbc3008bc88bc28be5874e148746168b5e1c81'
            'e3fffc539d5a5a071f5f5e5d5b5b5acb5289da1e8ed9b425cd211f5ac353b435cd2189d88cc25bc331c0c351'
            '5289c231c083fa05730dd1e2b10389d0d3e029d00548055a59c35589e55351565783ec0289c689d38b7f0483'
            '7d0400744d31fff6470704754c8976f631c98a0430e485c0740d89da46e846003dffff75ed89c185ff741380'
            '6707f9804f070485c9750789d8e83a0689c185c9750589f12b4ef689c88d66f85f5e595b5dc389d0e80907eb'
            'ac806707f9bf0100804f0702eba75351565789c189d38b77048b44063d0100740c85c07403e96f00c7440601'
            '00f64706027503e969008b7704837c0400750589d8e8c406be000483f90a7528be0006f6470640751f8b3f80'
            '4f0710c6050dff07ff47028b47023b470a750989d8e8ae0585c075238b3f804f0710880dff07ff4702857706'
            '75088b47023b470a752089d8e88b0585c07417b8ffff5f5e595bc3c706e2060400b8ffff804f0620ebec88c8'
            '30e4ebe6c706e2060900b8ffffc3c706e2060d00c3c706e2060e00c35589e55783ec0e8d7ef09bd935f7d08d'
            '7ef02146f29bd92531c08d66fe5f5dc35589e5535783ec0289c38d7efadd3d2356fa83e23f31c089178d66fc'
            '5f5b5dc35589e55783ec0e8d7ef030e49bd935243f8d7ef00946f29bd9259b30c0ebbb5589e55783ec028d7e'
            'fcd93d343f8066fcc08d7efc0946fc9bd92deb9e5589e55783ec028d7efcd93d8066fcc08d7efc0946fc9bd9'
            '2deb835589e5535783ec0e89c38d7eee9bd9358066f0c0231730f631c080e23f8d7eee0956f09bd925e979ff'
            '5589e55283ec0289c28d46fce851ff8b46fc8d66fe5a5dc35589e55783ec028d7efcd93d8b46fc30c080e40c'
            'e92bff5589e55783ec023d0004730a85c07412b80100e915ff760a3d000c74053d000875ee8d7efcd93d8066'
            'fdf38d7efc0946fc9bd92de9f2fe5789c79bd93531c05fc35589e5535783ec0289c389c79bd9358b070c3f8d'
            '7efa8946fa9bd92d31c0e9e8fe5789c79bd925ebd35589e55783ec028d7efcdd3d89c79bd9258b46fc253f00'
            'e8cdfee9a2fe5352bb010131c031d2a36006a34605b8e406e81d055a5bc35589e553515250c746f80000eb0a'
            'ff46f8817ef80001731f8a46f830e4509affff000085c074e78b5ef8c70660060100c687e50601ebd78d66fa'
            '5a595b5dc353515289c2e8a1ff83faff743e83fafe743983fafd744983fafc7526bb1f00ba0100b86607e8b3'
            '04bb1d00ba0100b8c507e8a704c70660060100c7064605a403eb1e85d27405b80100eb179affff00003d030a'
            '72f1e869ff9affff0000a3460531c05a595bc35389c38b470880e47f3dff7f751e817f0600807512837f0400'
            '750c8b47020b077505b804005bc3b803005bc3f74708ff7f75148b47060b470475078b47020b0774e3b80100'
            '5bc3b802005bc35589e58d4604e8abff5dc2080085f6740bd1e2d1d1d1d3d1d04e75f5c30bd278160bc97941'
            'f7d9f7db83d900e83700f7daf7d883da00c3f7daf7d883da000bc97912f7d9f7db83d900e81a00f7d9f7db83'
            'd900c3e80f00f7d9f7db83d900f7daf7d883da00c30bc9751a4b7416433bda77098bc88bc22bd2f7f391f7f3'
            '8bda8bd12bc9c33bca721a75103bd8770c2bc38bd82bc92bd2b80100c32bc92bdb9387cac355562bf68bee03'
            'db13c97211453bca72f577043bd876eff813f64d7820d1d9d1db2bc319caf572f003f64d780cd1e9d1db03c3'
            '13d173f1ebdf03c313d18bd88bca8bc633d25e5dc3539889c32d30009985d275053d090076118d47c99985d2'
            '75053d0f0076048d47a9995bc35589e551565783ec105253c746f41c0089c789c68cd88ec083c9ff33c0f2ae'
            'f7d149894ef68b0731ff31db8946f8895eee837ef600753231c0c746f21c008946ea8946ec8946f0837ef600'
            '7548f7c700807570d1e3d1d7f646ed807568d166ead156ec8346f8ffebe4ff4ef68b4ef48a04ff46ee98836e'
            'f404e860ffe306d1e0d1d2e2fa4609d709c38346f804837eee087c9eeba2ff4ef68b4ef28a04ff46f098836e'
            'f204e834ffe306d1e0d1d2e2fa460956ec0946ea8346f804837ef0087c86eb8aeb0580cb01eb938b76e88b56'
            'ea89148b46f8895c048b56ec89f3895402ff4ef8897f0605fe3f8947088b5ee68b46f889078d66fa5f5e595d'
            'c3005556575153528be82bd28bfa8bca8bda8bf28a46003c007449525351578bc603f613ff13c913db13d203'
            'f613ff13c913db13d203f05813f85813c85813d85813d003f613ff13c913db13d28a460083e00f03f083d700'
            '83d10083d30083d20045ebb08bc28bd7bf4e40e816005d897608894606895e04894e028956005b595f5e5dc3'
            '8be80beb0be90bea0bee74390bc0750f8bc38bd98bca8bd62bf683ef10ebed0bc0780d03f613d213c913db13'
            'c04febef13f683d20083d10083d30083d0007303b480478bf7c35589e5535152565783ec0289c6c746f40000'
            'f64407107403e96d008b5c04837f0400743b806406eff644072075318b44029989d309c3741489d189c3ba01'
            '00f7d98b4408f7db83d900e80b0283faff750e3dffff7509c746f4ffff804c06208b5c048b4704c744020000'
            '8904837ef40075098b5c04f6470801755f8b46f48d66f65f5e5a595b5dc3806407eff644060274cd8b7c048b'
            '450485c074c389c78b4c0285c974ba837ef40075b489cb89fa8b4408e8e10189c23dffff750d8946f4804c06'
            '2001d729d1ebd885c075f6c706e2060c00c746f4ffffebe58b4408e82d023dffff75968946f4eb91535689c3'
            'e81f02837f0a00750c8a4707a8027440c7470a86008b470ae82c028b77048944048b7704837c040075388067'
            '07f88d470c8b7704804f0704894404c7470a01008b77048b4404c74702000089075e5bc3a8047407c7470a40'
            '00ebbac7470a0002ebb3804f0608ebd8515789c789d988d0571e078ae0d1e9f3ab13c9f3aa5f89f85f59c353'
            '52565780266b05f8be4805800e6b05048b440685c07508a3e8075f5e5a5bc3b80c00e89e0185c0742a89c7a1'
            'e6078975028905897c04c745040000893ee6078b7c04c6450a008b7c0483c60ec745060000ebbdb80c00e86a'
            '0189c785c075ccbb0100b8b8018cdae971ca5589e5535152565783ec02d1e0b10389c2d3e08b1ee60729d0ba'
            '480531f601c231c98956f485db742e8b3f8b5f028a4707ba0100a840740c89d8e865024109c689fbebe1a808'
            '75f03b5ef472f381fb8e0573e531d2ebe185f6740fbbffff89d88d66f65f5e5a595b5dc389cbebf0b80500eb'
            '8d31c0e888ffe97e025589e5565783ec045089de89c889d38d7ef88b5ef688d089f2b442cd21368905368955'
            '02d1d2d1ca85d27c0d8b46f88b56fa8d66fc5f5e5dc3e8590299ebf35589e551565783ec0289c68956f889df'
            'e89c02a880753c833e940400740e89f0ff167e0485d2754485c075408b56f889f989f3b440cd2119d285d27c'
            '2a89c239f87406c706e2060c0089d08d66fa5f5e595dc3b00289f331d231c9b442cd21d1d2d1ca85d27db0e8'
            'ec01ebdf8b5ef889f9ff169404ebd4e96d025389c3f647072074025bc38b4708e8690285c074f4804f0720f6'
            '47070775ea804f07025bc35589e5535152565783ec0489c785c074523deaff774d05030024fe8946f23d0600'
            '724cc646f40031d28b46f23b066606764e8b36640685f6743c85f674538b4c0a8936640639f9720f8cda89f3'
            '89f8e8410289c285c0755e3b0e6606772d8b7404ebd731c08d66f65f5e5a595b5dc3c746f20600ebad893666'
            '068b366206ebba31c08b366206a36606ebaf890e6606ebcd807ef400740f89f8e8390485c07416c646f400eb'
            '8389f8e81d0585c074e8c646f401e973ff89d0ebab5589e55351565783ec0489c68956f6f6440603746931ff'
            'f644071075668b440285c07416f7d88b5c0899895ef489d189c3ba01008b46f4e82efe837ef60074088b4408'
            'e8f20409c7f644060874118b5c048b4704e82f058b5c04c747040000f6440708740689f0ff16ea07c6440600'
            '8064074089f88d66f85f5e595b5dc3bfffffebf0e88bfb85c07493bfffffeb8e5389c3e86bff89c289d8e826'
            '0089d05bc353528b1ee60785db74123b470274048b1febf3ba0100e8d6ff5a5bc3b8ffff5a5bc35356bee607'
            '8b1c85db74163b4702740489deebf18b078904a1e807891ee80789075e5bc3538b1ee80785db75025bc38b07'
            'a3e80789d8e88b04ebea535289c288c330ff891eee073d00017338803ea20303722b3c507504b00eeb063c22'
            '7217b01388c330ff8a8768069889c2b8ffff8916e2065a5bc33c207204b005ebe33c1376dfebdb88e230e6eb'
            'e25289c2e8abff89d05ac3533b067c06730c8b1ea606d1e001c38b075bc331c05bc3533b067c0672025bc38b'
            '1ea606d1e001c389175bc35389c3b43ecd21e8c1045bc35389c3b468f8cd21e8b4045bc3535289c3833e7e04'
            '00751db80044cd2119db750289d089c285db7f027518a8807414b801005a5bc3ff167e0485d275f285c074d7'
            'ebec31c05a5bc30000005156571e5589e583ec0289dec746fe000085c07503e96c0089c783c70383e7fe39c7'
            '72608eda8b440a83ff067303bf060039c7774f8eda8b4c088b5c0639cf76508eda8b0739c7774f894c0829f8'
            'ff440c3d0600725e89d901f9894c0689ce8904893f89cf8b77028975028b4704894504894c0489c6894c028e'
            'da800f0183c302895efe8b56fe89d089ec5d1f5f5e59c38b5c1431c9eba939c1730289c189f08eda0510008b'
            '5f0439c375958b56fe894c0aebd3ff4c0e8b7f02897c068b7704897504897c02ebb15156571e5589e583ec06'
            '89d1895efe85c07503e9840089c68eda83ee02f6040174f18b0489f324fe01c3f607017403e97e0003078b7e'
            'fe89043b5d0675038975068ed98b7f028b5f04895d04897f028b7efeff4d0e8ed98b7f0289fa8b040315897e'
            'fc39d6753b89fb030789078b5efe3b57067503897f068b76fc8b5efe8ed9ff4f0c3b770673083b4708760389'
            '47088b5efe8ed93b470a770b89ec5d1f5f5e59c3e9bd0089470a89ec5d1f5f5e59c38b5efe89048b5f0639de'
            '7203e96c003b770277918b5efe8b5f1439de72878b5efe8ed98b5f0e89d88b7efe4031d28946fa8b450cf776'
            'fa39d873188b550c29dad1e039d3734e8ed98b1c8ed901f3833fff75468b5efe8ed98b5f0639de724b8ed939'
            'de7303e93dff8b5f0439de72f68b5f0439de72ef8b5f04ebe68b5f0439de72e38b7efe83c7108b450289fb39'
            'c6768de911ffb8ffffebadf6070174c78b1780e2fe01d34874abeba48b5efe8b5f14ebad8b7efeff450e8b7e'
            'fc895c04897c02897504897702e901ff31c0c353565789c68b1e620631ff85db740e39de720789df8b5f04eb'
            'f1897702897c02895c0485ff7439897504c744100000c744080000c7440c00008d4410c7440e00008944128b'
            '1c89441483eb16894406895c1601f38d4416c74716ffff5f5e5bc389366206ebc4535289c38b1789d0050300'
            '24fe39d0722089c2051e0039d072173b06a8067305a1a80624fe890785c07403b801005a5bc331c05a5bc353'
            '515289c331c050ff379affff00005a595bc35589e5535650e88d018d46fae8deff89c385c075078d66fc5e5b'
            '5dc38b46fa2d0200890789d8e828ff89c6800c01c7470a0000050200ff470ce87d00b80100ebd45589e550a1'
            'aa0685c07409833e9e03fe750631c089ec5dc38d46fee858ff85c074f28b46fee897ffebea5351525689c189'
            'c3b43ecd2119d231f685d27c15833e840400751589c831d2e813fc89f05e5a595bc3e899fb89c6ebeb89c8ff'
            '167e0489c385d2750485c074db89c8ff16800489d8ff168404ebcd53525685c074558b36ec0785f6743439c6'
            '77088b1401f239d0724389f38b740285f6740c39c677088b1401f239d0722e8b770485f6740c39c677088b14'
            '01f239d072518b36640685f675158b36620685f6740939c6765a8b7404ebf3eb4feb3439c677088b1401f239'
            'd0722889f38b740285f6740c39c677088b1401f239d072138b770485f674c339c677bf8b1401f239d073b78c'
            'da89f3e880fc8936ec073b3664067308a166063b440a720e5e5a5bc38b1401f239d072dbeb9c8b440aa36606'
            'ebeab8ee07c3730750e8aafa58eb022bc0c37305e89ffaeb022bc0c3535152568b36620685f6743c8b048b5c'
            '122d16008b4c043b07741889ceebe9535152509affff000085c07403b8ffff5a595bc389f08b5402e8e4ff85'
            'c075dc89cb89f0e81300ebd331c05eebe25289c2a1aa068916aa065ac35689d63b06620674323b0664067514'
            '8936640685f6750c8b16620689366606891664063b06ec07750531c0a3ec0785f67403895c0485db75085ec3'
            '891e6206ebc88977025ec3000b010507a233050000000507a733050000000507aa33050000000507ae330600'
            '00000507e633050000000507ec3305000000030163370300cf000301cf37030003000301dc37040084000301'
            'bc430300050003017e45030007000200cc1c0100a01b0507561d0500000005075a1d06000000050724210500'
            '000005072a210500000005072e2105000000050733210500000005075b210500000005075f21060000000507'
            '6c210500000005079d21050000000507a321050000000507a721050000000507ac21050000000507b7210500'
            '00000507d121050000000507d721050000000507db21050000000507f126050000000507f526060000000507'
            '0527050000000507bd27050000000507bd29050000000507c329050000000507892c0500000005078f2c0500'
            '00000507932c050000000507982c050000000507a62c050000000507af2c050000000507b32c060000000507'
            'bd2c050000000507c12c050000000507c52c050000000507d62c050000000507de2c050000000507ef2c0500'
            '00000507f52c050000000507f92c050000000507fe2c050000000507142d0500000005071a2d050000000507'
            '1e2d050000000507232d0500000005076f2d050000000507782d0500000005077c2d060000000507862d0500'
            '000005078a2d0500000005078e2d050000000507a92d050000000507b12d050000000507c22d050000000507'
            'c82d050000000507cc2d050000000507d12d050000000507f92d050000000507ff2d050000000507032e0500'
            '00000507082e050000000507fc32050000000507023305000000050740330500000005074433060000000507'
            '39050500000005073c050500000005073f05050000000507440505000000050747050500000005074a050500'
            '000005074d050500000005075005050000000507530505000000050757050600000005076405050000000507'
            '68050500000005076e050500000005077205050000000507750505000000050778050500000005077b050500'
            '000005077e050500000005078105050000000507840505000000050787050500000005079305050000000301'
            'b90503005b000301f80503001e000301ff0501000500020019060100b40503012a060300030003015c060300'
            '310003014c07010001000507f107050000000507f407050000000507f807050000000507fc07060000000507'
            '1108050000000507150806000000050717080500000005071b080600000005072b080500000005072e080500'
            '0000050732080500000005073608060000000507210905000000050725090500000005072b09050000000507'
            '2e090500000005073209060000000301dd0a030084000501320b020001000201340b0200010005074b0d0500'
            '00000507500d050000000507530d050000000507570d060000000507990d0500000005079d0d060000000507'
            'e61a050000000507ec1a050000000507381b0500000005073b1b060000000200681b0200bc000200b01b0200'
            'bc0005007f1c0100581b0200821c0100581b0500c91c0100a01b05070d030500000005071003050000000507'
            '1b03050000000507250305000000050733030500000005074c03050000000507540305000000050760030500'
            '0000050767030500000005076a030500000005076d0305000000050778030500000005077d03050000000507'
            '80030500000005078b03030000000507910305000000050794030500000005079703050000000507a2030500'
            '00000507a903030000000507af03050000000507b203050000000507bd03050000000507c203050000000507'
            'c503050000000507cc03050000000507d103050000000507d803050000000507e503050000000507f5030500'
            '00000507f903060000000507020405000000050706040500000005070c040500000005071004060000000507'
            '3104050000000507340405000000050737040500000005073b04060000000507430405000000050747040500'
            '000005074c040500000005075004060000000507520405000000050756040600000005018f04020001000201'
            '9104020001000501b904020001000201bb04020001000507e004030000000507e604050000000507ea040600'
            '00000507f60405000000050706050300000005070c0505000000050710050600000005071805050000000507'
            '240505000000050727050500000005072a050500000005072d05050000000507300505000000050733050500'
            '0000050736050500000005071e00050000000507230005000000050728000500000005073000050000000507'
            '34000500000005073c00050000000507400005000000050745000500000005074d0005000000050751000500'
            '0000050756000500000005075900050000000507630005000000050766000600000005076e00050000000507'
            '750006000000050788000500000005079300050000000507a700060000000301e200010001000507e6000500'
            '00000507ea00050000000507f200050000000507f60006000000030148010200010003015501020001000301'
            '63010300840003018201020001000507c901050000000507cd01050000000507dc01050000000507df010500'
            '00000507e301060000000507eb01050000000507f30105000000050701020500000005074102050000000507'
            '5302050000000507630203000000050769020500000005076c020500000005076f020500000005077a020500'
            '00000507840205000000050787020500000005079d02050000000507a002050000000507b102050000000507'
            'b402050000000507b702050000000507ba02050000000507bd02050000000507c002050000000507c3020500'
            '00000507c602050000000507de02050000000507e102050000000507e402050000000507e802050000000507'
            'eb02050000000507ee02050000000507f60205000000050704030300000005070a0305000000000000000500'
            '00000000000000000000252e336620252e346620256c6420252e3566202567206377202530347800436f7072'
            '6f636573736f7200000000000000000000000000244061732d3852c1e03f0000000080842e41000000000000'
            '0840000000000000e03f000000000000f0bf4f70656e20576174636f6d003a8c30e28e79453e466c6f617469'
            '6e672d706f696e7420737570706f7274206e6f74206c6f616465640041424e4f524d414c205445524d494e41'
            '54494f4e0000446f6d61696e206572726f7200417267756d656e742073696e67756c6172697479004f766572'
            '666c6f772072616e6765206572726f7200556e646572666c6f772072616e6765206572726f7200546f74616c'
            '206c6f7373206f66207369676e69666963616e6365005061727469616c206c6f7373206f66207369676e6966'
            '6963616e6365006e756c6c0061636f73006173696e00737172740065787000636f73680073696e6800706f77'
            '0061636f7368006c6f6732006c6f67006c6f673130006174616e68006174616e320069706f770064706f7769'
            '00636f730073696e0074616e00793000793100796e006d6f6400636f74616e0020696e2000004e6f7420656e'
            '6f756768206d656d6f727920746f20616c6c6f636174652066696c652073747275637475726573000000bc00'
            'c900de00f30009012401410146014b015001550159015e01630167016d01720176017c01820188018d019301'
            '97019b019f01a201a501a801ac01000000000000f07f7f120000ffff00000000000000003031323334353637'
            '38396162636465666768696a6b6c6d6e6f707172737475767778797a00000001010101010101010103030303'
            '030101010101010101010101010101010101010a0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c383838383838383838'
            '380c0c0c0c0c0c0c58585858585848484848484848484848484848484848484848480c0c0c0c0c0c98989898'
            '989888888888888888888888888888888888888888880c0c0c0c010000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000807f0000c07f000000000000f07f00000000'
            '0000f87f000000000000f07f000000000000f87f000000000000044000000000000010400000ffff00000000'
            '000000000000000c000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '000001000000000000005f095f0900007f120100020002000200020002000200020001000100010002000200'
            '000000000000000000000000000000000000000000000000000000000000000000004e194e193f1b3f1b0000'
            '00000000000000006234000000000000000000a0024000000000000000c80540000000000000409c0c400000'
            '00000020bcbe194000000004bfc91b8e34409eb5702ba8adc59d6940d5a6cfff491f78c2d340e08ce980c947'
            'ba93a8418edef99dfbeb7eaa5143c7910ea6aea019e3a346170c7581867576c9484de55d3dc55d3b8b9e925a'
            '9b97208a025260c425750000000000000080ff7f010004000000000000000000010000000000000000000000'
            '0000020001000000000000000000000002000200000000000000000000000300030000000000000000000000'
            '0200040000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '000000000000000901010b06040505050203ff0e0724060801ff140001000200020003000200000000000000'
            '0000000000000000000000000000000000000000000000007e060020010000200401000000023c0100000020'
            '810a00000005ce0a00000006c60d00000020c82000000020793c000000027b0100000020573d0000'
        )),
    'OWAPI': (
        # wcc -bt=windows -ms owapi.c; wlink @owapi.lnk; wrc -bt=windows owapi.rc owapi.exe
        # ---- owapi.c
        #     #include <windows.h>
        #     #include <string.h>
        #
        #     /* not in Open Watcom's windows.h: KERNEL's own (imported in owapi.lnk) */
        #     DWORD FAR PASCAL GlobalDOSAlloc(DWORD bytes);
        #     UINT FAR PASCAL GlobalDOSFree(UINT sel);
        #     WORD FAR PASCAL GetExpWinVer(HINSTANCE inst);
        #     UINT FAR PASCAL LocalHeapSize(void);
        #
        #     /* KERNEL and USER beyond the basics: what a program shows in its window,
        #      * line by line; two dialogs with an lParam of their own (one from its
        #      * resources, one from a template in memory); a dialog of a class of its
        #      * own (DLGWINDOWEXTRA), its window procedure calling DefDlgProc: the
        #      * dialog procedure gets its commands, WM_CLOSE's IDCANCEL too; a
        #      * resource freed while ES still has it (as CHARMAP does): ES 0 after;
        #      * hooks USER calls: WH_CBT (HCBT_CREATEWND: the window, its
        #      * CREATESTRUCT) and WH_CALLWNDPROC (WM_NCCREATE: the window subclassed
        #      * there, before its procedure has it, as MSHEARTS does); an overlapped
        #      * window's first WM_SIZE: not from CreateWindow, from ShowWindow (as
        #      * Solitaire needs) */
        #     static char lines[9][96];
        #     static int nlines;
        #     static HINSTANCE inst;
        #
        #     static void say(const char *s) { if (nlines < 9) lstrcpy(lines[nlines++], s); }
        #
        #     BOOL FAR PASCAL DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_INITDIALOG) {
        #             RECT rc;
        #             char s[64];
        #             SetRect(&rc, 0, 0, 4, 8);
        #             MapDialogRect(h, &rc);
        #             wsprintf(s, "%s %dx%d", (LPSTR)lp, rc.right, rc.bottom);
        #             SetDlgItemText(h, 100, s);
        #             return TRUE;
        #         }
        #         if (msg == WM_COMMAND && wp == IDOK) {
        #             EndDialog(h, 7);
        #             return TRUE;
        #         }
        #         return FALSE;
        #     }
        #
        #     static int own_class, own_cmd;
        #
        #     BOOL FAR PASCAL OwnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_INITDIALOG)
        #             return TRUE;
        #         if (msg == WM_COMMAND && wp == IDCANCEL) {
        #             own_cmd++;
        #             return TRUE;
        #         }
        #         return FALSE;
        #     }
        #
        #     long FAR PASCAL OwnClassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_COMMAND)
        #             own_class++;
        #         return DefDlgProc(h, msg, wp, lp);
        #     }
        #
        #     /* ES the resource's block, then FreeResource: what ES is after it */
        #     extern unsigned es_free(HGLOBAL h);
        #     #pragma aux es_free = \
        #         "mov es, ax" \
        #         "push ax" \
        #         "call far ptr FreeResource" \
        #         "mov ax, es" \
        #         parm [ax] value [ax] modify [bx cx dx es];
        #
        #     static unsigned freed_es(HINSTANCE hInstance)
        #     {
        #         HGLOBAL h = LoadResource(hInstance, FindResource(hInstance, "OWNDATA", RT_RCDATA));
        #         if (!h || !LockResource(h))
        #             return 0xffff;
        #         GlobalUnlock(h);
        #         return es_free(h);
        #     }
        #
        #     /* the hooks: the window HCBT_CREATEWND made, its cx; WM_NCCREATE's cx
        #      * the WH_CALLWNDPROC hook saw, the window subclassed then; the subclass
        #      * got WM_NCCREATE */
        #     static HHOOK cbt_hook, cwp_hook;
        #     static HWND made;
        #     static int made_cx, sent_cx, subclassed, sub_got;
        #     static FARPROC sub_old;
        #
        #     /* WH_CALLWNDPROC's lParam (not in Open Watcom's windows.h) */
        #     typedef struct { LPARAM lParam; WPARAM wParam; UINT message; HWND hwnd; } CWPSTRUCT;
        #
        #     long FAR PASCAL SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_NCCREATE)
        #             sub_got++;
        #         return CallWindowProc(sub_old, h, msg, wp, lp);
        #     }
        #
        #     DWORD FAR PASCAL CbtHook(int code, WPARAM wp, LPARAM lp)
        #     {
        #         if (code == HCBT_CREATEWND) {
        #             made = (HWND)wp;
        #             made_cx = ((CBT_CREATEWND FAR *)lp)->lpcs->cx;
        #         }
        #         return CallNextHookEx(cbt_hook, code, wp, lp);
        #     }
        #
        #     DWORD FAR PASCAL CwpHook(int code, WPARAM wp, LPARAM lp)
        #     {
        #         CWPSTRUCT FAR *p = (CWPSTRUCT FAR *)lp;
        #         if (p->hwnd == made && p->message == WM_NCCREATE && !subclassed) {
        #             sent_cx = ((CREATESTRUCT FAR *)p->lParam)->cx;
        #             sub_old = (FARPROC)SetWindowLong(p->hwnd, GWL_WNDPROC,
        #                                              (LONG)MakeProcInstance((FARPROC)SubProc, inst));
        #             subclassed++;
        #         }
        #         return CallNextHookEx(cwp_hook, code, wp, lp);
        #     }
        #
        #     static int sizes;
        #
        #     /* the other hooks: the dialogs' keys the message filter saw, keys the
        #      * keyboard hook saw; a posted message WH_GETMESSAGE changed; CBT's
        #      * activation, focus, destruction (one refused); the shell's window made
        #      * and gone; CallMsgFilter's answer */
        #     static HHOOK filter_hook, key_hook, get_hook, cbt2_hook, shell_hook;
        #     static int filtered, keyed, activated, focused, destroyed, refuse, shelled;
        #
        #     DWORD FAR PASCAL FilterHook(int code, WPARAM wp, LPARAM lp)
        #     {
        #         MSG FAR *m = (MSG FAR *)lp;
        #         if (code == 77)
        #             return 1;
        #         if (code == MSGF_DIALOGBOX && m->message == WM_KEYDOWN)
        #             filtered++;
        #         return CallNextHookEx(filter_hook, code, wp, lp);
        #     }
        #
        #     DWORD FAR PASCAL KeyHook(int code, WPARAM wp, LPARAM lp)
        #     {
        #         if (code == HC_ACTION && wp == VK_RETURN && !(lp & 0x80000000L))
        #             keyed++;
        #         return CallNextHookEx(key_hook, code, wp, lp);
        #     }
        #
        #     DWORD FAR PASCAL GetHook(int code, WPARAM wp, LPARAM lp)
        #     {
        #         MSG FAR *m = (MSG FAR *)lp;
        #         if (m->message == WM_USER + 5)
        #             m->wParam = 2;
        #         return CallNextHookEx(get_hook, code, wp, lp);
        #     }
        #
        #     DWORD FAR PASCAL Cbt2Hook(int code, WPARAM wp, LPARAM lp)
        #     {
        #         if (code == HCBT_ACTIVATE)
        #             activated++;
        #         else if (code == HCBT_SETFOCUS)
        #             focused++;
        #         else if (code == HCBT_DESTROYWND) {
        #             destroyed++;
        #             if (refuse) {
        #                 refuse = 0;
        #                 return 1;
        #             }
        #         }
        #         return CallNextHookEx(cbt2_hook, code, wp, lp);
        #     }
        #
        #     DWORD FAR PASCAL ShellHook(int code, WPARAM wp, LPARAM lp)
        #     {
        #         if (code == HSHELL_WINDOWCREATED)
        #             shelled += 1;
        #         else if (code == HSHELL_WINDOWDESTROYED)
        #             shelled += 10;
        #         return CallNextHookEx(shell_hook, code, wp, lp);
        #     }
        #
        #     static void hooks2(HINSTANCE hInstance, HWND owner)
        #     {
        #         char s[96];
        #         HWND h;
        #         MSG m;
        #         int got, kept, gone, filter;
        #         get_hook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)MakeProcInstance((FARPROC)GetHook,
        #                                     hInstance), hInstance, GetCurrentTask());
        #         PostMessage(owner, WM_USER + 5, 1, 0);
        #         got = PeekMessage(&m, owner, WM_USER + 5, WM_USER + 5, PM_REMOVE) ? (int)m.wParam : 0;
        #         UnhookWindowsHookEx(get_hook);
        #         cbt2_hook = SetWindowsHookEx(WH_CBT, (HOOKPROC)MakeProcInstance((FARPROC)Cbt2Hook, hInstance),
        #                                      hInstance, GetCurrentTask());
        #         shell_hook = SetWindowsHookEx(WH_SHELL, (HOOKPROC)MakeProcInstance((FARPROC)ShellHook,
        #                                       hInstance), hInstance, NULL);
        #         h = CreateWindow("OwHooked", "cbt", WS_POPUP | WS_VISIBLE, 0, 0, 50, 20, NULL, NULL,
        #                          hInstance, NULL);
        #         SetFocus(h);
        #         refuse = 1;
        #         kept = !DestroyWindow(h) && IsWindow(h);
        #         gone = DestroyWindow(h) && !IsWindow(h);
        #         UnhookWindowsHookEx(shell_hook);
        #         UnhookWindowsHookEx(cbt2_hook);
        #         filter_hook = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)MakeProcInstance((FARPROC)FilterHook,
        #                                        hInstance), hInstance, GetCurrentTask());
        #         m.message = WM_USER + 9;
        #         filter = CallMsgFilter(&m, 77);
        #         UnhookWindowsHookEx(filter_hook);
        #         wsprintf(s, "hooks2: filter %d %d, keys %d, get %d, cbt %d %d %d %d %d, shell %d",
        #                  filtered > 0, filter, keyed >= 2, got, activated > 0, focused > 0, destroyed,
        #                  kept, gone, shelled);
        #         say(s);
        #     }
        #
        #     long FAR PASCAL HookedProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_SIZE)
        #             sizes++;
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     static void hooks(HINSTANCE hInstance)
        #     {
        #         char s[96];
        #         HWND h;
        #         WNDCLASS wc;
        #         int created, ok;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = HookedProc;
        #         wc.hInstance = hInstance;
        #         wc.lpszClassName = "OwHooked";
        #         RegisterClass(&wc);
        #         cbt_hook = SetWindowsHookEx(WH_CBT, (HOOKPROC)MakeProcInstance((FARPROC)CbtHook, hInstance),
        #                                     hInstance, GetCurrentTask());
        #         cwp_hook = SetWindowsHookEx(WH_CALLWNDPROC,
        #                                     (HOOKPROC)MakeProcInstance((FARPROC)CwpHook, hInstance),
        #                                     hInstance, GetCurrentTask());
        #         h = CreateWindow("OwHooked", "hooked", WS_POPUP, 0, 0, 50, 20, NULL, NULL, hInstance, NULL);
        #         UnhookWindowsHookEx(cwp_hook);
        #         UnhookWindowsHookEx(cbt_hook);
        #         ok = made == h && h != 0;
        #         DestroyWindow(h);
        #         sizes = 0;
        #         h = CreateWindow("OwHooked", "sized", WS_OVERLAPPED, 0, 0, 50, 40, NULL, NULL, hInstance, NULL);
        #         created = sizes;
        #         ShowWindow(h, SW_HIDE);
        #         wsprintf(s, "hooks: made %d %d, sent %d, subclassed %d, got %d; size %d %d", ok,
        #                  made_cx, sent_cx, subclassed, sub_got, created, sizes - created);
        #         DestroyWindow(h);
        #         say(s);
        #     }
        #
        #     /* the dialog of its own class: a command sent, WM_CLOSE (IDCANCEL posted) */
        #     static void own_dialog(HINSTANCE hInstance, HWND owner)
        #     {
        #         WNDCLASS wc;
        #         HWND d;
        #         MSG m;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = OwnClassProc;
        #         wc.cbWndExtra = DLGWINDOWEXTRA;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        #         wc.lpszClassName = "OwnDlg";
        #         RegisterClass(&wc);
        #         d = CreateDialog(hInstance, "OWNDLG", owner,
        #                          (DLGPROC)MakeProcInstance((FARPROC)OwnProc, hInstance));
        #         SendMessage(d, WM_COMMAND, IDCANCEL, 0);
        #         SendMessage(d, WM_CLOSE, 0, 0);
        #         while (PeekMessage(&m, d, 0, 0, PM_REMOVE))
        #             DispatchMessage(&m);
        #         DestroyWindow(d);
        #     }
        #
        #     static void kernel_part(void)
        #     {
        #         char s[96], a[16], b[16];
        #         HGLOBAL g = GlobalAlloc(GMEM_FIXED, 64);
        #         char FAR *p = (char FAR *)GlobalLock(g);
        #         UINT alias = AllocSelector(0);
        #         DWORD dos;
        #         _fmemset(p, 0, 64);
        #         lstrcpy(p, "through an alias");
        #         SetSelectorBase(alias, GetSelectorBase((UINT)g));
        #         SetSelectorLimit(alias, 31);
        #         lstrcpyn(a, "truncated text", 6);
        #         hmemcpy(b, "huge copy", 10);
        #         wsprintf(s, "%s|%s|%s|limit %ld", (LPSTR)a, (LPSTR)b, (LPSTR)MAKELP(alias, 0), GetSelectorLimit(alias));
        #         say(s);
        #         wsprintf(s, "bad: null %d, ok %d, code-write %d, str %d, free %d",
        #                  IsBadReadPtr(NULL, 1), IsBadReadPtr(s, sizeof(s)),
        #                  IsBadWritePtr((void FAR *)kernel_part, 1), IsBadStringPtr(a, 100),
        #                  FreeSelector(alias));
        #         say(s);
        #         dos = GlobalDOSAlloc(100);
        #         wsprintf(s, "dos %d, muldiv %d %d %d, ver %x, chars %d%d%d",
        #                  HIWORD(dos) != 0 && LOWORD(dos) != 0, MulDiv(10, 20, 3), MulDiv(-7, 3, 2),
        #                  MulDiv(1, 2, 0), GetExpWinVer(inst), IsCharAlpha('q'), IsCharUpper('q'),
        #                  IsCharAlphaNumeric('7'));
        #         if (dos)
        #             GlobalDOSFree(LOWORD(dos));
        #         say(s);
        #         {
        #             ATOM at = GlobalAddAtom("WinnAtom");
        #             char n[32];
        #             lstrcpy(a, "MiXeD");
        #             AnsiUpperBuff(a, 3);
        #             GlobalGetAtomName(at, n, sizeof(n));
        #             wsprintf(s, "atom %s %d, buff %s, heap %d", (LPSTR)n, GlobalFindAtom("winnatom") == at,
        #                      (LPSTR)a, LocalHeapSize() > 0);
        #             GlobalDeleteAtom(at);
        #         }
        #         say(s);
        #         GlobalUnlock(g);
        #         GlobalFree(g);
        #     }
        #
        #     /* a dialog template in memory: a caption, a static text and OK */
        #     static HGLOBAL make_template(void)
        #     {
        #         HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 128);
        #         BYTE FAR *t = (BYTE FAR *)GlobalLock(g), FAR *q = t;
        #         *(DWORD FAR *)q = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME; q += 4;
        #         *q++ = 2;
        #         *(WORD FAR *)q = 20; q += 2; *(WORD FAR *)q = 20; q += 2;
        #         *(WORD FAR *)q = 150; q += 2; *(WORD FAR *)q = 50; q += 2;
        #         *q++ = 0; *q++ = 0;
        #         lstrcpy((LPSTR)q, "In memory"); q += 10;
        #         *(WORD FAR *)q = 8; q += 2; *(WORD FAR *)q = 8; q += 2;
        #         *(WORD FAR *)q = 130; q += 2; *(WORD FAR *)q = 10; q += 2;
        #         *(WORD FAR *)q = 100; q += 2;
        #         *(DWORD FAR *)q = WS_CHILD | WS_VISIBLE | SS_LEFT; q += 4;
        #         *q++ = 0x82; *q++ = 0; *q++ = 0;
        #         *(WORD FAR *)q = 55; q += 2; *(WORD FAR *)q = 30; q += 2;
        #         *(WORD FAR *)q = 40; q += 2; *(WORD FAR *)q = 14; q += 2;
        #         *(WORD FAR *)q = IDOK; q += 2;
        #         *(DWORD FAR *)q = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON; q += 4;
        #         *q++ = 0x80; lstrcpy((LPSTR)q, "OK"); q += 3; *q++ = 0;
        #         GlobalUnlock(g);
        #         return g;
        #     }
        #
        #     LRESULT FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         switch (msg) {
        #         case WM_PAINT: {
        #             PAINTSTRUCT ps;
        #             HDC dc = BeginPaint(h, &ps);
        #             int i;
        #             for (i = 0; i < nlines; i++)
        #                 TextOut(dc, 4, 4 + i * 18, lines[i], lstrlen(lines[i]));
        #             GrayString(dc, 0, NULL, (LPARAM)(LPSTR)"grayed by GrayString", 0, 4,
        #                        4 + nlines * 18, 0, 0);
        #             EndPaint(h, &ps);
        #             return 0;
        #         }
        #         case WM_DESTROY:
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h, child;
        #         MSG m;
        #         char s[96];
        #         FARPROC dp;
        #         POINT pt;
        #         BYTE keys[256];
        #         HGLOBAL tpl;
        #         int r1, r2;
        #         UINT queue;
        #         DWORD hook;
        #         HDWP d;
        #         inst = hInstance;
        #         kernel_part();
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.cbClsExtra = 4;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwApi";
        #         RegisterClass(&wc);
        #         h = CreateWindowEx(0, "OwApi", "KERNEL and USER", WS_OVERLAPPEDWINDOW, 10, 20, 600, 220,
        #                            NULL, NULL, hInstance, NULL);
        #         child = CreateWindow("BUTTON", "child", WS_CHILD | WS_VISIBLE, 0, 0, 60, 20, h, (HMENU)5,
        #                              hInstance, NULL);
        #         d = BeginDeferWindowPos(1);
        #         d = DeferWindowPos(d, child, NULL, 520, 160, 60, 20, SWP_NOZORDER);
        #         EndDeferWindowPos(d);
        #         SetClassWord(h, 2, 0x1234);
        #         pt.x = 10; pt.y = 10;
        #         MapWindowPoints(child, h, &pt, 1);
        #         GetKeyboardState(keys);
        #         hook = (DWORD)SetWindowsHookEx(WH_MSGFILTER,
        #                                        (HOOKPROC)MakeProcInstance((FARPROC)FilterHook, hInstance),
        #                                        hInstance, GetCurrentTask());
        #         filter_hook = (HHOOK)hook;
        #         key_hook = SetWindowsHookEx(WH_KEYBOARD, (HOOKPROC)MakeProcInstance((FARPROC)KeyHook,
        #                                     hInstance), hInstance, GetCurrentTask());
        #         wsprintf(s, "class %x, mapped %d,%d, keys %d, hook %d, menu %d, popup %d",
        #                  GetClassWord(h, 2), pt.x, pt.y, keys[VK_SHIFT] & 0x80, hook != 0,
        #                  IsMenu((HMENU)h), AnyPopup());
        #         say(s);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         dp = MakeProcInstance((FARPROC)DlgProc, hInstance);
        #         r1 = DialogBoxParam(hInstance, "PARAMDLG", h, (DLGPROC)dp, (LPARAM)(LPSTR)"from DialogBoxParam");
        #         tpl = make_template();
        #         r2 = DialogBoxIndirectParam(hInstance, tpl, h, (DLGPROC)dp, (LPARAM)(LPSTR)"indirect");
        #         GlobalFree(tpl);
        #         UnhookWindowsHookEx((HHOOK)hook);
        #         InvalidateRect(h, NULL, TRUE);
        #         queue = HIWORD(GetQueueStatus(QS_PAINT | QS_TIMER));
        #         own_dialog(hInstance, h);
        #         wsprintf(s, "dialogs %d %d, queue %x, own class %d %d, freed es %x", r1, r2, queue,
        #                  own_class, own_cmd, freed_es(hInstance));
        #         say(s);
        #         hooks(hInstance);
        #         UnhookWindowsHookEx(key_hook);
        #         hooks2(hInstance, h);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owapi.rc
        #     #include <windows.h>
        #     PARAMDLG DIALOG 20, 20, 150, 50
        #     STYLE WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME
        #     CAPTION "Param"
        #     FONT 8, "Helv"
        #     BEGIN
        #         LTEXT "", 100, 8, 8, 130, 10
        #         DEFPUSHBUTTON "OK", IDOK, 55, 30, 40, 14
        #     END
        #     OWNDLG DIALOG 20, 20, 100, 40
        #     STYLE WS_POPUP | WS_CAPTION | DS_MODALFRAME
        #     CLASS "OwnDlg"
        #     CAPTION "Own class"
        #     BEGIN
        #         PUSHBUTTON "Cancel", IDCANCEL, 30, 20, 40, 14
        #     END
        #     OWNDATA RCDATA
        #     BEGIN
        #         "resource data\0"
        #     END
        # ---- owapi.lnk
        #     system windows
        #     name owapi.exe
        #     file owapi.o
        #     import GLOBALDOSALLOC KERNEL.184
        #     import GLOBALDOSFREE KERNEL.185
        #     import GETEXPWINVER KERNEL.167
        #     import LOCALHEAPSIZE KERNEL.162
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            'c0000200000000000203020000040020e60f010000000200020003000d0040005000a000a900af0042010000'
            '0000010003000208a800300e00000a03a800e012400de012110d7003410c200701000580020000000000d80e'
            '20003010470000000000f80e1b0030104000000000000a80010000000000d10e070030003800000000000000'
            '074f574e44415441064f574e444c4708504152414d444c47054f57415049000000010008000d0000064b4552'
            '4e454c0455534552034744490000096f776170692e65786500000000ccebfd909090900050b81000e88e0f58'
            '53515289c3a110073d09007d1f89c2d1e0d1e029d0b105d3e04289161007ba700301c21e521e539affff0000'
            '5a595bc31e5890455589e51e8ed8b86200e84d0f5683ec488b460c8b760e3d100175538d56f41e5231c05050'
            'b8040050b80800509affff0000568d56f41e529affff0000ff76faff76f8ff7608ff76061eb81000508d56b4'
            '1e529affff000083c41056b86400508d56b41e529affff0000b80100eb193d11017512837e0a01750c56b807'
            '00509affff0000ebe431c08d66fc5e1f5d4dca0a001e5890455589e51e8ed8b80800e8b80e8b460c3d100175'
            '05b80100ebdd3d1101750f837e0a027509b8010001060a07ebc931c0ebc51e5890455589e51e8ed8b81200e8'
            '830e8b460c3d11017504ff060c07ff760e50ff760aff7608ff76069affff0000eb9550b81400e85c0e585351'
            '525650501eb819005031c050b80a00509affff0000509affff000089c685c0740e509affff000085d2750985'
            'c07505b8ffffeb12569affff000089f08ec0509affffffff8cc05ee9aefe1e5890455589e51e8ed8b81600e8'
            'ff0d8b460c3d81007504ff060807ff36e606ff36e406ff760e50ff760aff7608ff76069affff0000e908ff1e'
            '5890455589e51e8ed8b81600e8c60d568b76068b560a8b4e0c83f9037511891604078e460826c41c268b470c'
            'a30007ff36ee06ff36ec065152ff7608569affff00005ee98f011e5890455589e51e8ed8b81600e87f0d568b'
            '76068b56088ec2268b44083b060407754626817c068100753e833e020700753726c41c268b470ca306078ec2'
            '26ff7408b8fcff50b8ffff50b8ffff50ff360e079affff000052509affff0000a3e4068916e606ff060207ff'
            '36ea06ff36e806ff760cff760ae979ff1e5890455589e51e8ed8b81400e8050d8b5e068b56088b460c8ec23d'
            '4d007508b8010031d2e9ed0085c0750c26817f0200017504ff06f406ff36de06ff36dc0650ff760a5253e9c7'
            '001e5890455589e51e8ed8b81400e8bc0c8b5e088b560a8b460c85c0750e83fa0d7509f6c7807504ff06fc06'
            'ff36d206ff36d006505253e98b001e5890455589e51e8ed8b81400e8830c8b5e068b46088ec026817f020504'
            '750626c747040200ff36da06ff36d806ff760cff760a50eb8c1e5890455589e51e8ed8b81400e84c0c8b460c'
            '3d05007506ff06f206eb273d09007506ff06f606eb1c3d04007517ff06fa06833ef00600740c31c0a3f006b8'
            '010031d2eb17ff36e206ff36e00650ff760aff7608ff76069affff00001f5d4dca08001e5890455589e51e8e'
            'd8b81400e8ea0b8b460c3d010075060106f806eb0a3d020075058306f8060aff36d606ff36d406ebb95589e5'
            '50b8a400e8be0b585351565783ec7a89c689d7b8030050b8ffff50b8ffff50569affff00005250569affff00'
            '00509affff0000a3d8068916da0657b8050450b801005031c050509affff00008d56de1e5257b805045050b8'
            '0100509affff000085c074058b7ee2eb0231ff897ef2ff36da06ff36d8069affff0000b8050050b8ffff50b8'
            'ffff50569affff00005250569affff0000509affff0000a3e0068916e206b80a0050b8ffff50b8ffff50569a'
            'ffff000052505631c0509affff0000a3d4068916d6061eb82500501eb8210050b800905031c0505050b83200'
            '50b814005031c050505650509affff000089c78946f6509affff0000c706f0060100579affff000085c0750f'
            '579affff000085c07405bf0100eb0231ff897ef0ff76f69affff000085c07411ff76f69affff000085c07505'
            'bf0100eb0231ffff36d606ff36d4069affff0000ff36e206ff36e0069affff0000b8ffff50b8ffff50b8ffff'
            '50569affff00005250569affff0000509affff0000a3dc068916de06c746e009048d56de1e52b84d00509aff'
            'ff00008946f4ff36de06ff36dc069affff0000ff36f80657ff76f0ff36fa06833ef606007e05be0100eb0231'
            'f656833ef206007e05be0100eb0231f656ff76f2833efc06027c05be0100eb0231f656ff76f4833ef406007e'
            '05be0100eb0231f6561eb82e00508d967eff1e529affff000083c41c8d867effe829fa8d66f85f5ee99b011e'
            '5890455589e51e8ed8b81200e8a6098b460c3d05007504ff06fe06ff760e50ff760aff7608ff76069affff00'
            '00e9b7fa5589e550b8aa00e87b0958535152565783ec7e89c6b91a0031db8cda8d46d8e89509c746daffffc7'
            '46dcffff8976e2c746ee25008c5ef08d56d81e529affff0000b8050050b8ffff50b8ffff50569affff000052'
            '50569affff0000509affff0000a3ec068916ee06b8040050b8ffff50b8ffff50569affff00005250569affff'
            '0000509affff0000a3e8068916ea061eb82500501eb8720050b800805031c0505050b8320050b814005031c0'
            '50505650509affff000089c78946f4ff36ea06ff36e8069affff0000ff36ee06ff36ec069affff0000a10407'
            '39c7750985c07405bf0100eb0231ffff76f49affff000031c0a3fe061eb82500501eb879005031c050505050'
            'b8320050b828005031c050505650509affff000089c6a1fe068946f25631c0509affff0000a1fe062b46f250'
            'ff76f2ff360807ff360207ff360607ff360007571eb87f00508d9678ff1e529affff000083c416569affff00'
            '008d8678ffe88cf88d66f65f5e5a595b5dc35589e550b84400e80d08585351565783ec2c89c689d7b91a0031'
            'db8cda8d46cce82608c746ceffffc746d0ffffc746d41e008976d631c05050b8007f509affff00008946dac7'
            '46dc0600c746e2bd008c5ee48d56cc1e529affff0000561eb8c4005057b8ffff50b8ffff50569affff000052'
            '509affff000089c789c650b8110150b802005031c050509affff000057b810005031c05050509affff00008d'
            '56e61e525631c05050b80100509affff000085c0740c8d56e61e529affff0000ebdd569affff0000e990fd55'
            '89e5b8d000e84507535152565781ecaa0031c05050b84000509affff00008946ee8946f2509affff000089c7'
            '8956ec31c0509affff000089c6b9400031db89f88b56ece83907ff76ec571eb8cb00509affff000056ff76ee'
            '9affff000052509affff00005631c050b81f00509affff00008d56dc1e521eb8dc0050b80600509affff0000'
            '8d56cc1e521eb8eb005031c050b80a00509affff0000569affff000052505631c0508d46cc1e508d56dc1e52'
            '1eb8f500508d964cff1e529affff000083c4188d864cffe8eef6569affff0000508d56dc1e52b86400509aff'
            'ff000050b8ffff50b84f0850b80100509affff0000508d964cff1e52b86000509affff00005031c05050b801'
            '00509affff0000501eb80801508d964cff1e529affff000083c4128d864cffe88ef631c050b86400509affff'
            '000089c789d68946f08956f4b83700509affff000050b87100509affff000050b87100509affff000050ff36'
            '0e079affff000050b8010050b802005031c0509affff000050b8f9ff50b8030050b80200509affff000050b8'
            '0a0050b8140050b80300509affff00005085f6740985ff7405be0100eb0231f6561eb83c01508d964cff1e52'
            '9affff000083c4188b46f40b46f07408ff76f09affff00008d864cffe8e1f51eb86a01509affff000089c689'
            'c78d56dc1e521eb87301509affff00008d56dc1e52b80300509affff0000568d56ac1e52b82000509affff00'
            '009affff000085c07605be0100eb0231f6568d56dc1e521eb87901509affff000039f87505be0100eb0231f6'
            '568d56ac1e521eb88201508d964cff1e529affff000083c414579affff00008d864cffe856f5ff76f29affff'
            '0000ff76f29affff0000e9b7fc5589e5b81a00e8cf04535152565783ec04b842005031c050b88000509affff'
            '000089c7509affff000089c68956f28ec226c704800026c74402c8808956f483c60426c604024626c7041400'
            '83c60226c704140083c60226c704960083c60226c704320083c60226c604004626c604004652561eb89f0150'
            '9affff000083c60a8e46f426c704080083c60226c704080083c60226c704820083c60226c7040a0083c60226'
            'c704640083c60226c704000026c74402005083c60426c604824626c604004626c604004626c704370083c602'
            '26c7041e0083c60226c704280083c60226c7040e0083c60226c704010083c60226c704010026c74402015083'
            'c60426c6048046ff76f2561eb8a901509affff000083c6038e46f426c60400579affff000089f8e992fb1e58'
            '90455589e51e8ed8b84200e8a303565783ec208b460c3d0f0074083d02007450e99900ff760e8d56da1e529a'
            'ffff000089c731f63b3610077d3857b8040050b10389f0d3e001f0d1e00504005089f0d1e0d1e029f0b105d3'
            'e0ba700301c21e521e529affff0000509affff000046ebc4eb405731c05050501eb8ac015031c050b8040050'
            '8b161007b10389d0d3e001d0d1e00504005031c050509affff0000ff760e8d56da1e529affff000031c031d2'
            'eb1c31c0509affff0000ebf0ff760e50ff760aff7608ff76069affff00008d66fa5fe90df45589e5b8c801e8'
            'd302565781ec9e018b760c89360e07e875fbb91a0031db8cda8d46bee8e802c746c0ffffc746c2ffffc746c4'
            '04008976c831c05050b8007f509affff00008946cc31c0509affff00008946cec746d4c1018c5ed68d56be1e'
            '529affff000031c050501eb8c101501eb8c70150b8cf005031c050b80a0050b8140050b8580250b8dc005031'
            'c050505650509affff000089c71eb8dd01501eb8d70150b800505031c0505050b83c0050b814005057b80500'
            '505631c050509affff00008946fab80100509affff000050ff76fa31c050b8080250b8a00050b83c0050b814'
            '0050b80400509affff0000509affff000057b8020050b83412509affff0000b80a008946ea8946ecff76fa57'
            '8d56ea1e52b80100509affff00008d965efe1e529affff0000b8ffff50b8ffff50b8ffff50569affff000052'
            '50569affff0000509affff00008946f08956ee8946f68956f8a3dc068916de06b8020050b8ffff50b8ffff50'
            '569affff00005250569affff0000509affff0000a3d0068916d2069affff000050579affff0000508b46ee0b'
            '46f07403b80100508a866efe248030e450ff76ecff76ea57b80200509affff0000501eb8e401508d965eff1e'
            '529affff000083c4168d865effe878f157ff76049affff0000579affff0000b8ffff50b8ffff50569affff00'
            '008946ee8956f4561eb83402505752ff76ee1eb82002509affff00008946f0e8f7fb8946f2565057ff76f4ff'
            '76ee1eb83d02509affff00008946f4ff76f29affff0000ff76f8ff76f69affff00005731c05050b80100509a'
            'ffff0000b83000509affff000089d389fa89f0e86cf889f0e81bf250ff360a07ff360c0753ff76f4ff76f01e'
            'b84602508d965eff1e529affff000083c4148d865effe8bff089f0e8caf6ff36d206ff36d0069affff000089'
            'fa89f0e873f48d56d81e5231c05050509affff000085c074168d56d81e529affff00008d56d81e529affff00'
            '00ebd78b46dc8d66fc5f5e5dc20a00008c168802c33bc473132bc4f7d83b060a0077068cd03b0688027401c3'
            '0e5ab8bf0fbb0100e9c801537461636b204f766572666c6f7721005789c788d88ec2578ae0d1e9f3ab13c9f3'
            'aa5f89f85fc3eb0203009affff00000bc0750e5033c0ba0f00e86a0258b44ccd218c068e02575606535253be'
            '12078936a9028c1eab02268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07'
            'b91207bf70032bcf32c0f3aab83011a3a502b8ffffa3a702b430cd21a29002882691029affff0000a2920288'
            '26930286e0a3940254583bc475120f01e0a90100740ab001a29602b003a297021ebfb10257b89000509affff'
            '0000b8b102893ead028c1eaf02b8ff00e86b01e81bfce9f9004f70656e20576174636f6d20432f432b2b3136'
            '2052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20'
            '436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f707972696768742028'
            '4329205379626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df'
            '8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026'
            'c607001e8d9e7aff531eb87c025031c0509affff000089f8e94400515689c189d6e8330185c0750689c889f2'
            'eb9889d8e92c0000c3c3535289c3833e6803007404ff166803ff164203baff00b81000e8a800ff164203ff16'
            '440389d8e928fe5289c2ff164203ff16440389d0e918fe5589e5535289c3833f0075078d66fc5a5b5dc31eff'
            '171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee813ff'
            'bf700389fabb6a0389fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d44'
            '02803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee8bcfe88'
            'd5bf700389fabb700389fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a'
            '6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803e460300750331c0'
            'c35250cceb0657564944454fb8010083c404c300bc0003013d0b010058000301d10b010058000301e10b0100'
            '13000301180c0200270003014f0c01005a000301550c030021000301870c0200b9000301940c020028000301'
            'a20c020006000301b60c02006b000500ea0c0100ea0b0200ef0c0100ea0b0301020d0200ad0003010d0d0300'
            '57000301220d020039000301530d0200c40103017f0d0200290003018b0d020003010301ab0d020004010301'
            'b10d020005010301bf0d020082000301da0d020002010301e50d0200de000200ee0d010088020500f20d0100'
            '88020301f70d010033000301ff0d010024000301050e020023010200210e0100d1020500250e0100d1020301'
            '2a0e010033000301320e010024000301380e020023010301440e0200340003014b0e020066010301710e0200'
            '81000301820e0200a4010301950e02002a0003019b0e02007c000200a00e010040000500a40e010040000301'
            'a90e010033000301c40e0200ef000301e00e0200f0000301eb0e010011000301f60e020024010301040f0200'
            '7d0003010d0f02004e0103013b0f0200a4010301570f0200240103016d0f02006c0003017b0f020071000301'
            '850f020072000301eb0f01005b0003012a1001001e00030131100200050002004b100100e60f03015c100100'
            '030003018e100100310003017e110200010003018e0601003300030196060100240003019c06020023010301'
            'ca06020029000301dc06020024010301e90602002401030103070200350003012c070200290003013d070200'
            '2a00030168070200a40103017107020035000500ac0701000a010200b10701000a010301c4070200ad000301'
            'de07020039000200ea070100d5000500ee070100d5000301f307010033000301fa0702005900030110080200'
            '6f0003011f0802006f000301320802006d000301400802007200030148080200350003016a0801000f000301'
            '760801001200030183080100af000301a008010058000301a9080100ba000301b0080100bb000301bd080100'
            'bd000301d008010061010301e60801005c010301ec080100bc0003010c090200a40103011c090100b0000301'
            '2b09010051010200310901004f0803013d0901004f0103014d0901004e0103015b0901004e0103016c090200'
            'a401030182090100b800030195090200b20103019f090200b3010301a9090200b1010301b3090100a7000301'
            'c409030080000301d609030080000301e809030080000301090a0200a40103011c0a0100b90003012d0a0200'
            '0c010301400a0100580003014e0a0200b50103015d0a02000f010301620a0100a20003017d0a02000e010301'
            '9e0a0200a4010301a70a02000d010301b60a010013000301be0a010011000301e20a01000f000301ea0a0100'
            '12000301380001005800030171000200480003017c0002006700030197000200a4010301a90002005c000301'
            'c3000200580003013401020034010301550101003c0003015b0101003d000301670101003e0003017a010100'
            '13000501840101003f000201860101003f000301c00102007a0003010602020025010200550201008e010500'
            '590201008e010301610201003300030168020200880003019903020025010200f00301000a030500f4030100'
            '0a030301f90301003300030101040100240003010704020023010301200402006e000301340402006d000301'
            '4f0402002401020058040100410305005c04010041030301610401003300030169040100240003016f040200'
            '230102007f040100a303050083040100a303030188040100330003019304020023010301c104020029000301'
            'cc04020016000301d804020035000301e20402002f000301f804020035000301040502002f0003011c050200'
            '240103012905020024010200320501008802050036050100880203013b050100330003014305010024000301'
            '4905020023010301630502007b0003017305020024010301d1050200a4010301110602006b0005003d060100'
            'e705020042060100e705030155060200390002005e060100c701050062060100c70103016706010033000301'
            '6f060100240003017506020023010200850601000e020500890601000e020000000000000000000000000000'
            '0000000000000500000000000000000000002573202564782564004f574e4441544100636274004f77486f6f'
            '6b656400686f6f6b73323a2066696c7465722025642025642c206b6579732025642c206765742025642c2063'
            '62742025642025642025642025642025642c207368656c6c20256400686f6f6b65640073697a656400686f6f'
            '6b733a206d6164652025642025642c2073656e742025642c20737562636c61737365642025642c20676f7420'
            '25643b2073697a65202564202564004f776e446c67004f574e444c47007468726f75676820616e20616c6961'
            '73007472756e63617465642074657874006875676520636f70790025737c25737c25737c6c696d697420256c'
            '64006261643a206e756c6c2025642c206f6b2025642c20636f64652d77726974652025642c20737472202564'
            '2c206672656520256400646f732025642c206d756c6469762025642025642025642c207665722025782c2063'
            '68617273202564256425640057696e6e41746f6d004d695865440077696e6e61746f6d0061746f6d20257320'
            '25642c20627566662025732c206865617020256400496e206d656d6f7279004f4b0067726179656420627920'
            '47726179537472696e67004f77417069004b45524e454c20616e642055534552006368696c6400425554544f'
            '4e00636c6173732025782c206d61707065642025642c25642c206b6579732025642c20686f6f6b2025642c20'
            '6d656e752025642c20706f7075702025640066726f6d204469616c6f67426f78506172616d00504152414d44'
            '4c4700696e646972656374006469616c6f67732025642025642c2071756575652025782c206f776e20636c61'
            '73732025642025642c206672656564206573202578004f70656e20576174636f6d000000ffff000000000000'
            '00000000000c0000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            'a111a1110000000000000000000000000000000000000000000000000000000000000000000000000020980f'
            '0000000000000000000000000000000000007265736f75726365206461746100c000c8800214001400960032'
            '000000506172616d00080048656c76000800080082000a0064000000025082000037001e0028000e00010001'
            '000150804f4b00008000c080011400140064002800004f776e446c67004f776e20636c617373001e00140028'
            '000e000200000001508043616e63656c0000'
        )),
    'OWGDI': (
        # wcl -bt=windows -l=windows -ms owgdi.c
        # #include <windows.h>
        # #include <string.h>
        #
        # /* GDI beyond the basics, in one window: arcs, pies and chords; FloodFill;
        #  * PolyPolygon; regions (elliptic, rounded, polygonal; framed, inverted);
        #  * the mapping modes (MM_ISOTROPIC, MM_LOENGLISH); DIBs (SetDIBitsToDevice,
        #  * StretchDIBits, CreateDIBitmap, GetDIBits, a DIB pattern brush); fonts
        #  * enumerated, character widths, LineDDA; what it found as text */
        # static char info[3][96];
        # static int fonts, dda;
        # static char names[64];
        #
        # int FAR PASCAL FontProc(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm, int type, LPARAM lp)
        # {
        #     if (fonts++ < 3) {
        #         lstrcat(names, lf->lfFaceName);
        #         lstrcat(names, ",");
        #     }
        #     return tm->tmHeight > 0;
        # }
        #
        # void FAR PASCAL DdaProc(int x, int y, LPARAM lp)
        # {
        #     dda++;
        #     SetPixel((HDC)LOWORD(lp), x, y, RGB(255, 0, 0));
        # }
        #
        # /* a 16x16 4-bit DIB: four colour quarters, a diagonal */
        # static struct { BITMAPINFOHEADER h; RGBQUAD c[16]; } dib;
        # static BYTE bits[16 * 8];
        #
        # static void make_dib(void)
        # {
        #     static const RGBQUAD pal[4] = { { 0, 0, 255, 0 }, { 0, 255, 0, 0 }, { 255, 0, 0, 0 },
        #                                     { 0, 255, 255, 0 } };
        #     int x, y;
        #     dib.h.biSize = sizeof(BITMAPINFOHEADER);
        #     dib.h.biWidth = 16;
        #     dib.h.biHeight = 16;
        #     dib.h.biPlanes = 1;
        #     dib.h.biBitCount = 4;
        #     for (x = 0; x < 4; x++)
        #         dib.c[x] = pal[x];
        #     dib.c[15].rgbRed = dib.c[15].rgbGreen = dib.c[15].rgbBlue = 0;
        #     for (y = 0; y < 16; y++)            /* bottom up */
        #         for (x = 0; x < 16; x++) {
        #             int c = x == y ? 15 : (y >= 8) * 2 + (x >= 8);
        #             bits[y * 8 + x / 2] |= (BYTE)(c << (x & 1 ? 0 : 4));
        #         }
        # }
        #
        # static void paint(HWND h, HDC dc)
        # {
        #     POINT pp[6] = { { 230, 20 }, { 290, 20 }, { 260, 70 }, { 230, 50 }, { 290, 50 }, { 260, 0 } };
        #     int counts[2] = { 3, 3 };
        #     POINT tri[3] = { { 340, 20 }, { 390, 70 }, { 330, 70 } };
        #     HRGN r1, r2, r3;
        #     HBRUSH red = CreateSolidBrush(RGB(255, 0, 0)), blue = CreateSolidBrush(RGB(0, 0, 255));
        #     HBRUSH green = CreateSolidBrush(RGB(0, 160, 0));
        #     HBRUSH pat;
        #     HGLOBAL hp;
        #     HBITMAP bm;
        #     HDC mem;
        #     BYTE back[16 * 8];
        #     struct { BITMAPINFOHEADER h; RGBQUAD c[16]; } got;
        #     POINT pt;
        #     int i;
        #
        #     /* arcs, pies, chords */
        #     SelectObject(dc, green);
        #     Arc(dc, 10, 10, 70, 70, 70, 40, 40, 10);
        #     Pie(dc, 80, 10, 140, 70, 140, 40, 110, 10);
        #     Chord(dc, 150, 10, 210, 70, 210, 40, 150, 40);
        #     /* PolyPolygon: two triangles, WINDING */
        #     SetPolyFillMode(dc, WINDING);
        #     SelectObject(dc, blue);
        #     PolyPolygon(dc, pp, counts, 2);
        #     SetPolyFillMode(dc, ALTERNATE);
        #     /* FloodFill inside a black outline */
        #     SelectObject(dc, GetStockObject(NULL_BRUSH));
        #     Polygon(dc, tri, 3);
        #     SelectObject(dc, red);
        #     FloodFill(dc, 360, 60, RGB(0, 0, 0));
        #     /* regions */
        #     r1 = CreateEllipticRgn(410, 10, 480, 70);
        #     r2 = CreateRoundRectRgn(490, 10, 560, 70, 20, 20);
        #     r3 = CreatePolygonRgn(tri, 3, ALTERNATE);
        #     OffsetRgn(r3, 230, 70);
        #     FillRgn(dc, r1, blue);
        #     FrameRgn(dc, r2, red, 3, 3);
        #     FillRgn(dc, r3, green);
        #     InvertRgn(dc, r1);
        #     DeleteObject(r1);
        #     DeleteObject(r2);
        #     DeleteObject(r3);
        #
        #     /* DIBs: as they are, four times as large, a bitmap of one, read back */
        #     SetDIBitsToDevice(dc, 10, 90, 16, 16, 0, 0, 0, 16, bits, (BITMAPINFO FAR *)&dib, DIB_RGB_COLORS);
        #     StretchDIBits(dc, 40, 90, 64, 64, 0, 0, 16, 16, bits, (BITMAPINFO FAR *)&dib, DIB_RGB_COLORS,
        #                   SRCCOPY);
        #     bm = CreateDIBitmap(dc, &dib.h, CBM_INIT, bits, (BITMAPINFO FAR *)&dib, DIB_RGB_COLORS);
        #     mem = CreateCompatibleDC(dc);
        #     SelectObject(mem, bm);
        #     BitBlt(dc, 120, 90, 16, 16, mem, 0, 0, SRCCOPY);
        #     DeleteDC(mem);
        #     got.h = dib.h;
        #     GetDIBits(dc, bm, 0, 16, back, (BITMAPINFO FAR *)&got, DIB_RGB_COLORS);
        #     DeleteObject(bm);
        #     /* a pattern brush of a packed DIB */
        #     hp = GlobalAlloc(GMEM_MOVEABLE, sizeof(dib) + sizeof(bits));
        #     {
        #         BYTE FAR *q = (BYTE FAR *)GlobalLock(hp);
        #         _fmemcpy(q, &dib, sizeof(dib));
        #         _fmemcpy(q + sizeof(dib), bits, sizeof(bits));
        #         GlobalUnlock(hp);
        #     }
        #     pat = CreateDIBPatternBrush(hp, DIB_RGB_COLORS);
        #     SelectObject(dc, pat);
        #     Rectangle(dc, 150, 90, 214, 154);
        #     SelectObject(dc, GetStockObject(WHITE_BRUSH));
        #     DeleteObject(pat);
        #     GlobalFree(hp);
        #     wsprintf(info[0], "dib back: %02x %02x %02x, top-left blue %d", back[0], back[4], back[15 * 8 + 7],
        #              GetPixel(dc, 40, 90) == RGB(0, 0, 255));
        #
        #     /* MM_ISOTROPIC: a circle of 100 units in a 120-pixel box, y up */
        #     SaveDC(dc);
        #     SetMapMode(dc, MM_ISOTROPIC);
        #     SetWindowExt(dc, 220, 220);
        #     SetViewportExt(dc, 120, -60);
        #     SetViewportOrg(dc, 300, 160);
        #     Ellipse(dc, -100, -100, 100, 100);
        #     MoveTo(dc, 0, 0);
        #     LineTo(dc, 0, 90);                  /* up */
        #     MoveTo(dc, 0, 0);
        #     LineTo(dc, 90, 0);                  /* right */
        #     pt.x = 100; pt.y = 100;
        #     LPtoDP(dc, &pt, 1);
        #     RestoreDC(dc, -1);
        #     /* MM_LOENGLISH: a rectangle an inch wide, half an inch high */
        #     SaveDC(dc);
        #     SetMapMode(dc, MM_LOENGLISH);
        #     SetViewportOrg(dc, 380, 110);
        #     Rectangle(dc, 0, 0, 100, -50);
        #     TextOut(dc, 5, -5, "1 inch", 6);
        #     RestoreDC(dc, -1);
        #     wsprintf(info[1], "iso %d,%d, map %d, horz %dmm, aspect %d", pt.x, pt.y, GetMapMode(dc),
        #              GetDeviceCaps(dc, HORZSIZE), GetDeviceCaps(dc, ASPECTX));
        #
        #     /* fonts, widths, LineDDA */
        #     {
        #         int wd[2];
        #         FARPROC fp = MakeProcInstance((FARPROC)FontProc, GetWindowWord(h, GWW_HINSTANCE));
        #         FARPROC dp = MakeProcInstance((FARPROC)DdaProc, GetWindowWord(h, GWW_HINSTANCE));
        #         fonts = dda = 0;
        #         names[0] = 0;
        #         EnumFonts(dc, NULL, (OLDFONTENUMPROC)fp, 0);
        #         GetCharWidth(dc, 'W', 'X', wd);
        #         LineDDA(240, 140, 280, 150, (LINEDDAPROC)dp, (LPARAM)(UINT)dc);
        #         wsprintf(info[2], "fonts %d %s W %d X %d, dda %d", fonts, (LPSTR)names, wd[0], wd[1], dda);
        #     }
        #     for (i = 0; i < 3; i++)
        #         TextOut(dc, 10, 230 + i * 18, info[i], lstrlen(info[i]));
        #     DeleteObject(red);
        #     DeleteObject(blue);
        #     DeleteObject(green);
        # }
        #
        # LRESULT FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        # {
        #     if (msg == WM_PAINT) {
        #         PAINTSTRUCT ps;
        #         HDC dc = BeginPaint(h, &ps);
        #         paint(h, dc);
        #         EndPaint(h, &ps);
        #         return 0;
        #     }
        #     if (msg == WM_DESTROY) {
        #         PostQuitMessage(0);
        #         return 0;
        #     }
        #     return DefWindowProc(h, msg, wp, lp);
        # }
        #
        # int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        # {
        #     WNDCLASS wc;
        #     HWND h;
        #     MSG m;
        #     make_dib();
        #     _fmemset(&wc, 0, sizeof(wc));
        #     wc.lpfnWndProc = WndProc;
        #     wc.hInstance = hInstance;
        #     wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #     wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #     wc.lpszClassName = "OwGdi";
        #     RegisterClass(&wc);
        #     h = CreateWindow("OwGdi", "GDI", WS_OVERLAPPEDWINDOW, 10, 10, 600, 330, NULL, NULL,
        #                      hInstance, NULL);
        #     ShowWindow(h, show);
        #     UpdateWindow(h);
        #     while (GetMessage(&m, NULL, 0, 0)) {
        #         TranslateMessage(&m);
        #         DispatchMessage(&m);
        #     }
        #     return m.wParam;
        # }
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '700002000000000002030200000400206409010000000200020003000d0040005000500059005f00f2000000'
            '0000010000000200000000000000000380005e0c400d5e0c7008c001410c1004054f57474449000000010008'
            '000c0000064b45524e454c0347444904555345520000096f776764692e65786500000000ccebfd9090909000'
            '1e5890455589e51e8ed8b81000e8dd08a1080489c242891608043d03007d231eb8a802508b4610051200ff76'
            '12509affff00001eb8a802501eb81000509affff0000c45e0c26833f007e05b80100eb0231c01f5d4dca0e00'
            '1e5890455589e51e8ed8b81200e88508ff7606ff760cff760a31c050b8ff0050ff060a049affff00001f5d4d'
            'ca0800b81000e8600853515256575589e583ec02c706c001280031c0a3c201c706c4011000a3c601c706c801'
            '1000a3ca01c706cc010100c706ce01040031db89ded1e6d1e68b84a0008b8ca2008984e801898cea014383fb'
            '047ce430c0a22402a22502a2260231ffeb064783ff107d5a31dbeb4d83ff087c05b90100eb0231c989c8d1e0'
            '83fb087c05b90100eb0231c901c1894efeb10389fed3e689d8992bc2d1f801c68db42802f6c301740431c9eb'
            '03b904008b46fed3e008044383fb107da939fb75afb90f00ebc889ec5d5f5e5a595bc35589e550b85601e88c'
            '07585351565781ec26015052b90c008cd88ec08d7ebabeb000f3a5a1c8008946dea1ca008946e08d7ed2becc'
            '00a5a5a5a5a5a531c050b8ff00509affff00008946f68946f0b8ff005031c0509affff000089c68946f231c0'
            '50b800a0509affff00008946f48946eeffb6cefe509affff0000ffb6cefeb80a005050b84600505050b82800'
            '5050b80a00509affff0000ffb6cefeb8500050b80a0050b88c0050b8460050b88c0050b8280050b86e0050b8'
            '0a00509affff0000ffb6cefeb8960050b80a0050b8d20050b8460050b8d20050b8280050b8960050b8280050'
            '9affff0000ffb6cefeb80200509affff0000ffb6cefe569affff0000ffb6cefe8d56ba1e528d56de1e52b802'
            '00509affff0000ffb6cefeb80100509affff0000ffb6cefeb80500509affff0000509affff0000ffb6cefe8d'
            '56d21e52b80300509affff0000ffb6cefeff76f69affff0000ffb6cefeb8680150b83c005031c050509affff'
            '0000b89a0150b80a0050b8e00150b84600509affff00008946eab8ea0150b80a0050b8300250b8460050b814'
            '0050509affff00008946ec8d56d21e52b8030050b80100509affff000089c750b8e60050b84600509affff00'
            '00ffb6cefeff76ea569affff0000ffb6cefeff76ecff76f6b8030050509affff0000ffb6cefe57ff76f49aff'
            'ff0000ffb6cefeff76ea9affff0000ff76ea9affff0000ff76ec9affff0000579affff0000ffb6cefeb80a00'
            '50b85a0050b81000505031c0505050b81000501eb82802501eb8c0015031c0509affff0000ffb6cefeb82800'
            '50b85a0050b84000505031c05050b8100050501eb82802501eb8c0015031c050b8cc0050b82000509affff00'
            '00ffb6cefe1eb8c0015031c050b80400501eb82802501eb8c0015031c0509affff00008946ecffb6cefe9aff'
            'ff000089c650ff76ec9affff0000ffb6cefeb8780050b85a0050b8100050505631c05050b8cc0050b8200050'
            '9affff0000569affff0000b914008cd88ec08dbe52ffbec001f3a5ffb6cefeff76ec31c050b81000508d96d2'
            'fe1e528d9652ff1e5231c0509affff0000ff76ec9affff0000b802005031c050b8e800509affff000089c750'
            '9affff00008946ec89d6b8680050bbc0018cd98b46ece88e04b88000508b46ec056800bb28028cd989f2e87a'
            '04579affff00005731c0509affff000089c6ffb6cefe509affff0000ffb6cefeb8960050b85a0050b8d60050'
            'b89a00509affff0000ffb6cefe31c0509affff0000509affff0000569affff0000579affff0000ffb6cefeb8'
            '280050b85a00509affff000081faff00750985c07505b80100eb0231c0508a8651ff30e4508a86d6fe508a86'
            'd2fe501eb81200501eb8e802509affff000083c410ffb6cefe9affff0000ffb6cefeb80700509affff0000ff'
            'b6cefeb8dc0050509affff0000ffb6cefeb8780050b8c4ff509affff0000ffb6cefeb82c0150b8a000509aff'
            'ff0000ffb6cefeb89cff5050b8640050509affff0000ffb6cefe31c050509affff0000ffb6cefe31c050b85a'
            '00509affff0000ffb6cefe31c050509affff0000ffb6cefeb85a005031c0509affff0000b864008946e28946'
            'e4ffb6cefe8d56e21e52b80100509affff0000ffb6cefeb8ffff509affff0000ffb6cefe9affff0000ffb6ce'
            'feb80400509affff0000ffb6cefeb87c0150b86e00509affff0000ffb6cefe31c05050b8640050b8ceff509a'
            'ffff0000ffb6cefeb8050050b8fbff501eb83d0050b80600509affff0000ffb6cefeb8ffff509affff0000ff'
            'b6cefeb82800509affff000050ffb6cefeb80400509affff000050ffb6cefe9affff000050ff76e4ff76e21e'
            'b84400501eb84803509affff000083c412b8ffff50b8ffff50ffb6d0feb8faff509affff0000509affff0000'
            '89c78956ecb8ffff50b8ffff50ffb6d0feb8faff509affff0000509affff00008946ea89d631c0a30a04a308'
            '0430c0a2a802ffb6cefe30e45050ff76ec5750509affff0000ffb6cefeb8570050b85800508d56e61e529aff'
            'ff0000b8f00050b88c0050b8180150b896005056ff76ea31c050ffb6cefe9affff0000ff360a04ff76e8ff76'
            'e61eb8a80250ff3608041eb86c00501eb8a803509affff000083c41431f6ffb6cefeb80a0050b10389f0d3e0'
            '01f0d1e005e6005089f0d1e0d1e029f0b105d3e0bae80201c21e521e529affff0000509affff00004683fe03'
            '7cc4ff76f09affff0000ff76f29affff0000ff76ee9affff00008d66f85f5e595b5dc31e5890455589e51e8e'
            'd8b83400e82a015683ec208b460c8b760e3d0f007523568d56dc1e529affff000089c289f0e873f9568d56dc'
            '1e529affff000031c031d2eb1f3d0200750a31c0509affff0000ebeb5650ff760aff7608ff76069affff0000'
            '8d66fc5e1f5d4dca0a005589e5b85000e8c6005683ec2c8b760ce856f8b91a0031db8cda8d46d2e80601c746'
            'd4ffffc746d6ffff8976dc31c05050b8007f509affff00008946e031c0509affff00008946e2c746e88a008c'
            '5eea8d56d21e529affff00001eb88a00501eb8900050b8cf005031c050b80a005050b8580250b84a015031c0'
            '50505650509affff000089c650ff76049affff0000569affff00008d56ec1e5231c05050509affff000085c0'
            '74168d56ec1e529affff00008d56ec1e529affff0000ebd78b46f08d66fe5e5dc20a00008c16d800c33bc473'
            '132bc4f7d83b060a0077068cd03b06d8007401c30e5ab81709bb0100e9ee01537461636b204f766572666c6f'
            '7721005589e5565789c78b460489de8ec21e57918ed8d1e9f3a513c9f3a45f1f89f85f5e5dc202005789c788'
            'd88ec2578ae0d1e9f3ab13c9f3aa5f89f85fc300eb0203009affff00000bc0750e5033c0ba0f00e86a0258b4'
            '4ccd218c06de00575606535253be0c048936f9008c1efb00268a078804464384c075f589360a005b2bc0509a'
            'ffff000057579affff0000fc1e07b90c04bfc0012bcf32c0f3aab8ae0aa3f500b8ffffa3f700b430cd21a2e0'
            '008826e1009affff0000a2e2008826e30086e0a3e40054583bc475120f01e0a90100740ab001a2e600b003a2'
            'e7001ebf010157b89000509affff0000b80101893efd008c1eff00b8ff00e86b01e802fee9f9004f70656e20'
            '576174636f6d20432f432b2b31362052756e2d54696d652073797374656d2e20436f70797269676874202863'
            '29204f70656e20576174636f6d20436f6e7472696275746f727320323030322d323032362e20506f7274696f'
            '6e7320436f7079726967687420284329205379626173652c20496e632e20313938382d323030322e00c3cb00'
            '5589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048e'
            'c14626880743ebe031c08ec15026c607001e8d9e7aff531eb894005031c0509affff000089f8e94400515689'
            'c189d6e8330185c0750689c889f2eb9889d8e92c0000c3c3535289c3833eb801007404ff16b801ff169201ba'
            'ff00b81000e8a800ff169201ff16940189d8e928fe5289c2ff169201ff16940189d0e918fe5589e5535289c3'
            '833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd9'
            '5589e5535152565789c11ee813ffbfc00189fabbba0189fe88c839fb7316803f02750583c306ebf23a470172'
            'f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc3'
            '5589e55351565789c11ee8bcfe88d5bfc00189fabbc00189fe88c839fb7316803f02750583c306ebf23a4701'
            '77f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66'
            'f85f5eeb9f803e960100750331c0c35250cceb0657564944454fb8010083c404c30070000301120602000300'
            '0301230602000d000301380602001b00030152060200210003015f060200270003016c060200500003017a06'
            '02005000030184060200510003019a060300a4010200a206010008000500a606010008000301b20603008500'
            '0301b806010033000200c206010060000500c606010060000301d206030085000301d806010033000301fd06'
            '020046000301130702005e010301330702006400030155070300a40103018a0701005a000301900702002100'
            '03019e07020045000301a607020045000301ae07020045000301e107030027000301f3070300280003010608'
            '030006000301180803006b00050049080100bb0702004e080100bb0703015c080300ad000301670802005700'
            '03017c08030039000301a608030029000301b10803002a000301b70803007c000301c60803006c000301d408'
            '030071000301de08030072000301690901005b000301a80901001e000301af09030005000200c90901006409'
            '0301da090100030003010c0a010031000301fc0a030001000301370001005900030146000100590003018500'
            '02001f000301a301020042000301b501020042000301c601020042000301d60102002d000301f30102001700'
            '03011c0202001a000301450202005c01030152020200060003015c0202002d00030173020200c20103018002'
            '0200060003018d02020057000301930202002d000301a502020024000301b10202002d000301c60202001900'
            '0301db02020036000301f8020200bc0103010d0302003f0003011d030200650003012a030200280003013e03'
            '0200290003014b03020028000301570302002a0003015f0302004500030167030200450003016d0302004500'
            '030199030200bb010301cd030200b7010301ef030200ba010301fb03020034000301060402002d0003012904'
            '0200220003012f0402004400030161040200b90103016904020045000301790401000f000301810401001200'
            '0301af04010013000301b8040200bd010301c40402002d000301dd0402001b000301e904020057000301ef04'
            '02002d000301f504020045000301fb040100110003010c050200530003013e050300a40103014a0502001e00'
            '03015705020003000301650502000c000301760502000e000301870502000d0003019a05020018000301a705'
            '020014000301b705020013000301c405020014000301d405020013000301ef05020063000301fc0502002700'
            '0301050602001e00000000000500000000000000000000002c00646962206261636b3a202530327820253032'
            '7820253032782c20746f702d6c65667420626c7565202564003120696e63680069736f2025642c25642c206d'
            '61702025642c20686f727a2025646d6d2c2061737065637420256400666f6e74732025642025732057202564'
            '20582025642c20646461202564004f7747646900474449004f70656e20576174636f6d000000ff0000ff0000'
            'ff00000000ffff00e60014002201140004014600e60032002201320004010000030003005401140086014600'
            '4a0146000000ffff00000000000000000000000c000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000001f0b1f0b0000000000000000000000000000000000000000000000000000'
            '000000000000000000000020f0080000'
        )),
    'OWAPI2': (
        # wcc -bt=windows -ms owapi2.c; wlink @owapi2.lnk; wrc -bt=windows owapi2.rc owapi2.exe
        # ---- owapi2.c
        #     #include <windows.h>
        #     #include <string.h>
        #
        #     /* KERNEL and USER, more: the clipboard (its text and a format of its own
        #      * through WINN's), DlgDirList and DlgDirSelect, window placement, EnumProps,
        #      * LoadMenuIndirect, ChangeMenu, ScrollWindowEx, CreateCursor, CreateIcon,
        #      * CopyIcon, lstrcatn, AllocSelectorArray, SelectorAccessRights,
        #      * GetCodeHandle; on SPACE, OpenComm (no ports: its error in a message box),
        #      * then a call of a function WINN has not (UserSeeUserDo), which ends
        #      * it with its name */
        #     static char lines[6][96];
        #     static int nlines, props;
        #     static char dirsel[64], dirspec[64];
        #
        #     /* not in Open Watcom's windows.h: KERNEL's own (imported in owapi2.lnk) */
        #     UINT FAR PASCAL AllocSelectorArray(UINT n);
        #     UINT FAR PASCAL SelectorAccessRights(UINT sel, UINT op, UINT rights);
        #     LPSTR FAR PASCAL lstrcatn(LPSTR d, LPCSTR s, int n);
        #     /* USER's debugging one, which WINN has not */
        #     DWORD FAR PASCAL UserSeeUserDo(UINT op, UINT a, UINT b, UINT c);
        #
        #     static void say(const char *s) { if (nlines < 6) lstrcpy(lines[nlines++], s); }
        #
        #     BOOL FAR PASCAL PropProc(HWND h, LPCSTR name, HANDLE data)
        #     {
        #         if (HIWORD((DWORD)name) && !lstrcmp(name, "OwProp") && data == 42)
        #             props += 10;
        #         else
        #             props++;
        #         return TRUE;
        #     }
        #
        #     BOOL FAR PASCAL DirProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_INITDIALOG) {
        #             lstrcpy(dirspec, "C:\\HOSTWIN\\*.DLL");
        #             DlgDirList(h, dirspec, 100, 101, DDL_DIRECTORY);
        #             SendDlgItemMessage(h, 100, LB_SETCURSEL, 0, 0);
        #             return TRUE;
        #         }
        #         if (msg == WM_COMMAND && wp == IDOK) {
        #             DlgDirSelect(h, dirsel, 100);
        #             EndDialog(h, (int)SendDlgItemMessage(h, 100, LB_GETCOUNT, 0, 0));
        #             return TRUE;
        #         }
        #         return FALSE;
        #     }
        #
        #     LRESULT FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         if (msg == WM_PAINT) {
        #             PAINTSTRUCT ps;
        #             HDC dc = BeginPaint(h, &ps);
        #             int i;
        #             for (i = 0; i < nlines; i++)
        #                 TextOut(dc, 4, 4 + i * 18, lines[i], lstrlen(lines[i]));
        #             EndPaint(h, &ps);
        #             return 0;
        #         }
        #         if (msg == WM_DESTROY) {
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         char s[96], got[64], own[32];
        #         HGLOBAL g;
        #         UINT fmt, sel, rights, n;
        #         WINDOWPLACEMENT wp;
        #         HMENU menu;
        #         BYTE tpl[64], and_m[128], xor_m[128];
        #         BYTE FAR *t = tpl;
        #         HCURSOR cur;
        #         HICON ic, ic2;
        #         RECT upd;
        #         int count;
        #
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwApi2";
        #         RegisterClass(&wc);
        #         h = CreateWindow("OwApi2", "KERNEL and USER, more", WS_OVERLAPPEDWINDOW, 20, 30, 560, 200,
        #                          NULL, NULL, hInstance, NULL);
        #
        #         /* the clipboard: text, and a format of its own, through WINN's */
        #         fmt = RegisterClipboardFormat("OwApi2 data");
        #         OpenClipboard(h);
        #         EmptyClipboard();
        #         g = GlobalAlloc(GMEM_MOVEABLE, 32);
        #         lstrcpy((LPSTR)GlobalLock(g), "clipboard text");
        #         GlobalUnlock(g);
        #         SetClipboardData(CF_TEXT, g);
        #         g = GlobalAlloc(GMEM_MOVEABLE, 16);
        #         lstrcpy((LPSTR)GlobalLock(g), "own");
        #         GlobalUnlock(g);
        #         SetClipboardData(fmt, g);
        #         CloseClipboard();
        #         OpenClipboard(h);
        #         g = GetClipboardData(CF_TEXT);
        #         lstrcpy(got, g ? (LPSTR)GlobalLock(g) : "none");
        #         if (g)
        #             GlobalUnlock(g);
        #         g = GetClipboardData(fmt);
        #         lstrcpy(own, g ? (LPSTR)GlobalLock(g) : "none");
        #         if (g)
        #             GlobalUnlock(g);
        #         count = CountClipboardFormats();
        #         CloseClipboard();
        #         wsprintf(s, "clip \"%s\" \"%s\" %d formats", (LPSTR)got, (LPSTR)own, count);
        #         say(s);
        #
        #         /* selectors, strings */
        #         sel = AllocSelectorArray(3);
        #         rights = SelectorAccessRights(sel, 0, 0);
        #         FreeSelector(sel);
        #         FreeSelector(sel + 8);
        #         FreeSelector(sel + 16);
        #         lstrcpy(got, "abc");
        #         lstrcatn(got, "defghij", 6);
        #         wsprintf(s, "array %d, rights %x, catn %s, code %d", sel != 0, rights, (LPSTR)got,
        #                  GetCodeHandle((FARPROC)WndProc) == HIWORD((DWORD)(FARPROC)WndProc));
        #         say(s);
        #
        #         /* a menu from a template in memory, changed the old way */
        #         *(WORD FAR *)t = 0; t += 2; *(WORD FAR *)t = 0; t += 2;          /* header */
        #         *(WORD FAR *)t = MF_POPUP | MF_END; t += 2; lstrcpy((LPSTR)t, "&File"); t += 6;
        #         *(WORD FAR *)t = 0; t += 2; *(WORD FAR *)t = 10; t += 2; lstrcpy((LPSTR)t, "&One"); t += 5;
        #         *(WORD FAR *)t = MF_END; t += 2; *(WORD FAR *)t = 11; t += 2; lstrcpy((LPSTR)t, "&Two"); t += 5;
        #         menu = LoadMenuIndirect(tpl);
        #         ChangeMenu(GetSubMenu(menu, 0), 0, "&Three", 12, MF_APPEND);
        #         ChangeMenu(GetSubMenu(menu, 0), 10, NULL, 0, MF_DELETE | MF_BYCOMMAND);
        #         SetMenu(h, menu);
        #
        #         /* placement, properties */
        #         ShowWindow(h, show);
        #         wp.length = sizeof(wp);
        #         GetWindowPlacement(h, &wp);
        #         wp.rcNormalPosition.left += 10;
        #         wp.rcNormalPosition.right += 10;
        #         SetWindowPlacement(h, &wp);
        #         GetWindowPlacement(h, &wp);
        #         SetProp(h, "OwProp", 42);
        #         SetProp(h, MAKEINTATOM(7), 1);
        #         EnumProps(h, (PROPENUMPROC)MakeProcInstance((FARPROC)PropProc, hInstance));
        #         RemoveProp(h, "OwProp");
        #         RemoveProp(h, MAKEINTATOM(7));
        #         wsprintf(s, "place %d,%d show %d, props %d, menu %d %d", wp.rcNormalPosition.left,
        #                  wp.rcNormalPosition.top, wp.showCmd, props,
        #                  GetMenuItemCount(GetSubMenu(menu, 0)), GetMenuItemID(GetSubMenu(menu, 0), 0));
        #         say(s);
        #
        #         /* cursors and icons of bits */
        #         _fmemset(and_m, 0xff, sizeof(and_m));
        #         _fmemset(xor_m, 0, sizeof(xor_m));
        #         cur = CreateCursor(hInstance, 1, 1, 32, 32, and_m, xor_m);
        #         ic = CreateIcon(hInstance, 32, 32, 1, 1, and_m, xor_m);
        #         ic2 = CopyIcon(hInstance, ic);
        #         ScrollWindowEx(h, 0, 4, NULL, NULL, NULL, &upd, SW_INVALIDATE);
        #         wsprintf(s, "cursor %d icon %d copy %d, scrolled %d", cur != 0, ic != 0, ic2 != ic && ic2,
        #                  upd.bottom - upd.top);
        #         say(s);
        #         DestroyCursor(cur);
        #         DestroyIcon(ic);
        #         DestroyIcon(ic2);
        #
        #         /* DlgDirList */
        #         n = DialogBox(hInstance, "DIRDLG", h, (DLGPROC)MakeProcInstance((FARPROC)DirProc, hInstance));
        #         wsprintf(s, "dir: %d entries, spec %s, first %s", n, (LPSTR)dirspec, (LPSTR)dirsel);
        #         say(s);
        #         InvalidateRect(h, NULL, TRUE);
        #         UpdateWindow(h);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             if (m.message == WM_KEYDOWN && m.wParam == VK_SPACE) {
        #                 char s[32];
        #                 wsprintf(s, "OpenComm %d", OpenComm("COM1", 1024, 128));
        #                 MessageBox(m.hwnd, s, "COM1", MB_OK);
        #                 UserSeeUserDo(0, 0, 0, 0);  /* WINN has not: the end */
        #             }
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owapi2.lnk
        #     system windows
        #     name owapi2.exe
        #     file owapi2.o
        #     import ALLOCSELECTORARRAY KERNEL.206
        #     import SELECTORACCESSRIGHTS KERNEL.196
        #     import LSTRCATN KERNEL.352
        #     import USERSEEUSERDO USER.216
        # ---- owapi2.rc
        #     #include <windows.h>
        #     DIRDLG DIALOG 20, 20, 160, 100
        #     STYLE WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME
        #     CAPTION "DlgDirList"
        #     FONT 8, "Helv"
        #     BEGIN
        #         LTEXT "", 101, 8, 4, 140, 10
        #         LISTBOX 100, 8, 16, 100, 70, LBS_SORT | WS_VSCROLL | WS_BORDER
        #         DEFPUSHBUTTON "OK", IDOK, 114, 16, 40, 14
        #     END
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '90000200000000000203020000040020e807010000000200020003000e00400050006f0079007f0012010000'
            '000001000100020890006c0800000a039000e20a400de20ace074c02410c100501000580010000000000fc08'
            '2b003010180000000000000006444952444c47064f5741504932000000010008000d0000064b45524e454c04'
            '555345520347444900000a6f77617069322e657865000000ccebfd909090900050b81000e890075853515289'
            'c3a10e053d06007d1f89c2d1e0d1e029d0b105d3e04289160e05bacc0201c21e521e539affff00005a595bc3'
            '1e5890455589e51e8ed8b81000e84f078b460a85c0741f50ff76081eb81000509affff000085c0750d837e06'
            '2a750783060c050aeb04ff060c05b801001f5d4dca08001e5890455589e51e8ed8b81600e80c07568b460c8b'
            '760e3d1001753e1eb88c02501eb81700509affff0000561eb88c0250b8640050b8650050b81000509affff00'
            '0056b8640050b807045031c05050509affff0000b80100eb383d11017531837e0a01752b561eb84c0250b864'
            '00509affff00005656b8640050b80c045031c05050509affff0000509affff0000ebc531c05e1f5d4dca0a00'
            '1e5890455589e51e8ed8b83800e87306565783ec208b460c3d0f007560ff760e8d56da1e529affff000089c7'
            '31f63b360e057d3657b8040050b10389f0d3e001f0d1e00504005089f0d1e0d1e029f0b105d3e0bacc0201c2'
            '1e521e529affff0000509affff000046ebc4ff760e8d56da1e529affff000031c031d2eb213d0200750a31c0'
            '509affff0000ebebff760e50ff760aff7608ff76069affff00008d66fa5fe954ff5589e5b8a602e8d5055657'
            '81ec80028db61cff8c5efab91a0031db8cda8d469ce8ed05c7469effffc746a0ffff8b460c8946a631c05050'
            'b8007f509affff00008946aa31c0509affff00008946acc746b228008c5eb48d569c1e529affff00001eb828'
            '00501eb82f0050b8cf005031c050b8140050b81e0050b8300250b8c8005031c05050ff760c50509affff0000'
            '89c71eb84500509affff00008946f48946ee579affff00009affff0000b802005031c050b82000509affff00'
            '008946e6509affff000052501eb85100509affff0000ff76e69affff0000b8010050ff76e69affff0000b802'
            '005031c050b81000509affff00008946f6509affff000052501eb86000509affff0000ff76f69affff0000ff'
            '76f4ff76f69affff00009affff0000579affff0000b80100509affff00008946f88d9edcfe1e5385c0740e50'
            '9affff000089d389c289d8eb05ba64008cd850529affff00008b46f885c07406509affff0000ff76ee9affff'
            '00008946f88d9e7cff1e5385c0740e509affff000089c389d089daeb05ba64008cd850529affff00008b46f8'
            '85c07406509affff00009affff00008946e69affff0000ff76e68d967cff1e528d96dcfe1e521eb86900508d'
            '967cfe1e529affff000083c4128d867cfee870fcb80300509affff00008946e68946ea5031c050509affff00'
            '008946ecff76e69affff00008b46e6050800509affff00008b46e6051000509affff00008d96dcfe1e521eb8'
            '8300509affff00008d96dcfe1e521eb8870050b80600509affff0000b8ffff50b8ffff509affff00003dffff'
            '7505b80100eb0231c0508d96dcfe1e52ff76ec8b46ea85c07403b80100501eb88f00508d967cfe1e529affff'
            '000083c4128d867cfee8c8fb8e46fa26c704000083c60226c704000083c60226c704900083c60206561eb8b5'
            '00509affff000083c6068e46fa26c704000083c60226c7040a0083c60206561eb8bb00509affff000083c605'
            '8e46fa26c704800083c60226c7040b0083c60206561eb8c000509affff00008d961cff1e529affff000089c6'
            '5031c0509affff00005031c0501eb8c50050b80c0050b80001509affff00005631c0509affff000050b80a00'
            '5031c0505050b80002509affff000057569affff000057ff76049affff0000c746b61600578d56b61e529aff'
            'ff0000578d56b61e528346c40a8346c80a9affff0000578d56b61e529affff0000571eb8100050b82a00509a'
            'ffff00005731c050b8070050b80100509affff000057b8ffff50b8ffff50ff760c9affff000052509affff00'
            '00571eb81000509affff00005731c050b80700509affff00005631c0509affff00005031c0509affff000050'
            '5631c0509affff0000509affff000050ff360c05ff76baff76c6ff76c41eb8cc00508d967cfe1e529affff00'
            '0083c4148d867cfee83dfab98000bbff008cda8d867cfde8f701b9800031db8cda8d86fcfde8e901ff760cb8'
            '01005050b8200050508d967cfd1e528d96fcfd1e529affff00008946f0ff760cb820005050b8010050508d96'
            '7cfd1e528d96fcfd1e529affff00008946e68946f2ff760c509affff000089c68946e85731c050b804005031'
            'c050505050508d56de1e52b80200509affff00008b46e42b46e0503b76e6740985f67405b80100eb0231c050'
            '8b46f285c07403b80100508b46f085c07403b80100501eb8f600508d967cfe1e529affff000083c4108d867c'
            'fee868f9ff76f09affff0000ff76f29affff0000ff76e89affff0000ff760c1eb81d015057b8ffff50b8ffff'
            '50ff760c9affff000052509affff00001eba4c02521eba8c0252501eb82401508d967cfe1e529affff000083'
            'c4128d867cfee80bf95731c05050b80100509affff0000579affff00008d76cc1e5631c05050509affff0000'
            '85c0746a817ece0001754d837ed02075471eb8470150b8000450b88000509affff0000501eb84c01508db65c'
            'ff1e569affff000083c40aff76cc8db65cff1e561eb847015031c0509affff000031c0505050509affff0000'
            '8d76cc1e569affff00008d76cc1e569affff0000eb838b46d08d66fc5f5e5dc20a008c166401c33bc473132b'
            'c4f7d83b060a0077068cd03b0664017401c30e5ab8c107bb0100e9c801537461636b204f766572666c6f7721'
            '005789c788d88ec2578ae0d1e9f3ab13c9f3aa5f89f85fc3eb0203009affff00000bc0750e5033c0ba0f00e8'
            '6a0258b44ccd218c066a01575606535253be1005893685018c1e8701268a078804464384c075f589360a005b'
            '2bc0509affff000057579affff0000fc1e07b91005bf4c022bcf32c0f3aab83209a38101b8ffffa38301b430'
            'cd21a26c0188266d019affff0000a26e0188266f0186e0a3700154583bc475120f01e0a90100740ab001a272'
            '01b003a273011ebf8d0157b89000509affff0000b88d01893e89018c1e8b01b8ff00e86b01e819f9e9f9004f'
            '70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374656d2e20436f707972696768'
            '7420286329204f70656e20576174636f6d20436f6e7472696275746f727320323030322d323032362e20506f'
            '7274696f6e7320436f7079726967687420284329205379626173652c20496e632e20313938382d323030322e'
            '00c3cb005589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c'
            '268a048ec14626880743ebe031c08ec15026c607001e8d9e7aff531eb858015031c0509affff000089f8e944'
            '00515689c189d6e8330185c0750689c889f2eb9889d8e92c0000c3c3535289c3833e4402007404ff164402ff'
            '161e02baff00b81000e8a800ff161e02ff16200289d8e928fe5289c2ff161e02ff16200289d0e918fe5589e5'
            '535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df1eff'
            '1f1febd95589e5535152565789c11ee813ffbf4c0289fabb460289fe88c839fb7316803f02750583c306ebf2'
            '3a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a'
            '595b5dc35589e55351565789c11ee8bcfe88d5bf4c0289fabb4c0289fe88c839fb7316803f02750583c306eb'
            'f23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83bffeb'
            'e91f8d66f85f5eeb9f803e220200750331c0c35250cceb0657564944454fb8010083c404c30071000301dc04'
            '02009f000301ef04020099000301f60402009e000301ff0402002a0003010f05020072010301220502007301'
            '03012d050200720103013c0502001a0003014d0502001a000200530501004000050057050100400003015e05'
            '010033000301650502001b00030170050200180003017d05020018000301860502009f0003018f0502000801'
            '0301990502009f0003019f05020007010301bd050200a4010301020602009601030123060200970103013206'
            '020070010301540602003f01030192060200a4010301a4060200ca010301ac060200c9010301b4060200c901'
            '0200c206010083000500c606010083000301cd06010033000301d406020057000301ef060200a40103010707'
            '02007d0003010d0702007c0003011c0702006c0003013f070200c800030150070200a4010301690702000100'
            '030174070200d80003017e070200710003018807020072000301ed0701005b0003012c0801001e0003013308'
            '0200050002004d080100e80703015e0801000300030190080100310003018009020001000301380001005800'
            '030161000200ae010301aa00010058000301c100020064000301d400020065000301f3000200630003010701'
            '0200650003010d010200580003014201020027000301790101005a0003017f010300210003018f0102002800'
            '0301a201020006000301b60102006b000500e70101001c010200ec0101001c010301fd010200ad0003010802'
            '0300570003011d020200390003014c0202002900030158020200910003016402020089000301690202008b00'
            '0301790201000f00030182020100120003018e020100580003019602010013000301a20202008d000301b202'
            '01000f000301bb02010012000301c702010058000301cf02010013000301da0202008d000301df0202008a00'
            '0301e502020089000301ee0202008e0003010103010012000301150301005800030122030100130003012a03'
            '02008e0003013d0301001200030151030100580003015e03010013000301630302008f0003016b0302008a00'
            '03018a030200a40103019d030100ce000301ad030100c4000301b8030100b0000301c4030100b0000301d003'
            '0100b0000301e003010058000301f403010060010200f90301001c010500fd0301001c010301010401005d00'
            '0200060401001c01030132040200a401030163040100580003018504010058000301a704010058000301b204'
            '0200dc000301bd0402009f000301d30402009900000000000000000000000000000000000000000005000000'
            '00000000000000004f7750726f7000433a5c484f535457494e5c2a2e444c4c004f7741706932004b45524e45'
            '4c20616e6420555345522c206d6f7265004f7741706932206461746100636c6970626f617264207465787400'
            '6f776e006e6f6e6500636c69702022257322202225732220256420666f726d61747300616263006465666768'
            '696a0061727261792025642c207269676874732025782c206361746e2025732c20636f646520256400264669'
            '6c6500264f6e65002654776f0026546872656500706c6163652025642c25642073686f772025642c2070726f'
            '70732025642c206d656e7520256420256400637572736f722025642069636f6e20256420636f70792025642c'
            '207363726f6c6c656420256400444952444c47006469723a20256420656e74726965732c2073706563202573'
            '2c20666972737420257300434f4d31004f70656e436f6d6d202564004f70656e20576174636f6d000000ffff'
            '00000000000000000000000c0000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '000000000000a309a30900000000000000000000000000000000000000000000000000000000000000000000'
            '000000209a07000000000000000000000000000000000000c000c8800314001400a00064000000446c674469'
            '724c69737400080048656c7600080004008c000a00650000000250820000080010006400460064000300a050'
            '8300007200100028000e00010001000150804f4b0000'
        )),
    'OWMETA': (
        # wcc -bt=windows -ms owmeta.c; wlink @owmeta.lnk
        # ---- owmeta.c
        #     #include <windows.h>
        #     #include <string.h>
        #
        #     /* Metafiles: one recorded (CreateMetaFile: pens, brushes, a hatch and a
        #      * pattern brush, a font, a region, shapes, text, a blit from a memory DC,
        #      * a DIB), played as it is, scaled into half the size (MM_ANISOTROPIC), and
        #      * so again record by record (EnumMetaFile, PlayMetaFileRecord); its bits
        #      * (GetMetaFileBits, SetMetaFileBits), a file (CopyMetaFile, GetMetaFile);
        #      * what it found as text */
        #     /* Windows 3.1's, not in Open Watcom's windows.h (imported in owmeta.lnk) */
        #     BOOL FAR PASCAL IsValidMetaFile(HMETAFILE mf);
        #
        #     static HMETAFILE mf;
        #     static char info[2][96];
        #     static int records, objects, played;
        #
        #     /* an 8x8 8-bit DIB: a red cross on yellow */
        #     static struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } dib;
        #     static BYTE bits[8 * 8];
        #
        #     int FAR PASCAL EnumProc(HDC dc, HANDLETABLE FAR *ht, METARECORD FAR *mr, int n, LPARAM lp)
        #     {
        #         records++;
        #         if (mr->rdFunction == META_CREATEPENINDIRECT || mr->rdFunction == META_CREATEBRUSHINDIRECT)
        #             objects++;
        #         if (lp == 1) {
        #             PlayMetaFileRecord(dc, ht, mr, n);
        #             played++;
        #         }
        #         return 1;
        #     }
        #
        #     static HMETAFILE record(void)
        #     {
        #         HDC dc = CreateMetaFile(NULL), mem;
        #         HBITMAP bm, old;
        #         HBRUSH red = CreateSolidBrush(RGB(255, 0, 0)), hatch = CreateHatchBrush(HS_CROSS, RGB(0, 0, 255));
        #         HBRUSH pat;
        #         HPEN pen = CreatePen(PS_SOLID, 3, RGB(0, 128, 0));
        #         HFONT font = CreateFont(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Helv");
        #         HRGN rgn = CreateEllipticRgn(200, 10, 260, 60);
        #         POINT tri[3] = { { 130, 70 }, { 180, 110 }, { 110, 110 } };
        #         RECT rc;
        #         int x, y;
        #         static WORD patbits[8] = { 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 };
        #         HBITMAP pb = CreateBitmap(8, 8, 1, 1, patbits);
        #         pat = CreatePatternBrush(pb);
        #         SelectObject(dc, pen);
        #         SelectObject(dc, red);
        #         Rectangle(dc, 10, 10, 90, 60);
        #         SelectObject(dc, hatch);
        #         Ellipse(dc, 100, 10, 190, 60);
        #         SelectObject(dc, pat);
        #         Polygon(dc, tri, 3);
        #         SelectObject(dc, GetStockObject(BLACK_PEN));
        #         MoveTo(dc, 10, 70);
        #         LineTo(dc, 90, 110);
        #         Pie(dc, 200, 70, 260, 120, 260, 95, 230, 70);
        #         FillRgn(dc, rgn, red);
        #         SetRect(&rc, 270, 10, 330, 40);
        #         FillRect(dc, &rc, hatch);
        #         SetTextColor(dc, RGB(0, 0, 160));
        #         SetBkMode(dc, TRANSPARENT);
        #         SelectObject(dc, font);
        #         TextOut(dc, 10, 120, "Recorded", 8);
        #         SetRect(&rc, 100, 120, 190, 136);
        #         SetBkColor(dc, RGB(255, 255, 0));
        #         ExtTextOut(dc, 104, 120, ETO_OPAQUE, &rc, "ExtText", 7, NULL);
        #         for (x = 0; x < 6; x++)
        #             SetPixel(dc, 340 + x, 10 + x, RGB(255, 0, 255));
        #         mem = CreateCompatibleDC(NULL);     /* a blit: its pixels into the record */
        #         bm = CreateCompatibleBitmap(GetDC(NULL), 20, 20);
        #         old = SelectObject(mem, bm);
        #         PatBlt(mem, 0, 0, 20, 20, WHITENESS);
        #         for (y = 0; y < 20; y += 4)
        #             PatBlt(mem, 0, y, 20, 2, BLACKNESS);
        #         BitBlt(dc, 270, 50, 20, 20, mem, 0, 0, SRCCOPY);
        #         StretchBlt(dc, 300, 50, 40, 40, mem, 0, 0, 20, 20, SRCCOPY);
        #         SelectObject(mem, old);
        #         DeleteDC(mem);
        #         DeleteObject(bm);
        #         StretchDIBits(dc, 270, 100, 32, 32, 0, 0, 8, 8, bits, (BITMAPINFO FAR *)&dib, DIB_RGB_COLORS,
        #                       SRCCOPY);
        #         SelectObject(dc, GetStockObject(WHITE_BRUSH));
        #         SelectObject(dc, GetStockObject(SYSTEM_FONT));
        #         DeleteObject(red);
        #         DeleteObject(hatch);
        #         DeleteObject(pat);
        #         DeleteObject(pb);
        #         DeleteObject(pen);
        #         DeleteObject(font);
        #         DeleteObject(rgn);
        #         return CloseMetaFile(dc);
        #     }
        #
        #     static void make_dib(void)
        #     {
        #         int x, y;
        #         dib.h.biSize = sizeof(BITMAPINFOHEADER);
        #         dib.h.biWidth = 8;
        #         dib.h.biHeight = 8;
        #         dib.h.biPlanes = 1;
        #         dib.h.biBitCount = 8;
        #         dib.h.biClrUsed = 2;
        #         dib.c[0].rgbRed = dib.c[0].rgbGreen = 255;
        #         dib.c[1].rgbRed = 255;
        #         for (y = 0; y < 8; y++)
        #             for (x = 0; x < 8; x++)
        #                 bits[y * 8 + x] = (BYTE)(x == 3 || x == 4 || y == 3 || y == 4);
        #     }
        #
        #     /* the bits and back, a file and back: sizes, validity */
        #     static void check(void)
        #     {
        #         HGLOBAL g;
        #         HMETAFILE f;
        #         DWORD size;
        #         char FAR *p;
        #         int ok;
        #         OFSTRUCT of;
        #         g = GetMetaFileBits(mf);
        #         size = GlobalSize(g);
        #         p = GlobalLock(g);
        #         ok = p && *(WORD FAR *)p == 1 && *(WORD FAR *)(p + 2) == 9;
        #         GlobalUnlock(g);
        #         mf = SetMetaFileBits(g);
        #         f = CopyMetaFile(mf, "C:\\HOSTWIN\\OWMETA.WMF");
        #         DeleteMetaFile(f);
        #         f = GetMetaFile("C:\\HOSTWIN\\OWMETA.WMF");
        #         records = objects = 0;
        #         EnumMetaFile(GetDC(NULL), f, (MFENUMPROC)EnumProc, 0);
        #         wsprintf(info[0], "bits %d, header %d, valid %d %d, file %d, records %d, pens+brushes %d",
        #                  size >= 400, ok, IsValidMetaFile(mf), IsValidMetaFile(f), f != 0, records, objects);
        #         DeleteMetaFile(f);
        #         OpenFile("C:\\HOSTWIN\\OWMETA.WMF", &of, OF_DELETE);
        #     }
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         switch (msg) {
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             PlayMetaFile(dc, mf);
        #             SaveDC(dc);                     /* again, half the size, to the right */
        #             SetMapMode(dc, MM_ANISOTROPIC);
        #             SetWindowExt(dc, 2, 2);
        #             SetViewportExt(dc, 1, 1);
        #             SetViewportOrg(dc, 380, 0);
        #             PlayMetaFile(dc, mf);
        #             SetViewportOrg(dc, 380, 90);    /* and below it record by record */
        #             played = 0;
        #             EnumMetaFile(dc, mf, (MFENUMPROC)EnumProc, 1);
        #             RestoreDC(dc, -1);
        #             wsprintf(info[1], "played %d", played);
        #             TextOut(dc, 10, 230, info[0], lstrlen(info[0]));
        #             TextOut(dc, 10, 250, info[1], lstrlen(info[1]));
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_DESTROY:
        #             DeleteMetaFile(mf);
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         make_dib();
        #         mf = record();
        #         check();
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwMeta";
        #         RegisterClass(&wc);
        #         h = CreateWindow("OwMeta", "Metafiles", WS_OVERLAPPEDWINDOW, 10, 10, 600, 310, NULL, NULL,
        #                          hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owmeta.lnk
        #     system windows
        #     name owmeta.exe
        #     file owmeta.o
        #     import ISVALIDMETAFILE GDI.410
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '710002000000000002030200000400203808010000000200020003000e004000500050005a006000f3000000'
            '000001000000020000000000000000038100320b400d320bdf07ae01410cf002064f574d4554410000000100'
            '05000a0000034744490455534552064b45524e454c00000a6f776d6574612e65786500000000ccebfd909090'
            '90001e5890455589e51e8ed8b81400e8d707c45e0cff06e202268b47043dfa0274053dfc027504ff06de0283'
            '7e0800751d837e06017517ff7614ff7612ff76100653ff760a9affff0000ff06e002b801001f5d4dca100055'
            '89e5b86200e88907535152565783ec3431c050509affff00008946f48946f231c050b8ff00509affff000089'
            '46f08946dab8040050b8ff005031c0509affff00008946ee8946dc31c050b803005031c050b80080509affff'
            '00008946d68946e2b8f4ff5031c0505050b8bc025031c050505050505050501eb81000509affff00008946ea'
            '8946e8b8c80050b80a0050b8040150b83c00509affff00008946ec8946d88cd88ec08d7ec2beaa00a5a5a5a5'
            'a5a5b808005050b8010050501eb8b600509affff00008946e6509affff000089c68946e0ff76f4ff76d69aff'
            'ff0000ff76f4ff76f09affff0000ff76f4b80a005050b85a0050b83c00509affff0000ff76f4ff76ee9affff'
            '0000ff76f4b8640050b80a0050b8be0050b83c00509affff0000ff76f4569affff0000ff76f48d76c21e56b8'
            '0300509affff0000ff76f4b80700509affff0000509affff0000ff76f4b80a0050b84600509affff0000ff76'
            'f4b85a0050b86e00509affff0000ff76f4b8c80050b8460050b8040150b8780050b8040150b85f0050b8e600'
            '50b84600509affff0000ff76f4ff76ecff76f09affff00008d76ce1e56b80e0150b80a0050b84a0150b82800'
            '509affff0000ff76f48d76ce1e56ff76ee9affff0000ff76f4b8a0005031c0509affff0000ff76f4b8010050'
            '9affff0000ff76f4ff76ea9affff0000ff76f4b80a0050b87800501eb8150050b80800509affff00008d76ce'
            '1e56b8640050b8780050b8be0050b88800509affff0000ff76f431c050b8ffff509affff0000ff76f4b86800'
            '50b8780050b80200508d76ce1e561eb81e0050b807005031c050509affff000031f6ff76f28d845401508d44'
            '0a50b8ff0050509affff00004683fe067ce431c0509affff000089c689c731c0509affff000050b814005050'
            '9affff00008946e456509affff00008946de5631c05050b814005050b8ff0050b86200509affff000031f657'
            '31c05056b8140050b802005031c050b84200509affff000083c60483fe147cdfff76f2b80e0150b8320050b8'
            '140050505731c05050b8cc0050b82000509affff0000ff76f2b82c0150b8320050b8280050505731c05050b8'
            '14005050b8cc0050b82000509affff000057ff76de9affff0000579affff0000ff76e49affff0000ff76f2b8'
            '0e0150b8640050b82000505031c05050b8080050501eb8de01501eb8ae015031c050b8cc0050b82000509aff'
            'ff0000ff76f231c0509affff0000509affff0000ff76f2b80d00509affff0000509affff0000ff76da9affff'
            '0000ff76dc9affff0000ff76e09affff0000ff76e69affff0000ff76e29affff0000ff76e89affff0000ff76'
            'd89affff0000ff76f29affff00008d66f65f5e5a595b5dc3b80a00e8ab0353515256c706ae01280031c0a3b0'
            '01c706b2010800a3b401c706b6010800a3b801c706ba010100c706bc010800c706ce010200a3d001b0ffa2d7'
            '01a2d801a2dc0131d2eb064283fa087d3031c0eb1e30dbb10389d6d3e601c6889cde01403d08007de23d0300'
            '740f3d0400740a83fa03740583fa0475d8b301ebd65e5a595bc35589e5b8b200e82203535152565781ec8e00'
            'ff36e4029affff000089c689c7509affff00008946f08956f4569affff000089c385d2750485c074148ec226'
            '833f01750c26837f02097505b80100eb0231c08946f2579affff0000579affff0000a3e402501eb82600509a'
            'ffff0000509affff00001eb82600509affff000089c689c731c0a3de02a3e202509affff00005056b8ffff50'
            'b8ffff5031c050509affff0000ff36de02ff36e20285f67405b80100eb0231c050579affff000050ff36e402'
            '9affff000050ff76f2837ef4007507817ef090017205b80100eb0231c0501eb83c00501eb81e02509affff00'
            '0083c416579affff00001eb82600508d9668ff1e52b80002509affff0000e961fe1e5890455589e51e8ed8b8'
            '3800e80c02565783ec208b460c8b7e0e3d0f00740b3d02007403e9fd00e9e700578d76da1e569affff000089'
            'c650ff36e4029affff0000569affff000056b80800509affff000056b8020050509affff000056b801005050'
            '9affff000056b87c015031c0509affff000056ff36e4029affff000056b87c0150b85a00509affff000031c0'
            'a3e00256ff36e402b8ffff50b8ffff5031c050b80100509affff000056b8ffff509affff0000ff36e0021eb8'
            '8200501eb87e02509affff000083c40a56b80a0050b8e600501eb81e02501e509affff0000509affff000056'
            'b80a0050b8fa00501eb87e02501e509affff0000509affff0000578d76da1e569affff000031c031f6eb25ff'
            '36e4029affff000031c0509affff0000ebe75750ff760aff7608ff76069affff000089d689f28d66fa5f5e1f'
            '5d4dca0a005589e5b85000e8cf005683ec2c8b760ce814fde830f9a3e402e891fdb91a0031db8cda8d46d2e8'
            'e100c746d4ffffc746d6ffff8976dc31c05050b8007f509affff00008946e031c0509affff00008946e2c746'
            'e88c008c5eea8d56d21e529affff00001eb88c00501eb8930050b8cf005031c050b80a005050b8580250b836'
            '015031c050505650509affff000089c650ff76049affff0000569affff00008d56ec1e5231c05050509affff'
            '000085c074168d56ec1e529affff00008d56ec1e529affff0000ebd78b46f08d66fe5e5dc20a00008c16c600'
            'c33bc473132bc4f7d83b060a0077068cd03b06c6007401c30e5ab81108bb0100e9c801537461636b204f7665'
            '72666c6f7721005789c788d88ec2578ae0d1e9f3ab13c9f3aa5f89f85fc3eb0203009affff00000bc0750e50'
            '33c0ba0f00e86a0258b44ccd218c06cc00575606535253bee6028936e7008c1ee900268a078804464384c075'
            'f589360a005b2bc0509affff000057579affff0000fc1e07b9e602bfae012bcf32c0f3aab88209a3e300b8ff'
            'ffa3e500b430cd21a2ce008826cf009affff0000a2d0008826d10086e0a3d20054583bc475120f01e0a90100'
            '740ab001a2d400b003a2d5001ebfef0057b89000509affff0000b8ef00893eeb008c1eed00b8ff00e86b01e8'
            '1ffee9f9004f70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374656d2e20436f'
            '7079726967687420286329204f70656e20576174636f6d20436f6e7472696275746f727320323030322d3230'
            '32362e20506f7274696f6e7320436f7079726967687420284329205379626173652c20496e632e2031393838'
            '2d323030322e00c3cb005589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d'
            '46f939c3730c268a048ec14626880743ebe031c08ec15026c607001e8d9e7aff531eb89e005031c0509affff'
            '000089f8e94400515689c189d6e8330185c0750689c889f2eb9889d8e92c0000c3c3535289c3833ea6010074'
            '04ff16a601ff168001baff00b81000e8a800ff168001ff16820189d8e928fe5289c2ff168001ff16820189d0'
            'e918fe5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c07504'
            '85d274df1eff1f1febd95589e5535152565789c11ee813ffbfae0189fabba80189fe88c839fb7316803f0275'
            '0583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f'
            '8d66f65f5e5a595b5dc35589e55351565789c11ee8bcfe88d5bfae0189fabbae0189fe88c839fb7316803f02'
            '750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ff'
            'ebeee83bffebe91f8d66f85f5eeb9f803e840100750331c0c35250cceb0657564944454fb8010083c404c300'
            '71000301500502004200020057050100080005005b0501000800030163050100af0003017d0501009a010301'
            '870501009a010301af050200a4010301b80501007f000301cc0503004a000301050602002700030111060100'
            '7b000301170601001e00030121060100030003012c0601000c000301370601000e000301440601000d000301'
            '4e0601007b0003015c0601000d0002006b060100080005006f060100080003017a060100af00030184060100'
            '2700030197060200a4010301af0603005a000301b506010021000301ca0603005a000301d006010021000301'
            'db06020028000301ea0601007f000301f206020006000301040702006b00050043070100d305020048070100'
            'd305030156070200ad00030161070100570003017607020039000301a007020029000301ab0702002a000301'
            'b10702007c000301c00702006c000301ce07020071000301d8070200720003013d0803005b0003017c080300'
            '1e00030183080200050002009d08010038080301ae08030003000301e008030031000301d009020001000301'
            '4c000100b0000301730001007d00030185000100420003019b0001003a000301b40001003d000301db000100'
            '38000301f6000100360003012001010030000301290101003c000301390101002d000301440101002d000301'
            '590101001b000301640101002d0003017c01010018000301850101002d0003019601010024000301a2010100'
            '57000301a80101002d000301b801010014000301c801010013000301f00101001a000301fe01010028000301'
            '18020200480003012802020051000301370201000900030143020100020003014e0201002d00030167020100'
            '2100030181020200480003019002010001000301b60201005f010301ce0201001f000301dc02010034000301'
            'e802020042000301f302010033000301fd0201002d000301170301001d000301320301001d0003015c030100'
            '2200030183030100230003018c0301002d00030192030100440003019a03010045000301cd030100b7010301'
            'd803010057000301de0301002d000301ea03010057000301f00301002d000301f80301004500030100040100'
            '4500030108040100450003011004010045000301180401004500030120040100450003012804010045000301'
            '300401007e000301db0401009f000301e504030014000301f1040300120003011a0503001300030120050100'
            'a00003012e05010097000301340501007f0003013e0501007c00000000000500000000000000000000004865'
            '6c76005265636f72646564004578745465787400433a5c484f535457494e5c4f574d4554412e574d46006269'
            '74732025642c206865616465722025642c2076616c69642025642025642c2066696c652025642c207265636f'
            '7264732025642c2070656e732b6272757368657320256400706c61796564202564004f774d657461004d6574'
            '6166696c657300004f70656e20576174636f6d0082004600b4006e006e006e00aa005500aa005500aa005500'
            'aa0055000000ffff00000000000000000000000c000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000f309f3090000000000000000000000000000000000000000000000000000'
            '000000000000000000000020ea070000'
        )),
    'OWSYS': (
        # wcc -bt=windows -ms owsys.c; wlink @owsys.lnk; wrc -bt=windows owsys.rc owsys.exe
        # ---- owsys.c
        #     #include <windows.h>
        #     #include <ddeml.h>
        #     #include <dde.h>
        #     #include <mmsystem.h>
        #     #include <toolhelp.h>
        #     #include <ver.h>
        #     #include <lzexpand.h>
        #     #include <string.h>
        #
        #     /* Windows' own DLLs, as WINN has them: MMSYSTEM (no devices, MCI none,
        #      * timeGetTime, timeSetEvent's procedure, mmio: a RIFF file written and
        #      * read back), TOOLHELP (the timer, the memory, tasks, modules,
        #      * MemoryRead), VER (its own VERSIONINFO, VerQueryValue, VerLanguageName,
        #      * VerFindFile, VerInstallFile expanding) and LZEXPAND (a file compressed
        #      * as COMPRESS.EXE does: LZOpenFile finds it by its name ending in _,
        #      * LZRead, LZSeek, LZCopy, GetExpandedName); the DOS environment by its
        #      * PSP, LocalReAlloc keeping a block's bytes; the BIOS's interrupts (10h,
        #      * 11h, 12h, 1Ah) and DOS's code page; LZEXPAND as a library:
        #      * LoadLibrary, GetModuleHandle, GetProcAddress by name and by ordinal (the
        #      * import's function), FreeLibrary; what it found as text */
        #     static char lines[13][100];
        #     static volatile int ticks;
        #     static UINT timer;
        #
        #     void FAR PASCAL TimeProc(UINT id, UINT msg, DWORD user, DWORD dw1, DWORD dw2)
        #     {
        #         if (user == 1234)
        #             ticks++;
        #     }
        #
        #     /* "ABCABCABCABC": three bytes as they are, then nine from the window */
        #     static const BYTE packed[] = { 'S', 'Z', 'D', 'D', 0x88, 0xf0, 0x27, 0x33, 'A', 'T',
        #                                    12, 0, 0, 0, 0x07, 'A', 'B', 'C', 0xf0, 0xf6 };
        #
        #     static void mm(void)
        #     {
        #         char err[64], ret[16];
        #         MMCKINFO riff, ck;
        #         HMMIO h;
        #         long n;
        #         DWORD t0 = timeGetTime();
        #         static char data[100], back[100];
        #         int i;
        #         PCMWAVEFORMAT fmt;
        #         for (i = 0; i < 100; i++)
        #             data[i] = (char)i;
        #         _fmemset(&fmt, 0, sizeof(fmt));
        #         fmt.wf.wFormatTag = WAVE_FORMAT_PCM;
        #         fmt.wf.nChannels = 1;
        #         fmt.wf.nSamplesPerSec = 11025;
        #         fmt.wf.nAvgBytesPerSec = 11025;
        #         fmt.wf.nBlockAlign = 1;
        #         fmt.wBitsPerSample = 8;
        #         h = mmioOpen("C:\\HOSTWIN\\OWSYS.WAV", NULL, MMIO_CREATE | MMIO_WRITE);
        #         riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
        #         mmioCreateChunk(h, &riff, MMIO_CREATERIFF);
        #         ck.ckid = mmioFOURCC('f', 'm', 't', ' ');
        #         ck.cksize = 0;
        #         mmioCreateChunk(h, &ck, 0);
        #         mmioWrite(h, (HPSTR)&fmt, sizeof(fmt));
        #         mmioAscend(h, &ck, 0);
        #         ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
        #         mmioCreateChunk(h, &ck, 0);
        #         mmioWrite(h, data, 100);
        #         mmioAscend(h, &ck, 0);
        #         mmioAscend(h, &riff, 0);
        #         mmioClose(h, 0);
        #         h = mmioOpen("C:\\HOSTWIN\\OWSYS.WAV", NULL, MMIO_READ);
        #         riff.fccType = mmioStringToFOURCC("wave", MMIO_TOUPPER) == mmioFOURCC('W', 'A', 'V', 'E') ?
        #                        mmioFOURCC('W', 'A', 'V', 'E') : 0;
        #         i = mmioDescend(h, &riff, NULL, MMIO_FINDRIFF);
        #         ck.ckid = mmioFOURCC('d', 'a', 't', 'a');
        #         i += mmioDescend(h, &ck, &riff, MMIO_FINDCHUNK);
        #         n = mmioRead(h, back, sizeof(back));
        #         mmioClose(h, 0);
        #         mmioOpen("C:\\HOSTWIN\\OWSYS.WAV", NULL, MMIO_DELETE);
        #         mciGetErrorString(mciSendString("open cdaudio", ret, sizeof(ret), NULL), err, sizeof(err));
        #         wsprintf(lines[0], "devs %d %d %d %d, snd %d, riff %d %ld %ld %d, %s",
        #                  waveOutGetNumDevs(), midiOutGetNumDevs(), joyGetNumDevs(), auxGetNumDevs(),
        #                  sndPlaySound("X.WAV", SND_ASYNC), i, riff.cksize, ck.cksize,
        #                  n == 100 && !_fmemcmp(back, data, 100), (LPSTR)err);
        #         timer = timeSetEvent(50, 10, (LPTIMECALLBACK)TimeProc, 1234, TIME_PERIODIC);
        #         wsprintf(lines[1], "time %d, event %d", timeGetTime() >= t0, timer != 0);
        #     }
        #
        #     static void th(void)
        #     {
        #         TIMERINFO ti;
        #         MEMMANINFO mi;
        #         TASKENTRY te;
        #         MODULEENTRY me;
        #         SYSHEAPINFO hi;
        #         int tasks = 0, mods = 0, mine = 0;
        #         char first[10];
        #         WORD w = 0x1234, got = 0;
        #         ti.dwSize = sizeof(ti);
        #         mi.dwSize = sizeof(mi);
        #         te.dwSize = sizeof(te);
        #         me.dwSize = sizeof(me);
        #         hi.dwSize = sizeof(hi);
        #         TimerCount(&ti);
        #         MemManInfo(&mi);
        #         SystemHeapInfo(&hi);
        #         if (TaskFirst(&te))
        #             do {
        #                 tasks++;
        #                 mine |= te.hTask == GetCurrentTask() && !lstrcmp(te.szModule, "OWSYS");
        #             } while (TaskNext(&te));
        #         first[0] = 0;
        #         if (ModuleFirst(&me)) {
        #             lstrcpy(first, me.szModule);
        #             do
        #                 mods++;
        #             while (ModuleNext(&me));
        #         }
        #         MemoryRead(SELECTOROF((LPVOID)&w), OFFSETOF((LPVOID)&w), &got, 2);
        #         wsprintf(lines[2], "timer %d, page %u, heap %u%%, tasks %d mine %d, modules %d %s, user %d, read %x",
        #                  ti.dwmsSinceStart > 0, mi.wPageSize, hi.wUserFreePercent, tasks > 0, mine, mods,
        #                  (LPSTR)first, ModuleFindName(&me, "USER") && !lstrcmp(me.szModule, "USER"), got);
        #     }
        #
        #     static void ver(void)
        #     {
        #         char path[128], name[48], cur[64], dest[64], dir[128], file[16], *p;
        #         DWORD handle, size;
        #         static char block[1024];
        #         VS_FIXEDFILEINFO FAR *fi = NULL;
        #         LPSTR desc = NULL;
        #         DWORD FAR *tr = NULL;
        #         UINT len = 0, curlen = sizeof(cur), destlen = sizeof(dest);
        #         GetModuleFileName(GetModuleHandle("OWSYS"), path, sizeof(path));
        #         size = GetFileVersionInfoSize(path, &handle);
        #         GetFileVersionInfo(path, handle, sizeof(block), block);
        #         VerQueryValue(block, "\\", (VOID FAR * FAR *)&fi, &len);
        #         VerQueryValue(block, "\\StringFileInfo\\040904E4\\FileDescription", (VOID FAR * FAR *)&desc,
        #                       &len);
        #         VerQueryValue(block, "\\VarFileInfo\\Translation", (VOID FAR * FAR *)&tr, &len);
        #         VerLanguageName(tr ? LOWORD(*tr) : 0, name, sizeof(name));
        #         for (p = path + lstrlen(path); p > path && p[-1] != '\\'; p--)
        #             ;                               /* its own directory: where it is found */
        #         lstrcpy(file, p);
        #         lstrcpy(dir, path);
        #         dir[p - path > 3 ? p - path - 1 : p - path] = 0;
        #         VerFindFile(0, file, "C:\\WINDOWS", dir, cur, &curlen, dest, &destlen);
        #         wsprintf(lines[3], "ver %d, file %lx, \"%s\", %s, find %d %d", size > 100,
        #                  fi ? fi->dwFileVersionMS : 0L, desc ? desc : (LPSTR)"", (LPSTR)name,
        #                  !lstrcmpi(cur, dir), !lstrcmpi(dest, dir));
        #     }
        #
        #     /* the DOS environment through the PSP (2Ch), as a C startup reads it:
        #      * its strings, the count 1, the program's path; a local block made
        #      * larger keeps what it had */
        #     static void env(void)
        #     {
        #         WORD psp = GetCurrentPDB(), seg = *(WORD FAR *)MAKELP(psp, 0x2c);
        #         char FAR *e = (char FAR *)MAKELP(seg, 0);
        #         char path[128];
        #         int n = 0, count, same;
        #         HLOCAL h = LocalAlloc(LMEM_MOVEABLE, 8);
        #         char NEAR *p = LocalLock(h);
        #         while (seg && e[0]) {               /* the strings */
        #             e += lstrlen(e) + 1;
        #             n++;
        #         }
        #         count = seg ? *(WORD FAR *)(e + 1) : 0;
        #         GetModuleFileName(GetModuleHandle("OWSYS"), path, sizeof(path));
        #         same = seg && !lstrcmpi(e + 3, path);
        #         lstrcpy(p, "kept");
        #         LocalUnlock(h);
        #         h = LocalReAlloc(h, 2000, LMEM_MOVEABLE);
        #         p = LocalLock(h);
        #         wsprintf(lines[6], "env %d, count %d, path %d, realloc \"%s\" %u", seg != 0 && n > 0, count,
        #                  same, h ? (LPSTR)p : (LPSTR)"", h ? LocalSize(h) : 0);
        #         LocalUnlock(h);
        #         LocalFree(h);
        #     }
        #
        #     /* the BIOS's interrupts, called as a program calls them (Windows'
        #      * extender answers): the display combination, the equipment, the memory,
        #      * the clock; DOS's code page (INT 21h 66h) */
        #     static void bios(void)
        #     {
        #         unsigned short combo = 0, equip = 0, mem = 0, ticks = 0, cp = 0, cf = 0;
        #         _asm {
        #             mov ax, 1a00h
        #             int 10h
        #             mov combo, bx
        #             int 11h
        #             mov equip, ax
        #             int 12h
        #             mov mem, ax
        #             mov ah, 0
        #             int 1ah
        #             mov ticks, dx
        #             mov ax, 6601h
        #             int 21h
        #             sbb cx, cx
        #             mov cf, cx
        #             mov cp, bx
        #         }
        #         wsprintf(lines[7], "bios vga %d, equip %x, mem %u, clock %d, cp %d", combo == 8, equip, mem,
        #                  ticks != 0, cf ? 0 : cp);
        #     }
        #
        #     /* Windows' DOS extender to a program: INT 2Fh's enhanced mode, DPMI
        #      * (INT 31h): its version, a descriptor made, set, read back and freed,
        #      * DOS memory; no DOS mouse driver */
        #     static void dosext(void)
        #     {
        #         unsigned short win = 0, ver = 0, sel = 0, base_hi = 0, base_lo = 0, err = 0;
        #         unsigned short dseg = 0, dsel = 0, dcf = 0, freed = 0, mouse = 1, bad = 0;
        #         unsigned char desc[8];
        #         unsigned char __far *d = desc;
        #         _asm {
        #             mov ax, 1600h
        #             int 2fh
        #             mov win, ax
        #             mov ax, 0400h
        #             int 31h
        #             mov ver, ax
        #             mov ax, 0000h
        #             mov cx, 1
        #             int 31h
        #             sbb cx, cx
        #             or err, cx
        #             mov sel, ax
        #             mov bx, ax
        #             mov ax, 0007h
        #             mov cx, 0012h
        #             mov dx, 3450h
        #             int 31h
        #             sbb cx, cx
        #             or err, cx
        #             mov ax, 0008h
        #             mov bx, sel
        #             xor cx, cx
        #             mov dx, 0fffh
        #             int 31h
        #             sbb cx, cx
        #             or err, cx
        #             mov ax, 0006h
        #             mov bx, sel
        #             int 31h
        #             mov base_hi, cx
        #             mov base_lo, dx
        #             push es
        #             push di
        #             les di, d
        #             mov ax, 000bh
        #             mov bx, sel
        #             int 31h
        #             pop di
        #             pop es
        #             mov ax, 0001h
        #             mov bx, sel
        #             int 31h
        #             sbb cx, cx
        #             or err, cx
        #             mov ax, 0100h
        #             mov bx, 4
        #             int 31h
        #             sbb cx, cx
        #             mov dcf, cx
        #             mov dseg, ax
        #             mov dsel, dx
        #             mov ax, 0101h
        #             mov dx, dsel
        #             int 31h
        #             sbb cx, cx
        #             not cx
        #             mov freed, cx
        #             mov ax, 0303h
        #             int 31h
        #             sbb cx, cx
        #             mov bad, cx
        #             xor ax, ax
        #             int 33h
        #             mov mouse, ax
        #         }
        #         wsprintf(lines[9], "dosext win %x, dpmi %x, desc %d %x%04x %d %x, dos %d %d, bad %d, mouse %d",
        #                  win, ver, sel != 0 && !err, base_hi, base_lo, desc[0] == 0xff && desc[1] == 0x0f &&
        #                  desc[2] == 0x50 && desc[3] == 0x34 && desc[4] == 0x12, desc[5],
        #                  !dcf && dseg != 0 && dsel != 0, freed != 0, bad != 0, mouse);
        #     }
        #
        #     /* DDEML: an instance as its own server and client: a request, a poke,
        #      * an execute, an advise loop; a service nobody has; the disconnection */
        #     static DWORD dde_inst;
        #     static HSZ hsvc, htopic, hitem;
        #     static int connects, confirms, advreqs, advdatas, disconnects;
        #     static char poked[16], executed[16], advgot[16];
        #
        #     HDDEDATA FAR PASCAL DdeCb(UINT type, UINT fmt, HCONV conv, HSZ h1, HSZ h2, HDDEDATA data,
        #                               DWORD d1, DWORD d2)
        #     {
        #         switch (type) {
        #         case XTYP_CONNECT:
        #             connects++;
        #             return (HDDEDATA)(DWORD)(h1 == htopic);
        #         case XTYP_CONNECT_CONFIRM:
        #             confirms++;
        #             return 0;
        #         case XTYP_REQUEST:
        #             if (h2 == hitem && fmt == CF_TEXT)
        #                 return DdeCreateDataHandle(dde_inst, "hello", 6, 0, hitem, CF_TEXT, 0);
        #             return 0;
        #         case XTYP_POKE:
        #             DdeGetData(data, poked, sizeof(poked), 0);
        #             return (HDDEDATA)(DWORD)DDE_FACK;
        #         case XTYP_EXECUTE:
        #             DdeGetData(data, executed, sizeof(executed), 0);
        #             return (HDDEDATA)(DWORD)DDE_FACK;
        #         case XTYP_ADVSTART:
        #             return (HDDEDATA)(DWORD)(h2 == hitem);
        #         case XTYP_ADVREQ:
        #             advreqs++;
        #             return DdeCreateDataHandle(dde_inst, "tick", 5, 0, hitem, CF_TEXT, 0);
        #         case XTYP_ADVDATA:
        #             advdatas++;
        #             DdeGetData(data, advgot, sizeof(advgot), 0);
        #             return (HDDEDATA)(DWORD)DDE_FACK;
        #         case XTYP_DISCONNECT:
        #             disconnects++;
        #             return 0;
        #         }
        #         return 0;
        #     }
        #
        #     /* ... and as those speaking DDE's messages themselves have it: the
        #      * server's window (WM_DDE_INITIATE), the commands (EXECUTE: its ACK gives
        #      * them back), the groups (REQUEST: DATA), the end (TERMINATE) */
        #     static HWND raw_server;
        #     static HGLOBAL raw_cmds;
        #     static UINT raw_status;
        #     static int raw_echo, raw_data, raw_term;
        #
        #     long FAR PASCAL RawDdeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         switch (msg) {
        #         case WM_DDE_ACK:
        #             if (!raw_server) {
        #                 raw_server = (HWND)wp;
        #                 GlobalDeleteAtom(LOWORD(lp));
        #                 GlobalDeleteAtom(HIWORD(lp));
        #             } else if (raw_cmds) {
        #                 raw_status = LOWORD(lp) | 1;
        #                 raw_echo = (HGLOBAL)HIWORD(lp) == raw_cmds;
        #                 GlobalFree((HGLOBAL)HIWORD(lp));
        #                 raw_cmds = 0;
        #             }
        #             return 0;
        #         case WM_DDE_DATA: {
        #             HGLOBAL d = (HGLOBAL)LOWORD(lp);
        #             LPSTR p = GlobalLock(d);
        #             raw_data = p && (p[1] & 0x30) == 0x30 && p[2] == CF_TEXT &&
        #                        _fstrstr(p + 4, "OwSys Raw\r\n") != NULL;
        #             GlobalUnlock(d);
        #             GlobalFree(d);
        #             GlobalDeleteAtom(HIWORD(lp));
        #             return 0;
        #         }
        #         case WM_DDE_TERMINATE:
        #             raw_term++;
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     static void pump(int *done)
        #     {
        #         MSG m;
        #         int i;
        #         for (i = 0; i < 400 && !*done; i++)
        #             if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        #                 TranslateMessage(&m);
        #                 DispatchMessage(&m);
        #             }
        #     }
        #
        #     static void raw_dde(HINSTANCE hInstance)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         ATOM app, topic;
        #         LPSTR p;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = RawDdeProc;
        #         wc.hInstance = hInstance;
        #         wc.lpszClassName = "OwSysDde";
        #         RegisterClass(&wc);
        #         h = CreateWindow("OwSysDde", "", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
        #         app = GlobalAddAtom("PROGMAN");
        #         topic = GlobalAddAtom("PROGMAN");
        #         SendMessage((HWND)-1, WM_DDE_INITIATE, (WPARAM)h, MAKELPARAM(app, topic));
        #         GlobalDeleteAtom(app);
        #         GlobalDeleteAtom(topic);
        #         if (raw_server) {
        #             raw_cmds = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 80);
        #             p = GlobalLock(raw_cmds);
        #             lstrcpy(p, "[CreateGroup(OwSys Raw)][AddItem(NOTEPAD.EXE,Raw DDE)]");
        #             GlobalUnlock(raw_cmds);
        #             PostMessage(raw_server, WM_DDE_EXECUTE, (WPARAM)h, MAKELPARAM(0, raw_cmds));
        #             pump((int *)&raw_status);
        #             PostMessage(raw_server, WM_DDE_REQUEST, (WPARAM)h,
        #                         MAKELPARAM(CF_TEXT, GlobalAddAtom("Groups")));
        #             pump(&raw_data);
        #             PostMessage(raw_server, WM_DDE_TERMINATE, (WPARAM)h, 0);
        #             pump(&raw_term);
        #         }
        #         wsprintf(lines[12], "raw dde server %d, execute %x %d, groups %d, terminate %d",
        #                  raw_server != 0, raw_status & ~1u, raw_echo, raw_data, raw_term);
        #         DestroyWindow(h);
        #     }
        #
        #     /* the Program Manager as setup programs have it: a group, an item */
        #     static void progman(void)
        #     {
        #         static char cmd[] = "[CreateGroup(OwSys Group)][AddItem(OWSYS.EXE,\"System, DLLs\")]"
        #                             "[ShowGroup(OwSys Group, 1)]";
        #         char list[200];
        #         HSZ pm = DdeCreateStringHandle(dde_inst, "PROGMAN", CP_WINANSI);
        #         HSZ groups = DdeCreateStringHandle(dde_inst, "Groups", CP_WINANSI);
        #         HCONV conv = DdeConnect(dde_inst, pm, pm, NULL);
        #         HDDEDATA d, ok;
        #         DWORD res = 0;
        #         list[0] = 0;
        #         ok = DdeClientTransaction((LPBYTE)cmd, sizeof(cmd), conv, 0, 0, XTYP_EXECUTE, 3000, &res);
        #         d = DdeClientTransaction(NULL, 0, conv, groups, CF_TEXT, XTYP_REQUEST, 3000, NULL);
        #         if (d) {
        #             DdeGetData(d, list, sizeof(list), 0);
        #             DdeFreeDataHandle(d);
        #         }
        #         DdeDisconnect(conv);
        #         wsprintf(lines[11], "progman conn %d, exec %d %x, groups %d", conv != 0, ok != 0,
        #                  (UINT)res, _fstrstr(list, "OwSys Group\r\n") != NULL);
        #     }
        #
        #     static void dde(HINSTANCE hInstance)
        #     {
        #         char req[16], str[16];
        #         HCONV conv, none;
        #         HDDEDATA d;
        #         DWORD res;
        #         UINT err, init;
        #         req[0] = str[0] = 0;
        #         init = DdeInitialize(&dde_inst, (PFNCALLBACK)MakeProcInstance((FARPROC)DdeCb, hInstance),
        #                              APPCLASS_STANDARD, 0);
        #         hsvc = DdeCreateStringHandle(dde_inst, "OwSys", CP_WINANSI);
        #         htopic = DdeCreateStringHandle(dde_inst, "Topic", CP_WINANSI);
        #         hitem = DdeCreateStringHandle(dde_inst, "Item", CP_WINANSI);
        #         DdeNameService(dde_inst, hsvc, 0, DNS_REGISTER);
        #         conv = DdeConnect(dde_inst, hsvc, htopic, NULL);
        #         d = DdeClientTransaction(NULL, 0, conv, hitem, CF_TEXT, XTYP_REQUEST, 1000, &res);
        #         if (d) {
        #             DdeGetData(d, req, sizeof(req), 0);
        #             DdeFreeDataHandle(d);
        #         }
        #         DdeClientTransaction("poked", 6, conv, hitem, CF_TEXT, XTYP_POKE, 1000, &res);
        #         DdeClientTransaction("[run]", 6, conv, 0, 0, XTYP_EXECUTE, 1000, &res);
        #         DdeClientTransaction(NULL, 0, conv, hitem, CF_TEXT, XTYP_ADVSTART, 1000, &res);
        #         DdePostAdvise(dde_inst, htopic, hitem);
        #         DdeQueryString(dde_inst, hitem, str, sizeof(str), CP_WINANSI);
        #         none = DdeConnect(dde_inst, DdeCreateStringHandle(dde_inst, "Nobody", CP_WINANSI), htopic,
        #                           NULL);
        #         err = DdeGetLastError(dde_inst);
        #         DdeDisconnect(conv);
        #         progman();
        #         wsprintf(lines[10], "dde %d %d %d %d, %s %s %s, adv %d %d %s, %s, none %d %x, disc %d %d",
        #                  init, conv != 0, connects, confirms, (LPSTR)req,
        #                  (LPSTR)poked, (LPSTR)executed, advreqs, advdatas, (LPSTR)advgot, (LPSTR)str,
        #                  none != 0, err, disconnects, DdeUninitialize(dde_inst));
        #     }
        #
        #     static void library(void)
        #     {
        #         HINSTANCE h = LoadLibrary("LZEXPAND.DLL");
        #         FARPROC byname = h >= 32 ? GetProcAddress(h, "LZStart") : NULL;
        #         FARPROC byord = h >= 32 ? GetProcAddress(h, MAKEINTRESOURCE(7)) : NULL;
        #         int (FAR PASCAL *start)(void) = (int (FAR PASCAL *)(void))byname;
        #         wsprintf(lines[8], "library %d, handle %d, proc %d %d, start %d, none %d", h >= 32,
        #                  GetModuleHandle("LZEXPAND") == h, byname != NULL && byname == byord,
        #                  byname == (FARPROC)LZStart, start ? start() : -1,
        #                  h >= 32 && GetProcAddress(h, "NoSuchFunction") == NULL);
        #         if (h >= 32)
        #             FreeLibrary(h);
        #     }
        #
        #     static void lz(void)
        #     {
        #         OFSTRUCT of;
        #         HFILE f;
        #         int h, n, m;
        #         long copied, inst;
        #         char buf[16], again[4], expanded[128], tmp[16];
        #         UINT tmplen = sizeof(tmp);
        #         f = _lcreat("C:\\HOSTWIN\\OWSYS.TX_", 0);
        #         _lwrite(f, packed, sizeof(packed));
        #         _lclose(f);
        #         h = LZOpenFile("C:\\HOSTWIN\\OWSYS.TXT", &of, OF_READ);
        #         n = LZRead(h, buf, sizeof(buf));
        #         buf[n > 0 ? n : 0] = 0;
        #         LZSeek(h, 3, 0);
        #         m = LZRead(h, again, 3);
        #         again[3] = 0;
        #         LZClose(h);
        #         GetExpandedName("C:\\HOSTWIN\\OWSYS.TX_", expanded);
        #         h = LZOpenFile("C:\\HOSTWIN\\OWSYS.TX_", &of, OF_READ);
        #         f = _lcreat("C:\\HOSTWIN\\OWSYS.OUT", 0);
        #         copied = LZCopy(h, f);
        #         LZClose(h);
        #         _lclose(f);
        #         inst = VerInstallFile(0, "OWSYS.TX_", "OWSYS2.TXT", "C:\\HOSTWIN", "C:\\HOSTWIN", "",
        #                               tmp, &tmplen);
        #         f = _lopen("C:\\HOSTWIN\\OWSYS2.TXT", OF_READ);
        #         m = m == 3 ? (int)_llseek(f, 0, 2) : -1;
        #         _lclose(f);
        #         wsprintf(lines[4], "lz \"%s\" \"%s\" %d, %s, copy %ld, install %ld %d", (LPSTR)buf,
        #                  (LPSTR)again, n, (LPSTR)expanded, copied, inst, m);
        #         OpenFile("C:\\HOSTWIN\\OWSYS.TX_", &of, OF_DELETE);
        #         OpenFile("C:\\HOSTWIN\\OWSYS.OUT", &of, OF_DELETE);
        #         OpenFile("C:\\HOSTWIN\\OWSYS2.TXT", &of, OF_DELETE);
        #     }
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         int i;
        #         switch (msg) {
        #         case WM_TIMER:
        #             KillTimer(h, 1);
        #             timeKillEvent(timer);
        #             wsprintf(lines[5], "ticks %d", ticks >= 4);    /* 0.3 s of 50 ms: 6 */
        #             InvalidateRect(h, NULL, TRUE);
        #             return 0;
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             for (i = 0; i < 13; i++)
        #                 TextOut(dc, 8, 8 + 20 * i, lines[i], lstrlen(lines[i]));
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_DESTROY:
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         mm();
        #         th();
        #         ver();
        #         lz();
        #         library();
        #         env();
        #         bios();
        #         dosext();
        #         dde(hInstance);
        #         raw_dde(hInstance);
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwSys";
        #         RegisterClass(&wc);
        #         h = CreateWindow("OwSys", "System DLLs", WS_OVERLAPPEDWINDOW, 0, 10, 640, 300, NULL, NULL,
        #                          hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         SetTimer(h, 1, 300, NULL);          /* 0.3 s of timeSetEvent's procedure */
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owsys.rc
        #     #include <windows.h>
        #     #include <ver.h>
        #
        #     VS_VERSION_INFO VERSIONINFO
        #     FILEVERSION 1,2,3,4
        #     PRODUCTVERSION 1,2,0,0
        #     FILEFLAGSMASK 0x3f
        #     FILEOS VOS__WINDOWS16
        #     FILETYPE VFT_APP
        #     BEGIN
        #         BLOCK "StringFileInfo"
        #         BEGIN
        #             BLOCK "040904E4"
        #             BEGIN
        #                 VALUE "CompanyName", "murm386"
        #                 VALUE "FileDescription", "WINN system DLLs test"
        #                 VALUE "FileVersion", "1.2.3.4"
        #             END
        #         END
        #         BLOCK "VarFileInfo"
        #         BEGIN
        #             VALUE "Translation", 0x409, 1252
        #         END
        #     END
        # ---- owsys.lnk
        #     system windows
        #     name owsys.exe
        #     file owsys.o
        #     library mmsystem.lib, toolhelp.lib, ver.lib, lzexpand.lib, ddeml.lib
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            'b7000200000000000203020000040020cc17010000000200020008000d004000500068007100810039010000'
            '0000010001000208a3000f1400000a03a3001c1b400d1c1b5a11b405410cf00f010010800100000000003c14'
            '760030000180000000000000054f5753595300000001000a000f0018001f0023002900320000084d4d535953'
            '54454d045553455208544f4f4c48454c50064b45524e454c03564552054444454d4c084c5a455850414e4403'
            '4744490000096f777379732e657865000000ccebfd90909090001e5890455589e51e8ed8b80800e8af16837e'
            '1000750e817e0ed2047507a11e0bff061e0b1f5d4dca10005589e5b8c000e88c16535152565781ec90009aff'
            'ff00008946f08956f2c746f400008b76f48a46f48884220bff46f4837ef4647cedb9100031db8cda8d46dee8'
            '8516b801008946de8946e0c746e2112bc746e40000c746e6112bc746e800008946eac746ec08001eb8100050'
            '31c0505050b80110509affff000089c6c746ae5741c746b05645508d56a61e52b82000509affff0000c746ba'
            '666dc746bc742031c08946be8946c0568d56ba1e52509affff0000568d56de1e5231c050b81000509affff00'
            '00568d56ba1e5231c0509affff0000c746ba6461c746bc7461568d56ba1e5231c0509affff0000561eb8220b'
            '5031c050b86400509affff0000568d56ba1e5231c0509affff0000568d56a61e5231c0509affff00005631c0'
            '509affff00001eb810005031c0505050509affff000089c61eb8250050b81000509affff000081fa5645750d'
            '3d57417508ba5741b85645eb0431d231c08956ae8946b0568d56a61e5231c05050b82000509affff000089c7'
            '8946f4c746ba6461c746bc7461568d56ba1e528d56a61e52b81000509affff000001c7897ef4561eb8860b50'
            '31c050b86400509affff00008946ee89d75631c0509affff00001eb810005031c0505050b80002509affff00'
            '001eb82a00508d56ce1e52b810005031c0509affff000052508d9666ff1e52b84000509affff00008d9666ff'
            '1e5285ff7520837eee64751ab8640050bb220b8cd9b8860b8cdae8c91485c07505b80100eb0231c050ff76c0'
            'ff76beff76acff76aaff76f41eb8370050b80100509affff0000509affff0000509affff0000509affff0000'
            '509affff0000501eb83d00501eb8f405509affff000083c422b8320050b80a0050b8ffff50b8ffff5031c050'
            'b8d20450b80100509affff0000a3200b85c07403b80100509affff00003b56f27707750a3b46f07205b80100'
            'eb0231c0501eb86e00501eb85806509affff000083c40c8d66f65f5e5a595b5dc35589e5b8b801e8c3135351'
            '52565781ec8e0131ff897ef231f6c746f43412897ef0c746da0c00897edcc7867cff2a0089be7effc746a628'
            '00897ea8c78668fe140189be6afec746ce0c00897ed08d56da1e529affff00008d967cff1e529affff00008d'
            '56ce1e529affff00008d56a61e529affff000085c07435479affff00003b46aa75188d56c01e521eb8800050'
            '9affff000085c07505b80100eb0231c009c68d56a61e529affff000085c075cbc646e6008d9668fe1e529aff'
            'ff000085c074228d56e61e528d966cfe1e529affff00008d9668fe1e52ff46f29affff000085c075ee8d56f4'
            '1e31c050528d56f01e5250b80200509affff0000ff76f08d9668fe1e521eb88600509affff000085c074198d'
            '966cfe1e521eb88600509affff000085c07505b80100eb0231c0508d56e61e52ff76f25685ff7e05b80100eb'
            '0231c050ff76d2ff76a48b46e00b46de85c07605b80100eb0231c0501eb88b00501eb8bc06509affff000083'
            'c41ce986fe5589e5b80402e85312535152565781ecd80131c08946e28946e48946e68946e88946ea8946ec89'
            '46f0b840008946f28946ee1eb88000509affff0000508d961efe1e52b88000509affff00008d961efe1e528d'
            '56de1e529affff000089c78956f48d961efe1e52ff76e0ff76de31c050b80004501eb8ea0b509affff00001e'
            'b8ea0b501eb8db00508d56e21e528d56f01e529affff00001eb8ea0b501eb8dd00508d56e61e528d56f01e52'
            '9affff00001eb8ea0b501eb80601508d56ea1e528d56f01e529affff0000837eec007506837eea007408c476'
            'ea268b04eb0231c0508d569e1e52b83000509affff00008d961efe1e529affff00008db61efe01c68d861efe'
            '39c67609807cff5c74034eebf38d56ce1e521e569affff00008d969efe1e528d961efe1e529affff00008d86'
            '1efe29c683fe037e014e30c088829efe30e4508d56ce1e521eb81f01508d969efe1e528d965eff1e528d56f2'
            '1e528d961eff1e528d56ee1e529affff00008d961eff1e528d969efe1e529affff000085c07505b80100eb02'
            '31c0508d965eff1e528d969efe1e529affff000085c07505b80100eb0231c0508d569e1e52837ee800750683'
            '7ee60074088b76e68b46e8eb05be2a018cd85056837ee4007506837ee200740dc476e2268b4408268b740aeb'
            '0431c031f65650837ef400750583ff647605be0100eb0231f6561eb82b01501eb82007509affff000083c41a'
            'e978fc5589e5b8ae00e84510535152565781ec8c009affff000031f683ce2c8ec0268b048946f231f68946f4'
            '31ffb8020050b80800509affff00008946f0509affff00008946ee837ef20074168e46f426803c00740d0656'
            '9affff00004001c647ebe48b46f285c074078e46f4268b44018946ec1eb88000509affff0000508d966aff1e'
            '52b88000509affff0000837ef200741b83c603ff76f4568db66aff1e569affff000085c07505b80100eb0231'
            'c08946ea1eff76ee1eb85201509affff0000ff76f09affff0000ff76f0b8d00750b80200509affff000089c6'
            '8946f0509affff00008946ee85f67408569affff0000eb0231c050837ef00074078cd88b76eeeb05be2a018c'
            'd85056ff76eaff76ec837ef200740985ff7e05b80100eb0231c0501eb85701501eb84c08509affff000083c4'
            '14ff76f09affff0000ff76f09affff0000e933fb5589e5b82c00e8000f535152565783ec0c31c08946ea8946'
            'ec8946ee8946f08946f28946f4b8001acd10899eeaffcd118986ecffcd128986eeffb400cd1a8996f0ffb801'
            '66cd2119c9898ef4ff899ef2ff837ef400740431c0eb038b46f2508b46f085c07403b80100508b46ee508b46'
            'ec50837eea087505b80100eb0231c0501eb88201501eb8b008509affff000083c412e99efa5589e5b85000e8'
            '6b0e535152565783ec2431c08946e08946f08946de8946e88946ea8946e28946e48946e68946f48946f2c746'
            'ec01008946ee8d56d28956da8c5edcb80016cd2f8986e0ffb80004cd318986f0ffb80000b90100cd3119c909'
            '8ee2ff8986deff8bd8b80700b91200ba5034cd3119c9098ee2ffb808008b9edeff33c9baff0fcd3119c9098e'
            'e2ffb806008b9edeffcd31898ee8ff8996eaff0657c4bedaffb80b008b9edeffcd315f07b801008b9edeffcd'
            '3119c9098ee2ffb80001bb0400cd3119c9898ef4ff8986e4ff8996e6ffb801018b96e6ffcd3119c9f7d1898e'
            'f2ffb80303cd3119c9898eeeff33c0cd338986ecff8b46ec508b46ee85c07403b80100508b46f285c07403b8'
            '010050837ef4007511837ee400740b837ee6007405b80100eb0231c0508a46d730e450807ed2ff751d807ed3'
            '0f7517807ed4507511807ed534750b807ed6127505b80100eb0231c0508b46ea508b46e850837ede00740b83'
            '7ee2007505b80100eb0231c0508b46f0508b46e0501eb8b101501eb87809509affff000083c41ee90df91e58'
            '90455589e51e8ed8b82000e8d30c8b560e8b5e108b4e128b46203d1040721e76553d9040720f76553dc28074'
            '5d3d7280744de92e013d50407453e926013d6210720f76153db020744b3d22207441e912013d3010743ce90a'
            '01ff061a0b8b46183b06ee0575138b46163b06ec05750ab8010099e9f100e9c80031c0ebf5eb5e31c031d2ff'
            '06180be9dd00e9cc00eb6ae98200eb6d8b46143b06ea05753b3b0ee8057535837e1e01752fff36f205ff36f0'
            '051eb8fb015031c050b806005031c05050ff36ea05ff36e805b801005031c0509affff0000e98f0031c0e988'
            '0053521eb8c4055031c050b810005031c050509affff0000b80080eb6c53521eb8d405ebe28b46143b06ea05'
            '7403e96cff3b0ee805e959ffff36f205ff36f0051eb801025031c050b805005031c05050ff36ea05ff36e805'
            'b801005031c050ff06160beb8f53521eb8b4055031c050b810005031c05050ff061c0beb9631c031d2ff0614'
            '0beb0431c031d21f5d4dca1c001e5890455589e51e8ed8b81600e8640b56578b46068b76088b5e0a8b560c81'
            'fae503745881fae403740981fae103744ae9a400833e0c0b007517891e0c0b509affff0000569affff000031'
            'c031d2e99200833e080b0074f20c01a30a0b3b36080b7505b80100eb0231c0a30e0b569affff000031c0a308'
            '0bebd0eb5189c7509affff000089c385d2750485c0742b8ec2268a470124303c30751f26807f020175188d47'
            '04bb06028cd9e8390b85d2750485c07405b80100eb0231c0a3120b579affff0000579affff0000e977ff31c0'
            '31d2ff06100beb0cff760e525356509affff00005f5e1f5d4dca0a005589e550b82c00e87f0a585351525657'
            '83ec1289c731f6eb0a4681fe90017c03e990f68b0585c075f78d56e41e52505050b80100509affff000085c0'
            '74db8d56e41e529affff00008d56e41e529affff0000ebc55589e550b84a00e82b0a58535152565783ec1e89'
            'c6b91a0031db8cda8d46d8e8450ac746daffffc746dcffff8976e2c746ee12028c5ef08d56d81e529affff00'
            '001eb81202501eb82a0150b800805031c0505050505050505650509affff000089c68946f41eb81b02509aff'
            'ff000089c71eb81b02509affff00008946f2b8ffff50b8e003505689f8ff76f2509affff0000579affff0000'
            'ff76f29affff0000833e0c0b007503e98600b802205031c050b85000509affff0000a3080b509affff000052'
            '501eb82302509affff0000ff36080b9affff0000ff360c0bb8e8035056a1080b5031c0509affff0000b80a0b'
            'e8b9feff360c0bb8e60350561eb85a02509affff000031d280ca0150529affff0000b8120be894feff360c0b'
            'b8e103505631c050509affff0000b8100be87cfeff36100bff36120bff360e0ba10a0b24fe50a10c0b85c074'
            '03b80100501eb86102501eb8a40a509affff000083c412ff76f49affff0000e9f5f45589e5b80201e8c20853'
            '5152565781ecd800ff36f205ff36f0051eb81b0250b8ec03509affff000089c789d6ff36f205ff36f0051eb8'
            '5a0250b8ec03509affff00008946ec8956eaff36f205ff36f0055657565731c050509affff000089c789d689'
            '46f28956f431c08946e68946e8c6861eff001eb872045031c050b8590050525731c0505050b850405031c050'
            'b8b80b508d56e61e529affff00008946ee8956f031c0505050505657ff76eaff76ecb8010050b8b0205031c0'
            '50b8b80b5031c050509affff000089c789d689d009f8741f52578d961eff1e5231c050b8c8005031c050509a'
            'ffff000056579affff0000ff76f4ff76f29affff0000bb9b028cd98cda8d861effe8320885d2750485c07405'
            'ba0100eb0231d252ff76e68b46f00b46ee7405ba0100eb0231d2528b46f40b46f27405ba0100eb0231d2521e'
            'b8a902501eb8400a509affff000083c410e9a3f35589e550b86e00e86f0758535152565783ec30c646c600c6'
            '46d6001ebaf00552baffff52baffff52509affff0000525031c0505050509affff00008946f0ff36f205ff36'
            'f0051eb8d00250b8ec03509affff0000a3e4058916e605ff36f205ff36f0051eb8d60250b8ec03509affff00'
            '00a3ec058916ee05ff36f205ff36f0051eb8dc0250b8ec03509affff0000a3e8058916ea05ff36f205ff36f0'
            '05ff36e605ff36e40531c05050b80100509affff0000ff36f205ff36f005ff36e605ff36e405ff36ee05ff36'
            'ec0531c050509affff000089c789d631db535353535250ff36ea05ff36e805b8010050b8b0205053b8e80350'
            '8d56e61e529affff00008946f28956f489d00b46f2742452ff76f28d56d61e5231c050b810005031c050509a'
            'ffff0000ff76f4ff76f29affff00001eb8e1025031c050b80600505657ff36ea05ff36e805b8010050b89040'
            '5031c050b8e803508d56e61e529affff00001eb8e7025031c050b8060050565731c0505050b850405031c050'
            'b8e803508d56e61e529affff000031c0505050505657ff36ea05ff36e805b8010050b830105031c050b8e803'
            '508d56e61e529affff0000ff36f205ff36f005ff36ee05ff36ec05ff36ea05ff36e8059affff0000ff36f205'
            'ff36f005ff36ea05ff36e8058d56c61e5231c050b8100050b8ec03509affff0000ff36f205ff36f005ff36f2'
            '05ff36f0051eb8ed0250b8ec03509affff00005250ff36ee05ff36ec0531c050509affff00008946ee8956ea'
            'ff36f205ff36f0059affff00008946ec56579affff0000e86cfcff36f205ff36f0059affff000050ff36140b'
            'ff76ec8b46ea0b46ee7403b80100508d56c61e521eb8b40550ff361c0bff36160b1eb8d405501eb8c405508d'
            '56d61e52ff36180bff361a0b09fe7405b80100eb0231c050ff76f01eb8f402501eb8dc09509affff000083c4'
            '30e9f3f05589e5b82c00e8c004535152565783ec0a1eb83803509affff000089c63d2000720f501eb8450350'
            '9affff000089d7eb0431c031ff8946f483fe2072155631c050b80700509affff000089d389c289d8eb0431d2'
            '31c08956f08946f28b46f48946ec897eee83fe207218561eb84d03509affff000085d2750985c07505b80100'
            'eb0231c050837eee007506837eec007405ff5eeceb03b8ffff5081ffffff750c817ef4ffff7505b80100eb02'
            '31c05085ff7506837ef40074123b7ef2750d8b46f43b46f07505b80100eb0231c0501eb85c03509affff0000'
            '39f07505b80100eb0231c05083fe207205b80100eb0231c0501eb86503501eb81409509affff000083c41483'
            'fe207303e9e8ef569affff0000e9dfef5589e5b86801e8ac03535152565781ec3a01c746e810001eb89a0350'
            '31c0509affff000089c6501eb85e0450b81400509affff0000569affff00001eb8af03508d96bcfe1e5231c0'
            '509affff000089c6508d56d41e52b81000509affff00008946ee85c07e0489c7eb0231ff30c08843d45630e4'
            '50b803005031c0509affff0000568d56e41e52b80300509affff00008946eac646e700569affff00001eb89a'
            '03508d9644ff1e529affff00001eb89a03508d96bcfe1e5231c0509affff000089c71eb8c4035031c0509aff'
            'ff000089c657509affff00008946f28956f4579affff0000569affff000031c0501eb8ef03501eb8e403501e'
            'b8d903501e501eb82a01508d56c41e528d56e81e529affff000089c78956ec1eb8f9035031c0509affff0000'
            '8946f0837eea0375125031c05050b80200509affff000089c6eb03beffffff76f09affff000056ff76ec57ff'
            '76f4ff76f28d9644ff1e52ff76ee8d56e41e528d56d41e521eb80f04501eb88407509affff000083c4201eb8'
            '9a03508d96bcfe1e52b80002509affff00001eb8c403508d96bcfe1e52b80002509affff00001eb8f903508d'
            '96bcfe1e52b80002509affff0000e926ee1e5890455589e51e8ed8b83800e8ec01565783ec208b460c3d1301'
            '740d3d0f0074573d02007450e9ba00ff760eb80100509affff0000ff36200b9affff0000833e1e0b047c05b8'
            '0100eb0231c0501eb83d04501eb8e807509affff000083c40aff760e31c05050b80100509affff000031c031'
            'd2e97f00eb61ff760e8d56da1e529affff000089c731f657b808005089f0d1e0d1e001f0d1e0d1e005080050'
            '89f0d1e0d1e029f0b103d3e001f0d1e0d1e0baf40501c21e521e529affff0000509affff00004683fe0d7cbf'
            'ff760e8d56da1e529affff0000eb9631c0509affff0000eb8cff760e50ff760aff7608ff76069affff00008d'
            '66fae96ff65589e5b85000e8f7005683ec2c8b760ce858eae81eede88beee82ffde818fce890f0e8d2f1e864'
            'f289f0e85af989f0e899f6b91a0031db8cda8d46d2e8f300c746d4ffffc746d6ffff8976dc31c05050b8007f'
            '509affff00008946e031c0509affff00008946e2c746e8d0028c5eea8d56d21e529affff00001eb8d002501e'
            'b8460450b8cf005031c05050b80a0050b8800250b82c015031c050505650509affff000089c650ff76049aff'
            'ff0000569affff000056b8010050b82c015031c050509affff00008d56ec1e5231c05050509affff000085c0'
            '74168d56ec1e529affff00008d56ec1e529affff0000ebd78b46f08d66fe5e5dc20a00008c16cc04c33bc473'
            '132bc4f7d83b060a0077068cd03b06cc047401c30e5ab8e916bb0100e98402537461636b204f766572666c6f'
            '7721005789c788d88ec2578ae0d1e9f3ab13c9f3aa5f89f85fc35589e5565789c68b460489df8ec189d11e91'
            '8ed80bc9f3a6740519c983d9ff1f89c85f5e5dc202005589e5565783ec0889c689d7895ef8894efa8ec12680'
            '3f00742c26807f0100742cb9ffff31dbe824028b56fa8946f48b46f8e831028946f68b4ef429f13b4ef67318'
            '31c031d28d66fc5f5e5dc3268a079889c389f0e82302ebecc45ef8268a079889fa89c389f0e8e30189c385d2'
            '750485c074ce8b4ef6c47ef889d61e968ed8f3a69f1ff6c440750789d78d7701eba889d8ebb2eb0203009aff'
            'ff00000bc0750e5033c0ba0f00e8c00258b44ccd218c06d204575606535253beea0f8936ed048c1eef04268a'
            '078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07b9ea0fbfb4052bcf32c0f3aa'
            'b81619a3e904b8ffffa3eb04b430cd21a2d4048826d5049affff0000a2d6048826d70486e0a3d80454583bc4'
            '75120f01e0a90100740ab001a2da04b003a2db041ebff50457b89000509affff0000b8f504893ef1048c1ef3'
            '04b8ff00e8c101e83bfde94f014f70656e20576174636f6d20432f432b2b31362052756e2d54696d65207379'
            '7374656d2e20436f7079726967687420286329204f70656e20576174636f6d20436f6e7472696275746f7273'
            '20323030322d323032362e20506f7274696f6e7320436f7079726967687420284329205379626173652c2049'
            '6e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484'
            'c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026c607001e8d9e7aff531eb852'
            '045031c0509affff000089f8e99a00515689c189d6e8890185c0750689c889f2eb9889d8e982005789c788d8'
            '8ec2e308f2ae75044f8cc1a98bf989f889ca5fc3515789c78ec283c9ff33c0f2aef7d14989c85f59c3515689'
            'c689d189da8ec1268a0430e439d0740f89f3268a074684c075ed31d25e59c389f089ca5e59c3c3c3535289c3'
            '833eac05007404ff16ac05ff168605baff00b81000e8a800ff168605ff16880589d8e9d2fd5289c2ff168605'
            'ff16880589d0e9c2fd5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b4702'
            '8b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee8bdfebfb40589fabbae0589fe88c839fb'
            '7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcb'
            'e89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee866fe88d5bfb40589fabbb40589fe88c839'
            'fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c'
            '007505e827ffebeee83bffebe91f8d66f85f5eeb9f803e8a0500750331c0c35250cceb0657564944454fb801'
            '0083c404c300c8000301b016020072000301d11704005b000301101804001e00030117180200050002003118'
            '0100cc1703014218040003000301741804003100030164190200010003017910060013000301a81006000b00'
            '0301d01006000b000301f91006000b000301161106000d0003013b1106001700030159110600150003016c11'
            '0600070003017f1106001400030189110600080003019911060003000301f4110200a4010301151204005f00'
            '0301271204003200030144120400320003016f120400320002019a12070007000501a112070007000301d212'
            '04002f000301fa120200a40103010b1304006000030132130400530003014313040056000301491304005100'
            '03015c130700020003016d130700050003018f130700040003019e13070005000301ab13070006000301bb13'
            '07000a000301ce13070002000301dd13040053000301e613070001000301f213070006000301f81304005100'
            '0301201405000900030132140400550003014914040054000301581404005100030185140200a40103019c14'
            '04004a000301b01404004a000301c41404004a000301fd1402000c000301061501005b02030124150200a401'
            '0301371502007d0003014d15020027000301861504005a0003018c150800210003019f15020028000301a915'
            '020006000301bd1502006b00050009160100cb1402000e160100cb1403011c160200ad000301271608005700'
            '03013c160200390003016616020029000301711602002a000301771602007c000301891602000a0003019816'
            '02006c000301a616020071000301f00604002f000301000704003100030118070200d7010301340704005800'
            '03013c070400090003014c070400060003015707040008000301640704000a000301a4070200a4010301af07'
            '040009000301b70704000700030149080200a4010301da090200a4010301b70a06000e000301d60a06001000'
            '0301930b02000d010301990b02000d010301c20b040011000301d30b040012000301130c040013000301190c'
            '040011000301320c02006b000301740c02006d000301820c0200710003018c0c020072000500b70c0100530b'
            '0200bc0c0100530b0301cf0c020039000301ee0c020029000301fd0c02000c010301090d02000c010301200d'
            '02006f000301260d02000d0103012e0d02000d010301480d04000f000301510d0400120003015d0d04005800'
            '0301660d0400130003017b0d02006e000301940d02000c010301a00d02006e000301b80d02006e000301ea0d'
            '0200a4010301f50d020035000301200e0600150003013a0e060015000301550e060007000301940e06000b00'
            '0301c00e06000b000301e20e060010000301e90e060013000301f40e060008000301440f0200a40102006f0f'
            '0100e4090500730f0100e4090301780f040033000301850f0600020003019e0f060015000301bb0f06001500'
            '0301d80f060015000301fc0f06001b0003011d10060007000301481006000b0003016e100600100003014500'
            '01005f020301a8000100ba040301c3000100c9040301e1000100c9040301f3000100bd04030101010100c804'
            '030119010100c90403012b010100bd04030139010100c804030147010100c804030150010100bb0403016001'
            '0100ba04030170010100c4040301a0010100c7040301c3010100c7040301da010100bc040301e8010100bb04'
            '0301fb010100ba04030111020100be02030122020100c20203016c02010002000301720201005e0103017802'
            '0100650003017e020100c9000301840201009101030194020200a4010200a402010008000500a80201000800'
            '0301b70201005a020301c70201005f020301ea020200a40103014e0303005000030159030300480003016303'
            '0300470003016d0303003f00030177030400240003018b030200ae010301a203030040000301b50303003b00'
            '0301c903040058000301d70303003c000301f20303004e000301050403003d00030119040200ae0103016104'
            '0200a4010301a30404002f000301b304040031000301c304050006000301e504050007000301fe0405000b00'
            '0301170505000b000301300505000b000301550505000a000301600504005a00030183050400580003019405'
            '040058000301d405050008000301e5050200d701030102060200d70103016f060200a40103018c0604002500'
            '0301ad06040005000301b606040008000301cf0604005a000000000000000000000000000000000000000000'
            '050000000000000000000000433a5c484f535457494e5c4f575359532e5741560077617665006f70656e2063'
            '64617564696f00582e57415600646576732025642025642025642025642c20736e642025642c207269666620'
            '256420256c6420256c642025642c2025730074696d652025642c206576656e74202564004f57535953005553'
            '45520074696d65722025642c20706167652025752c206865617020257525252c207461736b73202564206d69'
            '6e652025642c206d6f64756c65732025642025732c20757365722025642c2072656164202578005c005c5374'
            '72696e6746696c65496e666f5c30343039303445345c46696c654465736372697074696f6e005c5661724669'
            '6c65496e666f5c5472616e736c6174696f6e00433a5c57494e444f575300007665722025642c2066696c6520'
            '256c782c20222573222c2025732c2066696e64202564202564006b65707400656e762025642c20636f756e74'
            '2025642c20706174682025642c207265616c6c6f6320222573222025750062696f73207667612025642c2065'
            '717569702025782c206d656d2025752c20636c6f636b2025642c20637020256400646f736578742077696e20'
            '25782c2064706d692025782c2064657363202564202578253034782025642025782c20646f73202564202564'
            '2c206261642025642c206d6f7573652025640068656c6c6f007469636b004f77537973205261770d0a004f77'
            '5379734464650050524f474d414e005b43726561746547726f7570284f7753797320526177295d5b41646449'
            '74656d284e4f54455041442e4558452c52617720444445295d0047726f757073007261772064646520736572'
            '7665722025642c20657865637574652025782025642c2067726f7570732025642c207465726d696e61746520'
            '2564004f775379732047726f75700d0a0070726f676d616e20636f6e6e2025642c2065786563202564202578'
            '2c2067726f757073202564004f7753797300546f706963004974656d00706f6b6564005b72756e5d004e6f62'
            '6f6479006464652025642025642025642025642c2025732025732025732c206164762025642025642025732c'
            '2025732c206e6f6e652025642025782c2064697363202564202564004c5a455850414e442e444c4c004c5a53'
            '74617274004e6f5375636846756e6374696f6e004c5a455850414e44006c6962726172792025642c2068616e'
            '646c652025642c2070726f632025642025642c2073746172742025642c206e6f6e6520256400433a5c484f53'
            '5457494e5c4f575359532e54585f00433a5c484f535457494e5c4f575359532e54585400433a5c484f535457'
            '494e5c4f575359532e4f555400433a5c484f535457494e004f57535953322e545854004f575359532e54585f'
            '00433a5c484f535457494e5c4f57535953322e545854006c7a202225732220222573222025642c2025732c20'
            '636f707920256c642c20696e7374616c6c20256c64202564007469636b732025640053797374656d20444c4c'
            '73004f70656e20576174636f6d00535a444488f0273341540c00000007414243f0f65b43726561746547726f'
            '7570284f775379732047726f7570295d5b4164644974656d284f575359532e4558452c2253797374656d2c20'
            '444c4c7322295d5b53686f7747726f7570284f775379732047726f75702c2031295d00000000ffff00000000'
            '000000000000000c000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000dd19dd190000000000000000000000000000000000000000000000000000000000000000000000000020'
            'c216000000000000000000000000000000000000ec00340056535f56455253494f4e5f494e464f00bd04effe'
            '00000100020001000400030002000100000000003f0000000000000001000000010000000000000000000000'
            'f591f46a80000000537472696e6746696c65496e666f00006c00000030343039303445340000000018000800'
            '436f6d70616e794e616d65006d75726d333836002c00160046696c654465736372697074696f6e0057494e4e'
            '2073797374656d20444c4c7320746573740000001800080046696c6556657273696f6e00312e322e332e3400'
            '2400000056617246696c65496e666f00140004005472616e736c6174696f6e000904e404'
        )),
    'OWFONT': (
        # wcc -bt=windows -ms owfont.c; wlink @owfont.lnk; wrc -bt=windows owfont.rc owfont.exe (WINNTEST.FON beside it: mkfon.py)
        # ---- owfont.c
        #     #include <windows.h>
        #     #include <string.h>
        #
        #     /* Fonts of a raster font file (WINNTEST.FON beside it: the face "Winn
        #      * Test", 12 pixels and 24 bold, AddFontResource): each size, the
        #      * nearest when another is asked for, twice the size, italic and bold
        #      * made, underlined and struck out; the VGA glyphs (System, Fixedsys)
        #      * twice and three times as large; GetTextMetrics, GetTextFace,
        #      * EnumFonts, GetTextExtent with them; as text. SPACE: a dialog whose
        #      * template has the font (FONT 9, "Winn Test"): its controls in it, its
        #      * units the font's */
        #     static int added, faces, sizes;
        #     static char path[128];
        #
        #     int FAR PASCAL EnumProc(LOGFONT FAR *lf, TEXTMETRIC FAR *tm, int type, LPARAM lp)
        #     {
        #         if (lp)
        #             sizes++;
        #         else
        #             faces++;
        #         return 1;
        #     }
        #
        #     static void line(HDC dc, int *y, int height, int weight, BYTE italic, BYTE under, BYTE strike,
        #                      LPCSTR face, LPCSTR text)
        #     {
        #         HFONT f = CreateFont(height, 0, 0, 0, weight, italic, under, strike, 0, 0, 0, 0, 0, face);
        #         HFONT old = SelectObject(dc, f);
        #         TEXTMETRIC tm;
        #         char name[40], s[120];
        #         DWORD ext;
        #         GetTextMetrics(dc, &tm);
        #         GetTextFace(dc, sizeof(name), name);
        #         ext = GetTextExtent(dc, text, lstrlen(text));
        #         wsprintf(s, "%s: %s %d/%d/%d %d", text, (LPSTR)name, tm.tmHeight, tm.tmAscent,
        #                  tm.tmInternalLeading, LOWORD(ext));
        #         TextOut(dc, 8, *y, s, lstrlen(s));
        #         *y += tm.tmHeight + tm.tmExternalLeading + 2;
        #         SelectObject(dc, old);
        #         DeleteObject(f);
        #     }
        #
        #     /* the dialog: its template's font (FONT 9, "Winn Test") the controls',
        #      * its units that font's; what MapDialogRect and WM_GETFONT say in its title */
        #     BOOL FAR PASCAL DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         static char *items[] = { "First line", "Second", "Third", "Fourth", "Fifth", "Sixth" };
        #         int i;
        #         RECT r;
        #         char s[60];
        #         switch (msg) {
        #         case WM_INITDIALOG:
        #             for (i = 0; i < 6; i++) {
        #                 SendDlgItemMessage(h, 101, LB_ADDSTRING, 0, (LPARAM)(LPSTR)items[i]);
        #                 SendDlgItemMessage(h, 103, CB_ADDSTRING, 0, (LPARAM)(LPSTR)items[i]);
        #             }
        #             SendDlgItemMessage(h, 101, LB_SETCURSEL, 1, 0);
        #             SendDlgItemMessage(h, 103, CB_SETCURSEL, 2, 0);
        #             SetDlgItemText(h, 100, "Edited text");
        #             CheckDlgButton(h, 102, 1);
        #             SetRect(&r, 0, 0, 4, 8);
        #             MapDialogRect(h, &r);
        #             wsprintf(s, "Units %d x %d, font %d", r.right, r.bottom,
        #                      SendDlgItemMessage(h, IDOK, WM_GETFONT, 0, 0) != 0);
        #             SetWindowText(h, s);
        #             return TRUE;
        #         case WM_COMMAND:
        #             if (wp == IDOK || wp == IDCANCEL)
        #                 EndDialog(h, wp);
        #             return TRUE;
        #         }
        #         return FALSE;
        #     }
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         int y = 4;
        #         char s[80];
        #         switch (msg) {
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             SetBkMode(dc, TRANSPARENT);
        #             wsprintf(s, "added %d, faces %d, sizes %d", added, faces, sizes);
        #             TextOut(dc, 8, y, s, lstrlen(s));
        #             y += 18;
        #             line(dc, &y, -10, FW_NORMAL, 0, 0, 0, "Winn Test", "Small \xc0\xe1\xe2");
        #             line(dc, &y, 12, FW_BOLD, 0, 0, 0, "Winn Test", "Bold made");
        #             line(dc, &y, 12, FW_NORMAL, 1, 0, 0, "Winn Test", "Italic made");
        #             line(dc, &y, 20, FW_BOLD, 0, 1, 1, "Winn Test", "Ask 20");
        #             line(dc, &y, 24, FW_BOLD, 0, 0, 0, "Winn Test", "Large");
        #             line(dc, &y, 48, FW_BOLD, 0, 0, 0, "Winn Test", "x2");
        #             line(dc, &y, 32, FW_NORMAL, 0, 0, 0, "System", "VGA x2");
        #             line(dc, &y, 16, FW_NORMAL, 1, 0, 0, "Fixedsys", "Fixed italic");
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_KEYDOWN:
        #             if (wp == VK_SPACE)
        #                 DialogBox((HINSTANCE)GetWindowWord(h, GWW_HINSTANCE), "FONTDLG", h, (DLGPROC)DlgProc);
        #             return 0;
        #         case WM_DESTROY:
        #             RemoveFontResource(path);
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         HDC dc;
        #         char *p;
        #         GetModuleFileName(hInstance, path, sizeof(path));
        #         for (p = path + lstrlen(path); p > path && p[-1] != '\\'; p--)
        #             ;
        #         lstrcpy(p, "WINNTEST.FON");
        #         added = AddFontResource(path);
        #         dc = GetDC(NULL);
        #         EnumFonts(dc, NULL, (FONTENUMPROC)EnumProc, NULL);
        #         EnumFonts(dc, "Winn Test", (FONTENUMPROC)EnumProc, (LPSTR)"sizes");
        #         ReleaseDC(NULL, dc);
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwFont";
        #         RegisterClass(&wc);
        #         h = CreateWindow("OwFont", "Fonts", WS_OVERLAPPEDWINDOW, 0, 0, 640, 470, NULL, NULL,
        #                          hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owfont.rc
        #     #include <windows.h>
        #
        #     FONTDLG DIALOG 20, 20, 150, 92
        #     STYLE DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU
        #     CAPTION "A dialog in Winn Test 9"
        #     FONT 9, "Winn Test"
        #     BEGIN
        #         LTEXT "&Name:", -1, 6, 8, 30, 8
        #         EDITTEXT 100, 38, 6, 104, 12, ES_AUTOHSCROLL
        #         LISTBOX 101, 6, 24, 70, 44, WS_VSCROLL | WS_TABSTOP
        #         CONTROL "&Check it", 102, "BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, 84, 24, 60, 10
        #         COMBOBOX 103, 84, 38, 60, 40, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP
        #         DEFPUSHBUTTON "OK", IDOK, 28, 74, 40, 14
        #         PUSHBUTTON "Cancel", IDCANCEL, 82, 74, 40, 14
        #     END
        # ---- owfont.lnk
        #     system windows
        #     name owfont.exe
        #     file owfont.o
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '91000200000000000203020000040020d605010000000200020003000e004000500070007a00800013010000'
            '000001000100020891008b0600000a039100d008400dd0080a061402410ca002010005800100000000001c07'
            '61003010180000000000000007464f4e54444c47064f57464f4e54000000010005000c000003474449064b45'
            '524e454c045553455200000a6f77666f6e742e65786500000000ccebfd90909090001e5890455589e51e8ed8'
            'b80800e875058b46080b46067406ff069602eb04ff069802b801001f5d4dca0e005589e550b8ea00e8500558'
            '565781ecc40089c689d75331c0505050518a460430e4508a4606508a46085030c05050505050ff760cff760a'
            '9affff00008946f856509affff00008946fa568d5ed81e539affff000056b82800508d5eb01e539affff0000'
            '56ff7610ff760eff7610ff760e9affff0000509affff000050ff76deff76daff76d88d5eb01e53ff7610ff76'
            '0e1eb83b00508d9e38ff1e539affff000083c41856b8080050ff358d9e38ff1e531e539affff0000509affff'
            '00008b46d80346e0050200010556ff76fa9affff0000ff76f89affff00008d66fc5f5e5dc20e001e58904555'
            '89e51e8ed8b85e00e86804565783ec448b460a8b560c81fa11017503e9020181fa10017403e90e0131f6ff76'
            '0eb8650050b801045031c05089f7d1e78b9520011e529affff0000ff760eb8670050b803045031c0508bbd20'
            '011e579affff00004683fe067cc4ff760eb8650050b8070450b801005031c050509affff0000ff760eb86700'
            '50b80e0450b802005031c050509affff0000ff760eb86400501eb84e00509affff0000ff760eb8660050b801'
            '00509affff00008d56f21e5231c05050b8040050b80800509affff0000ff760e8d56f21e529affff0000ff76'
            '0eb8010050b831005031c05050509affff000009d07403b8010050ff76f8ff76f61eb85a00508d56b61e529a'
            'ffff000083c40eff760e8d56b61e529affff0000b80100eb173d010074053d020075f1ff760e509affff0000'
            'ebe631c08d66fa5f5e1f5d4dca0a001e5890455589e51e8ed8b88c00e82003565783ec728b560a8b460c8b7e'
            '0ec746f804003d0001740d3d0f00740e3d02007406e99401e95801e97a01578d56d81e529affff000089c650'
            'b80100509affff0000ff369602ff369802ff3694021eb87100508d56881e529affff000083c40e56b8080050'
            'ff76f88d56881e521e529affff0000509affff00001eb88e00501eb898005031c05050508346f812b99001bb'
            'f6ff8d56f889f0e82bfd1eb8a200501eb898005031c0505050b9bc02bb0c008d56f889f0e80efd1eb8ac0050'
            '1eb898005031c05050b8010050b99001bb0c008d56f889f0e8eefc1eb8b800501eb8980050b80100505031c0'
            '50b9bc02bb14008d56f889f0e8cefc1eb8bf00501eb898005031c0505050b9bc02bb18008d56f889f0e8b1fc'
            '1eb8c500501eb898005031c0505050b9bc02bb30008d56f889f0e894fc1eb8c800501eb8cf005031c0505050'
            'b99001bb20008d56f889f0e877fc1eb8d600501eb8e3005031c05050b8010050b99001bb10008d56f889f0e8'
            '57fc578d56d81e529affff000031c031f6eb4983fa2075f557b8faff509affff0000501eb8ec005057b8ffff'
            '50b8ffff509affff0000ebd51eb81402509affff000031c0509affff0000ebc1575052ff7608ff76069affff'
            '000089d689f2e917fe5589e5b85200e84901565783ec2c8b7e0c571eb8140250b88000509affff00001eb814'
            '02509affff0000bb140201c381fb14027609807fff5c74034bebf11e531eb8f400509affff00001eb8140250'
            '9affff0000a3940231c0509affff000089c65031c05050b8ffff50b8ffff5031c050509affff0000561eb898'
            '0050b8ffff50b8ffff501eb80101509affff000031c050569affff0000b91a0031db8cda8d46d0e8df00c746'
            'd2ffffc746d4ffff897eda31c05050b8007f509affff00008946de31c0509affff00008946e0c746e607018c'
            '5ee88d56d01e529affff00001eb80701501eb80e0150b8cf005031c0505050b8800250b8d6015031c0505057'
            '50509affff000089c650ff76049affff0000569affff00008d46ea1e5031c05050509affff000085c074168d'
            '46ea1e509affff00008d46ea1e509affff0000ebd78b46ee8d66fc5f5e5dc20a00008c162c01c33bc473132b'
            'c4f7d83b060a0077068cd03b062c017401c30e5ab8af05bb0100e9c801537461636b204f766572666c6f7721'
            '005789c788d88ec2578ae0d1e9f3ab13c9f3aa5f89f85fc3eb0203009affff00000bc0750e5033c0ba0f00e8'
            '6a0258b44ccd218c063201575606535253be9a0289364d018c1e4f01268a078804464384c075f589360a005b'
            '2bc0509affff000057579affff0000fc1e07b99a02bf14022bcf32c0f3aab82007a34901b8ffffa34b01b430'
            'cd21a23401882635019affff0000a236018826370186e0a3380154583bc475120f01e0a90100740ab001a23a'
            '01b003a23b011ebf550157b89000509affff0000b85501893e51018c1e5301b8ff00e86b01e8a5fde9f9004f'
            '70656e20576174636f6d20432f432b2b31362052756e2d54696d652073797374656d2e20436f707972696768'
            '7420286329204f70656e20576174636f6d20436f6e7472696275746f727320323030322d323032362e20506f'
            '7274696f6e7320436f7079726967687420284329205379626173652c20496e632e20313938382d323030322e'
            '00c3cb005589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c'
            '268a048ec14626880743ebe031c08ec15026c607001e8d9e7aff531eb814015031c0509affff000089f8e944'
            '00515689c189d6e8330185c0750689c889f2eb9889d8e92c0000c3c3535289c3833e0c02007404ff160c02ff'
            '16e601baff00b81000e8a800ff16e601ff16e80189d8e928fe5289c2ff16e601ff16e80189d0e918fe5589e5'
            '535289c3833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df1eff'
            '1f1febd95589e5535152565789c11ee813ffbf140289fabb0e0289fe88c839fb7316803f02750583c306ebf2'
            '3a470172f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a'
            '595b5dc35589e55351565789c11ee8bcfe88d5bf140289fabb140289fe88c839fb7316803f02750583c306eb'
            'f23a470177f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83bffeb'
            'e91f8d66f85f5eeb9f803eea0100750331c0c35250cceb0657564944454fb8010083c404c300420003017e06'
            '0200310003016e070300010003016b00010038000301750001002d000301830001005d000301920001005c00'
            '0301a40002005a000301aa0001005b000301cf000300a4010301e60002005a000301ec000100210003010001'
            '01002d00030108010100450003015d0103006500030176010300650003019401030065000301ac0103006500'
            '0301bd0103005c000301cd01030061000301e301030048000301f00103006700030105020300650003012202'
            '0300a401030132020300250003014a020300580003019f02030027000301ab02010002000301c6020300a401'
            '0301dd0202005a000301e302010021000301e303030028000301f80303008500020004040100150105000804'
            '0100150103010c0403005700030118040100880003012004030006000301300403006b000301570402003100'
            '0301610402005a00030181040200580003018b040100770003019604030042000200a204010008000500a604'
            '010008000301ae04010046000200b904010008000500bd04010008000301c604010046000301cf0403004400'
            '0500e30401005d020200e80401005d020301f6040300ad000301010501005700030116050300390003013d05'
            '030029000301480503002a0003014e0503007c0003015d0503006c0003016b05030071000301750503007200'
            '0301db0502005b0003011a0602001e00030121060300050002003b060100d60503014c060200030000000000'
            '000000000000000000000000000000000500000000000000000000004669727374206c696e65005365636f6e'
            '6400546869726400466f757274680046696674680053697874680025733a2025732025642f25642f25642025'
            '6400456469746564207465787400556e69747320256420782025642c20666f6e742025640061646465642025'
            '642c2066616365732025642c2073697a657320256400536d616c6c20c0e1e20057696e6e205465737400426f'
            '6c64206d616465004974616c6963206d6164650041736b203230004c61726765007832005647412078320053'
            '797374656d004669786564206974616c696300466978656473797300464f4e54444c470057494e4e54455354'
            '2e464f4e0073697a6573004f77466f6e7400466f6e7473004f70656e20576174636f6d0010001b0022002800'
            '2f0035000000ffff00000000000000000000000c000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000910791070000000000000000000000000000000000000000000000000000'
            '0000000000000000000000208805000000000000000000000000000000000000c000c880071400140096005c'
            '00000041206469616c6f6720696e2057696e6e2054657374203900090057696e6e205465737400060008001e'
            '000800ffff0000025082264e616d653a00002600060068000c006400800081508100000600180046002c0065'
            '000100a150830000540018003c000a006600030001508026436865636b2069740000540026003c0028006700'
            '030021508500001c004a0028000e00010001000150804f4b000052004a0028000e000200000001508043616e'
            '63656c000000'
        )),
    'OWHELP': (
        # wcc -bt=windows -ms owhelp.c; wlink @owhelp.lnk
        # ---- owhelp.c
        #     /*
        #      * owhelp.c - WinHelp from a Win16 program: the help file's [MAP]
        #      * context 2 (WINHELP.EXE started), a keyword (the running one asked),
        #      * then HELP_QUIT; what each returned.
        #      */
        #     #include <windows.h>
        #     #include <string.h>
        #
        #     static int step, r1, r2, r3;
        #     static char line[80] = "waiting";
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         switch (msg) {
        #         case WM_TIMER:
        #             step++;
        #             if (step == 1) {
        #                 r2 = WinHelp(h, "WINNTEST.HLP", HELP_KEY, (DWORD)(LPSTR)"pop-up");
        #             } else if (step == 2) {
        #                 r3 = WinHelp(h, "WINNTEST.HLP", HELP_QUIT, 0);
        #                 KillTimer(h, 1);
        #                 wsprintf(line, "winhelp context %d, key %d, quit %d", r1, r2, r3);
        #                 InvalidateRect(h, NULL, TRUE);
        #             }
        #             return 0;
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             TextOut(dc, 8, 8, line, lstrlen(line));
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_DESTROY:
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwHelp";
        #         RegisterClass(&wc);
        #         h = CreateWindow("OwHelp", "WinHelp API", WS_OVERLAPPEDWINDOW, 0, 330, 640, 70, NULL, NULL,
        #                          hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         r1 = WinHelp(h, "WINNTEST.HLP", HELP_CONTEXT, 2);
        #         SetTimer(h, 1, 1000, NULL);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owhelp.lnk
        #     system windows
        #     name owhelp.exe
        #     file owhelp.o
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '710002000000000002030200000400204802010000000200020003000e004000500050005a006000f3000000'
            '0000010000000200000000000000000381004205400d42059f03a001410cb001064f5748454c500000000100'
            '06000d00000455534552064b45524e454c0347444900000a6f7768656c702e65786500000000ccebfd909090'
            '90001e5890455589e51e8ed8b83800e8e7015683ec208b460c8b760e3d1301740d3d0f00742d3d0200742ae9'
            'bb00ff06a001a1a0013d0100751e561eb8170050b80101501eb81000509affff0000a3a401eb56eb5ae98700'
            '3d0200754c561eb8170050b802005031c050509affff0000a3a60156b80100509affff0000ff36a601ff36a4'
            '01ff36a2011eb82400501eb86800509affff000083c40e5631c05050b80100509affff000031c031d2eb4a56'
            '8d56dc1e529affff000050b8080050501eb86800501e509affff0000509affff0000568d56dc1e529affff00'
            '00ebca31c0509affff0000ebc05650ff760aff7608ff76069affff00008d66fc5e1f5d4dca0a005589e5b850'
            '00e8ed005683ec2c8b760cb91a0031db8cda8d46d2e80b01c746d4ffffc746d6ffff8976dc31c05050b8007f'
            '509affff00008946e031c0509affff00008946e2c746e848008c5eea8d56d21e529affff00001eb84800501e'
            'b84f0050b8cf005031c05050b84a0150b8800250b846005031c050505650509affff000089c650ff76049aff'
            'ff0000569affff0000561eb8170050b801005031c050b80200509affff0000a3a20156b8010050b8e8035031'
            'c050509affff00008d56ec1e5231c05050509affff000085c074168d56ec1e529affff00008d56ec1e529aff'
            'ff0000ebd78b46f08d66fe5e5dc20a008c16b800c33bc473132bc4f7d83b060a0077068cd03b06b8007401c3'
            '0e5ab82102bb0100e9c801537461636b204f766572666c6f7721005789c788d88ec2578ae0d1e9f3ab13c9f3'
            'aa5f89f85fc3eb0203009affff00000bc0750e5033c0ba0f00e86a0258b44ccd218c06be00575606535253be'
            'a8018936d9008c1edb00268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07'
            'b9a801bfa0012bcf32c0f3aab89203a3d500b8ffffa3d700b430cd21a2c0008826c1009affff0000a2c20088'
            '26c30086e0a3c40054583bc475120f01e0a90100740ab001a2c600b003a2c7001ebfe10057b89000509affff'
            '0000b8e100893edd008c1edf00b8ff00e86b01e801fee9f9004f70656e20576174636f6d20432f432b2b3136'
            '2052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20'
            '436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f707972696768742028'
            '4329205379626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df'
            '8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026'
            'c607001e8d9e7aff531eb85c005031c0509affff000089f8e94400515689c189d6e8330185c0750689c889f2'
            'eb9889d8e92c0000c3c3535289c3833e9801007404ff169801ff167201baff00b81000e8a800ff167201ff16'
            '740189d8e928fe5289c2ff167201ff16740189d0e918fe5589e5535289c3833f0075078d66fc5a5b5dc31eff'
            '171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee813ff'
            'bfa00189fabb9a0189fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d44'
            '02803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee8bcfe88'
            'd5bfa00189fabba00189fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a'
            '6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803e760100750331c0'
            'c35250cceb0657564944454fb8010083c404c3001f00030150000100ab00030172000100ab0003017f000100'
            '0c0003019a000100a4010301ab0001007d000301bc00010027000301ce0002005a000301d400030021000301'
            'df00010028000301e900010006000301fb0001006b00050029010100080002002e010100080003013c010100'
            'ad00030147010300570003015c010100390003018601010029000301910101002a000301970101007c000301'
            'ad010100ab000301c20101000a000301d10101006c000301df01010071000301e9010100720003014d020200'
            '5b0003018c0202001e0003019302010005000200ad02010048020301be02020003000301f002020031000301'
            'e0030100010000000000050000000000000000000000706f702d75700057494e4e544553542e484c50007769'
            '6e68656c7020636f6e746578742025642c206b65792025642c2071756974202564004f7748656c700057696e'
            '48656c702041504900004f70656e20576174636f6d0077616974696e67000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000000000ffff00000000000000000000000c0000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000304030400000000000000000000000000000000'
            '00000000000000000000000000000000000000000020fa010000'
        )),
    'OWDLG': (
        # wcc -bt=windows -ms owdlg.c; wlink @owdlg.lnk
        # ---- owdlg.c
        #     /*
        #      * owdlg.c - COMMDLG's Color, Font and Print dialogs and GetFileTitle from
        #      * a Win16 program: Color opened full (a colour picked, Add to Custom
        #      * Colors, OK), Font (the System font asked, Bold Italic picked, OK),
        #      * Print (PD_RETURNDEFAULT: no printer), the title of a path; what each
        #      * returned, and CommDlgExtendedError after each.
        #      */
        #     #include <windows.h>
        #     #include <commdlg.h>
        #     #include <string.h>
        #
        #     static char line1[100] = "waiting", line2[100] = "", line3[100] = "";
        #
        #     static void run(HWND h)
        #     {
        #         static COLORREF cust[16];
        #         CHOOSECOLOR cc;
        #         CHOOSEFONT cf;
        #         LOGFONT lf;
        #         PRINTDLG pd;
        #         char style[LF_FACESIZE], title[16];
        #         BOOL rc, rf, rp;
        #         DWORD ec, ef, ep;
        #         short rt;
        #         int i;
        #         for (i = 0; i < 16; i++)
        #             cust[i] = RGB(255, 255, 255);
        #         _fmemset(&cc, 0, sizeof(cc));
        #         cc.lStructSize = sizeof(cc);
        #         cc.hwndOwner = h;
        #         cc.rgbResult = RGB(0, 0, 255);
        #         cc.lpCustColors = cust;
        #         cc.Flags = CC_RGBINIT | CC_FULLOPEN;
        #         rc = ChooseColor(&cc);
        #         ec = CommDlgExtendedError();
        #         _fmemset(&lf, 0, sizeof(lf));
        #         lstrcpy(lf.lfFaceName, "System");
        #         lf.lfHeight = -13;
        #         lstrcpy(style, "Regular");
        #         _fmemset(&cf, 0, sizeof(cf));
        #         cf.lStructSize = sizeof(cf);
        #         cf.hwndOwner = h;
        #         cf.lpLogFont = &lf;
        #         cf.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT | CF_USESTYLE;
        #         cf.rgbColors = RGB(128, 0, 0);
        #         cf.lpszStyle = style;
        #         rf = ChooseFont(&cf);
        #         ef = CommDlgExtendedError();
        #         _fmemset(&pd, 0, sizeof(pd));
        #         pd.lStructSize = sizeof(pd);
        #         pd.hwndOwner = h;
        #         pd.Flags = PD_RETURNDEFAULT;
        #         rp = PrintDlg(&pd);
        #         ep = CommDlgExtendedError();
        #         rt = GetFileTitle("C:\\WINDOWS\\WIN.INI", title, sizeof(title));
        #         wsprintf(line1, "color %d %06lX err %lX, custom %06lX %06lX", rc, cc.rgbResult, ec,
        #                  cust[0], cust[1]);
        #         wsprintf(line2, "font %d err %lX: %s %d, w %d i %d u %d, %d/10 pt, %s, type %X, %06lX",
        #                  rf, ef, (LPSTR)lf.lfFaceName, lf.lfHeight, lf.lfWeight, lf.lfItalic,
        #                  lf.lfUnderline, cf.iPointSize, (LPSTR)style, cf.nFontType, cf.rgbColors);
        #         wsprintf(line3, "print %d err %lX, title %d %s", rp, ep, rt, (LPSTR)title);
        #         InvalidateRect(h, NULL, TRUE);
        #     }
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         switch (msg) {
        #         case WM_TIMER:
        #             KillTimer(h, 1);
        #             run(h);
        #             return 0;
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             TextOut(dc, 8, 8, line1, lstrlen(line1));
        #             TextOut(dc, 8, 28, line2, lstrlen(line2));
        #             TextOut(dc, 8, 48, line3, lstrlen(line3));
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_DESTROY:
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwDlg";
        #         if (!hPrev && !RegisterClass(&wc))
        #             return 1;
        #         h = CreateWindow("OwDlg", "COMMDLG", WS_OVERLAPPEDWINDOW, 0, 0, 640, 100,
        #                          NULL, NULL, hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         SetTimer(h, 1, 100, NULL);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owdlg.lnk
        #     system windows
        #     name owdlg.exe
        #     file owdlg.o
        #     library commdlg.lib
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '7a0002000000000002030200000400202804010000000200020004000d0040005000500059006100fc000000'
            '0000010000000200000000000000000385002207400d2207c304ee02410c3003054f57444c47000000010009'
            '00100015000007434f4d4d444c47064b45524e454c0455534552034744490000096f77646c672e6578650000'
            '0000ccebfd90909090005589e550b82a01e8cd0358535152565781ecf60089c631c089c3d1e3d1e3c787ee02'
            'ffffc787f002ff00403d10007ce8b9200031db8cda8d46b4e8cc03c746b4200031c08946b68976b88946bcc7'
            '46beff00c746c0ee028c5ec2c746c403008946c68d56b41e529affff00008946e89affff00008946ee8956f4'
            'b9320031db8cda8d8634ffe881038d9646ff1e521eb81000509affff0000c78634fff3ff8d56941e521eb817'
            '00509affff0000b92e0031db8cda8d8666ffe84e03c78666ff2e00c78668ff000089b66aff8d9634ff89966e'
            'ff8c9e70ffc78674ffc101c78676ff0000c78678ff8000c7867aff00008d569489568a8c5e8c8d9666ff1e52'
            '9affff00008946ec9affff00008946f08956eab9340031db8cda8d8600ffe8ea02c78600ff3400c78602ff00'
            '0089b604ffc7860cff0004c7860eff00008d9600ff1e529affff00008946f29affff00008946e689d71eb81f'
            '00508d56d41e52b81000509affff00008946e4ff36f402ff36f202ff36f002ff36ee02ff76f4ff76eeff76be'
            'ff76bcff76e81eb83200501eb8da00509affff000083c41affb67affffb678ffff768e8d56941e52ffb672ff'
            '8a863fff30e4508a863eff50ffb63cffffb634ff8d9646ff1e52ff76eaff76f0ff76ec1eb85d00501eb83e01'
            '509affff000083c4268d56d41e52ff76e457ff76e6ff76f21eb8a200501eb8a201509affff000083c4145631'
            'c05050b80100509affff00008d66f65f5e5a595b5dc31e5890455589e51e8ed8b83800e8ab01565783ec208b'
            '460c8b7e0e3d1301740d3d0f0074203d02007419e98a0057b80100509affff000089f8e8a8fd31c031d2e984'
            '00eb68578d56da1e529affff000089c650b8080050501eb8da00501e509affff0000509affff000056b80800'
            '50b81c00501eb83e01501e509affff0000509affff000056b8080050b83000501eb8a201501e509affff0000'
            '509affff0000578d56da1e529affff0000eb8f31c0509affff0000eb855750ff760aff7608ff76069affff00'
            '008d66fa5f5e1f5d4dca0a005589e5b85000e8e0005683ec2c8b760cb91a0031db8cda8d46d2e8fe00c746d4'
            'ffffc746d6ffff8976dc31c05050b8007f509affff00008946e031c0509affff00008946e2c746e8c0008c5e'
            'ea837e0a0075138d56d21e529affff000085c07505b80100eb761eb8c000501eb8c60050b8cf005031c05050'
            '50b8800250b864005031c050505650509affff000089c650ff76049affff0000569affff000056b8010050b8'
            '64005031c050509affff00008d56ec1e5231c05050509affff000085c074168d56ec1e529affff00008d56ec'
            '1e529affff0000ebd78b46f08d66fe5e5dc20a008c160602c33bc473132bc4f7d83b060a0077068cd03b0606'
            '027401c30e5ab80104bb0100e9c801537461636b204f766572666c6f7721005789c788d88ec2578ae0d1e9f3'
            'ab13c9f3aa5f89f85fc3eb0203009affff00000bc0750e5033c0ba0f00e86a0258b44ccd218c060c02575606'
            '535253be2e03893627028c1e2902268a078804464384c075f589360a005b2bc0509affff000057579affff00'
            '00fc1e07b92e03bfee022bcf32c0f3aab87205a32302b8ffffa32502b430cd21a20e0288260f029affff0000'
            'a210028826110286e0a3120254583bc475120f01e0a90100740ab001a21402b003a215021ebf2f0257b89000'
            '509affff0000b82f02893e2b028c1e2d02b8ff00e86b01e80efee9f9004f70656e20576174636f6d20432f43'
            '2b2b31362052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174'
            '636f6d20436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f7079726967'
            '687420284329205379626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec8000'
            '89c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c0'
            '8ec15026c607001e8d9e7aff531eb8ce005031c0509affff000089f8e94400515689c189d6e8330185c07506'
            '89c889f2eb9889d8e92c0000c3c3535289c3833ee602007404ff16e602ff16c002baff00b81000e8a800ff16'
            'c002ff16c20289d8e928fe5289c2ff16c002ff16c20289d0e918fe5589e5535289c3833f0075078d66fc5a5b'
            '5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c1'
            '1ee813ffbfee0289fabbe80289fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe'
            '74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11e'
            'e8bcfe88d5bfee0289fabbee0289fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39'
            'fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803ec40200'
            '750331c0c35250cceb0657564944454fb8010083c404c3002b0003017000010005000301780001001a000301'
            '9c00020058000301b100020058000301070101000f0003010f0101001a0003014a0101001400030152010100'
            '1a0003016a0101001b0003019b010300a4010301e4010300a401030105020300a4010301160203007d000301'
            '570203000c0003017002030027000301840202005a0003018a020400210003019f0202005a000301a5020400'
            '21000301ba0202005a000301c002040021000301cb02030028000301d502030006000301e70203006b000500'
            '16030100240202001b0301002402030129030300ad00030134030400570003014f030300390003017f030300'
            '290003018a0303002a000301900303007c000301a20303000a000301b10303006c000301bf03030071000301'
            'c9030300720003012d0402005b0003016c0402001e00030173040300050002008d040100280403019e040200'
            '03000301d004020031000301c005030001000000000005000000000000000000000053797374656d00526567'
            '756c617200433a5c57494e444f57535c57494e2e494e4900636f6c6f72202564202530366c58206572722025'
            '6c582c20637573746f6d202530366c58202530366c5800666f6e742025642065727220256c583a2025732025'
            '642c2077202564206920256420752025642c2025642f31302070742c2025732c20747970652025582c202530'
            '366c58007072696e742025642065727220256c582c207469746c65202564202573004f77446c6700434f4d4d'
            '444c47004f70656e20576174636f6d0077616974696e67000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000ffff00000000000000000000000c0000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '000000000000000000000000000000000000e305e30500000000000000000000000000000000000000000000'
            '00000000000000000000000000000020da030000'
        )),
    'OWMISC': (
        # wcc -bt=windows -ms owmisc.c; wlink @owmisc.lnk
        # ---- owmisc.c
        #     /*
        #      * owmisc.c - what WRITE.EXE needed: moveable local memory (the handle an
        #      * entry with the block's pointer, read as Windows' programs read it: *h),
        #      * LocalHandleDelta, KEYBOARD's VkKeyScan, MapVirtualKey, GetKeyNameText,
        #      * ToAscii, and OLECLI (a client document; nothing on the clipboard makes
        #      * an object). What each gave, in three lines.
        #      */
        #     #include <windows.h>
        #     #include <ole.h>
        #     #include <string.h>
        #
        #     UINT FAR PASCAL LocalHandleDelta(UINT);
        #
        #     static char line1[120] = "", line2[120] = "", line3[120] = "";
        #
        #     static void run(void)
        #     {
        #         HLOCAL h;
        #         char NEAR *p;
        #         UINT d0, d1, f1, f2, f3, s2, moved;
        #         char name1[16], name2[16];
        #         BYTE keys[256];
        #         DWORD out;
        #         int ta;
        #         LHCLIENTDOC doc;
        #         OLESTATUS r1, r2, r3;
        #         h = LocalAlloc(LMEM_MOVEABLE | LMEM_ZEROINIT, 10);
        #         p = LocalLock(h);
        #         f1 = LocalFlags(h);
        #         lstrcpy(p, "moveable");
        #         LocalUnlock(h);
        #         h = LocalReAlloc(h, 3000, LMEM_MOVEABLE);
        #         p = *(char NEAR * NEAR *)h;         /* as Windows' programs do */
        #         moved = p == LocalLock(h);
        #         LocalUnlock(h);
        #         s2 = LocalSize(h);
        #         LocalReAlloc(h, 0, LMEM_MOVEABLE);  /* LocalDiscard */
        #         f2 = LocalFlags(h);
        #         f3 = *(UINT NEAR *)h;               /* its pointer: none */
        #         LocalFree(h);
        #         d0 = LocalHandleDelta(0);
        #         d1 = LocalHandleDelta(d0 * 5);
        #         wsprintf(line1, "local: handle %d, flags %X, '%s' %d, size %u, discarded %X %u, delta %X %X",
        #                  (h & 3) == 2, f1, (LPSTR)p, moved, s2, f2, f3, d0, d1);
        #         GetKeyNameText(0x1e0000L, name1, sizeof(name1));
        #         GetKeyNameText(0x10000L, name2, sizeof(name2));
        #         _fmemset(keys, 0, sizeof(keys));
        #         keys[VK_SHIFT] = 0x80;
        #         out = 0;
        #         ta = ToAscii('1', 2, keys, &out, 0);
        #         wsprintf(line2, "keyboard: %X %X %X, %X %X, %s %s, %d %c",
        #                  VkKeyScan('A'), VkKeyScan('a'), VkKeyScan('?'), MapVirtualKey('A', 0),
        #                  MapVirtualKey(0x1e, 1), (LPSTR)name1, (LPSTR)name2, ta, (char)out);
        #         r1 = OleRegisterClientDoc("OwMisc", "DOC.TXT", 0, &doc);
        #         r2 = OleQueryCreateFromClip("StdFileEditing", olerender_draw, 0);
        #         r3 = OleRevokeClientDoc(doc);
        #         wsprintf(line3, "olecli: %d %d %d", r1, r2, r3);
        #     }
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         switch (msg) {
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             TextOut(dc, 8, 8, line1, lstrlen(line1));
        #             TextOut(dc, 8, 28, line2, lstrlen(line2));
        #             TextOut(dc, 8, 48, line3, lstrlen(line3));
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_DESTROY:
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwMisc";
        #         if (!hPrev && !RegisterClass(&wc))
        #             return 1;
        #         run();
        #         h = CreateWindow("OwMisc", "OWMISC", WS_OVERLAPPEDWINDOW, 0, 0, 640, 100,
        #                          NULL, NULL, hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owmisc.lnk
        #     system windows
        #     name owmisc.exe
        #     file owmisc.o
        #     library olecli.lib
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '85000200000000000203020000040020e803010000000200020005000e004000500050005a00640007010000'
            '000001000000020000000000000000038b00e206400de206dd041e03410c2003064f574d4953430000000100'
            '08000d0016001d0000064b45524e454c0455534552084b4559424f415244064f4c45434c490347444900000a'
            '6f776d6973632e65786500000000ccebfd90909090005589e5b86001e88e03535152565781ec3401b8420050'
            'b80a00509affff000089c6509affff000089c7569affff00008946f01e571eb81000509affff0000569affff'
            '000056b8b80b50b80200509affff000089c389c68b078946f4539affff00003b46f47507c746f20100eb05c7'
            '46f20000569affff0000569affff00008946ec5631c050b80200509affff0000569affff00008946ee8b0489'
            '46ea569affff000031c0509affff000089c789c3d1e3d1e301c3539affff00005057ff76eaff76eeff76ecff'
            '76f21eff76f4ff76f083e60383fe027505ba0100eb0231d2521eb81900501eb8ce00509affff000083c41cb8'
            '1e005031c0508d56c21e52b81000509affff0000b801005031c0508d56d21e52b81000509affff0000b90001'
            '31db8cda8d86c2fee8a002c686d2fe8031c08946e28946e4b8310050b80200508d86c2fe1e508d56e21e5231'
            'c0509affff00008a56e230f652508d56d21e528d56c21e52b81e0050b80100509affff000050b841005031c0'
            '509affff000050b83f00509affff000050b86100509affff000050b84100509affff0000501eb86400501eb8'
            '4601509affff000083c41e1eb89400501eb88c005031c050508d56e61e529affff000089c61eb89b0050b801'
            '005031c0509affff000089c7ff76e8ff76e69affff00005057561eb8aa00501eb8be01509affff000083c40e'
            '8d66f65f5e5a595b5dc31e5890455589e51e8ed8b83800e88301565783ec208b460c8b7e0e3d0f0074073d02'
            '00746eeb76578d56da1e529affff000089c650b8080050501eb8ce00501e509affff0000509affff000056b8'
            '080050b81c00501eb84601501e509affff0000509affff000056b8080050b83000501eb8be01501e509affff'
            '0000509affff0000578d56da1e529affff000031c031d2eb1a31c0509affff0000ebf05750ff760aff7608ff'
            '76069affff00008d66fa5f5e1f5d4dca0a005589e5b85000e8d2005683ec2c8b760cb91a0031db8cda8d46d2'
            'e8f000c746d4ffffc746d6ffff8976dc31c05050b8007f509affff00008946e031c0509affff00008946e2c7'
            '46e894008c5eea837e0a0075138d56d21e529affff000085c07505b80100eb67e8dbfc1eb89400501eb8bb00'
            '50b8cf005031c0505050b8800250b864005031c050505650509affff000089c650ff76049affff0000569aff'
            'ff00008d56ec1e5231c05050509affff000085c074168d56ec1e529affff00008d56ec1e529affff0000ebd7'
            '8b46f08d66fe5e5dc20a00008c163602c33bc473132bc4f7d83b060a0077068cd03b0636027401c30e5ab8c1'
            '03bb0100e9c801537461636b204f766572666c6f7721005789c788d88ec2578ae0d1e9f3ab13c9f3aa5f89f8'
            '5fc3eb0203009affff00000bc0750e5033c0ba0f00e86a0258b44ccd218c063c02575606535253be1e038936'
            '57028c1e5902268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07b91e03bf'
            '1e032bcf32c0f3aab83205a35302b8ffffa35502b430cd21a23e0288263f029affff0000a240028826410286'
            'e0a3420254583bc475120f01e0a90100740ab001a24402b003a245021ebf5f0257b89000509affff0000b85f'
            '02893e5b028c1e5d02b8ff00e86b01e81cfee9f9004f70656e20576174636f6d20432f432b2b31362052756e'
            '2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20436f6e74'
            '72696275746f727320323030322d323032362e20506f7274696f6e7320436f70797269676874202843292053'
            '79626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df8d9e7aff'
            '8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026c607001e'
            '8d9e7aff531eb8c2005031c0509affff000089f8e94400515689c189d6e8330185c0750689c889f2eb9889d8'
            'e92c0000c3c3535289c3833e1603007404ff161603ff16f002baff00b81000e8a800ff16f002ff16f20289d8'
            'e928fe5289c2ff16f002ff16f20289d0e918fe5589e5535289c3833f0075078d66fc5a5b5dc31eff171febf3'
            '5589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee813ffbf1e0389'
            'fabb180389fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d4402803c00'
            '7508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee8bcfe88d5bf1e03'
            '89fabb1e0389fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a6c017305'
            'c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803ef40200750331c0c35250cc'
            'eb0657564944454fb8010083c404c3003800030123000100050003012b00010008000301330001000c000301'
            '42000100580003014800010009000301560001000600030165000100080003017c0001000900030182000100'
            '0a0003019200010006000301980001000c000301a600010007000301ae00010036010301be00010036010301'
            'f2000200a40103010a010300850003011f010300850003015501030004000301730103008300030180010300'
            '830003018a0103008100030194010300810003019e01030081000301ae010200a4010301c901040029000301'
            'dc0104000a000301e90104002a000301fb010200a40103013a020200270003014e0201005a00030154020500'
            '21000301690201005a0003016f02050021000301840201005a0003018a020500210003019502020028000301'
            'a302020006000301b50202006b000500e40201000c020200e90201000c020301f7020200ad00030102030500'
            '570003011d0302003900030150030200290003015b0302002a000301610302007c000301700302006c000301'
            '7e030200710003018803020072000301ed0301005b0003012c0401001e00030133040200050002004d040100'
            'e80303015e040100030003019004010031000301800502000100000000000500000000000000000000006d6f'
            '766561626c65006c6f63616c3a2068616e646c652025642c20666c6167732025582c20272573272025642c20'
            '73697a652025752c206469736361726465642025582025752c2064656c7461202558202558006b6579626f61'
            '72643a2025582025582025582c2025582025582c2025732025732c20256420256300444f432e545854004f77'
            '4d6973630053746446696c6545646974696e67006f6c65636c693a202564202564202564004f574d49534300'
            '4f70656e20576174636f6d000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000000000000000000000ffff00000000000000000000000c0000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '000000000000000000000000000000000000000000000000000000000000a305a30500000000000000000000'
            '000000000000000000000000000000000000000000000000000000209a030000'
        )),
    'OWSND': (
        # wcc -bt=windows -ms -zW owsnd.c; wlink @owsnd.lnk
        # ---- owsnd.c
        #     /*
        #      * owsnd.c - SOUND (a scale queued, played, waited for; a tone; the voice
        #      * another task's: none) and OLESVR (a server and a document registered and
        #      * revoked, their Release methods called): what each returned.
        #      */
        #     #include <windows.h>
        #     #include <ole.h>
        #     #include <string.h>
        #
        #     static char line1[120] = "", line2[120] = "";
        #     static int server_released, doc_released;
        #
        #     OLESTATUS FAR PASCAL SrvRelease(LPOLESERVER s) { server_released++; return OLE_OK; }
        #     OLESTATUS FAR PASCAL DocRelease(LPOLESERVERDOC d) { doc_released++; return OLE_OK; }
        #     OLESTATUS FAR PASCAL Nothing(void) { return OLE_OK; }
        #
        #     static OLESERVERVTBL svtbl;
        #     static OLESERVERDOCVTBL dvtbl;
        #     static OLESERVER server;
        #     static OLESERVERDOC sdoc;
        #
        #     static void run(HINSTANCE inst)
        #     {
        #         int open, q, n1, a, t, s, n2, w, n3, e;
        #         LHSERVER hs = 0;
        #         LHSERVERDOC hd = 0;
        #         OLESTATUS r1, r2, r3, r4, r5;
        #         BOOL more = TRUE;
        #         open = OpenSound();
        #         q = SetVoiceQueueSize(1, 6 * 16);
        #         a = SetVoiceAccent(1, 240, 128, S_NORMAL, 0);
        #         n1 = SetVoiceNote(1, 37, 8, 0);         /* C D E F G, eighths */
        #         SetVoiceNote(1, 39, 8, 0);
        #         SetVoiceNote(1, 41, 8, 0);
        #         SetVoiceNote(1, 42, 8, 0);
        #         SetVoiceNote(1, 44, 8, 1);
        #         t = SetVoiceSound(1, MAKELONG(0, 440), 4);
        #         e = SetVoiceNote(1, 85, 8, 0);
        #         n2 = CountVoiceNotes(1);
        #         s = StartSound();
        #         w = WaitSoundState(S_QUEUEEMPTY);
        #         n3 = CountVoiceNotes(1);
        #         CloseSound();
        #         MessageBeep(0);
        #         wsprintf(line1, "sound: open %d, size %d, accent %d, note %d, tone %d, bad %d, "
        #                  "count %d, start %d, wait %d, count %d", open, q, a, n1, t, e, n2, s, w, n3);
        #         svtbl.Release = SrvRelease;
        #         dvtbl.Release = DocRelease;
        #         server.lpvtbl = &svtbl;
        #         sdoc.lpvtbl = &dvtbl;
        #         r1 = OleRegisterServer("OwSnd", &server, &hs, inst, OLE_SERVER_MULTI);
        #         r2 = OleRegisterServerDoc(hs, "SOME.DOC", &sdoc, &hd);
        #         r3 = OleUnblockServer(hs, &more);
        #         r4 = OleRevokeServerDoc(hd);
        #         r5 = OleRevokeServer(hs);
        #         wsprintf(line2, "olesvr: %d %d %d %d %d, more %d, released %d %d, version %lX",
        #                  r1, r2, r3, r4, r5, more, server_released, doc_released, OleQueryServerVersion());
        #     }
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         switch (msg) {
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             TextOut(dc, 8, 8, line1, lstrlen(line1));
        #             TextOut(dc, 8, 28, line2, lstrlen(line2));
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_DESTROY:
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "OwSnd";
        #         if (!hPrev && !RegisterClass(&wc))
        #             return 1;
        #         run(hInstance);
        #         h = CreateWindow("OwSnd", "OWSND", WS_OVERLAPPEDWINDOW, 0, 0, 640, 80,
        #                          NULL, NULL, hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- owsnd.lnk
        #     system windows
        #     name owsnd.exe
        #     file owsnd.o
        #     library olesvr.lib
        #     export SrvRelease
        #     export DocRelease
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '9b000a000000000002030200000400200a04010000000200020005000d0040005000500073007d0025010000'
            '0000010000000200000000000000000399000407400d0407f404aa02410c0003054f57534e4400000a535256'
            '52454c4541534501000a444f4352454c45415345020000010007000c0013001a000005534f554e4404555345'
            '52064f4c45535652064b45524e454c034744490201010800011b000000096f77736e642e657865000000cceb'
            'fd9090909000b80600e8b3035589e5ff06ee0231c05dca0400b80600e8a0035589e5ff06f002ebebb80400e8'
            '910331c0cb5589e550b84c00e8840358535152565783ec205031c08946d68946d88946da8946dcc746e40100'
            '9affff00008946eab8010050b86000509affff00008946ecb8010050b8f00050b880005031c050509affff00'
            '008946eeb8010050b8250050b808005031c0509affff00008946f0b8010050b8270050b808005031c0509aff'
            'ff0000b8010050b8290050b808005031c0509affff0000b8010050b82a0050b808005031c0509affff0000b8'
            '010050b82c0050b8080050b80100509affff0000b8010050b8b8015031c050b80400509affff00008946f2b8'
            '010050b8550050b808005031c0509affff00008946f4b80100509affff000089c69affff000089c731c0509a'
            'ffff00008946e6b80100509affff00008946e89affff000031c0509affff0000ff76e8ff76e65756ff76f4ff'
            '76f2ff76f0ff76eeff76ecff76ea1eb81000501eb8d200509affff000083c41cc706e202ffffc706e402ffff'
            'c706be02ffffc706c002ffffc706ca02ce028c1ecc02c706ea02aa028c1eec021eb87400501eb8ca02508d56'
            'd61e52ff76d431c0509affff00008946deff76d8ff76d61eb87a00501eb8ea02508d56da1e529affff000089'
            '46e0ff76d8ff76d68d56e41e529affff00008946e2ff76dcff76da9affff000089c7ff76d8ff76d69affff00'
            '0089c69affff00005250ff36f002ff36ee02ff76e45657ff76e2ff76e0ff76de1eb88300501eb84a01509aff'
            'ff000083c41c8d66f65f5e5a595b5dc3455589e5b83600e86901565783ec208b460c8b7e0e3d0f0074073d02'
            '007453eb5b578d56dc1e529affff000089c650b8080050501eb8d200501e509affff0000509affff000056b8'
            '080050b81c00501eb84a01501e509affff0000509affff0000578d56dc1e529affff000031c031d2eb1a31c0'
            '509affff0000ebf05750ff760aff7608ff76069affff00008d66fc5f5e5d4dca0a005589e5b85000e8d40056'
            '83ec2c8b760cb91a0031db8cda8d46d2e8f200c746d4ffffc746d6ffff8976dc31c05050b8007f509affff00'
            '008946e031c0509affff00008946e2c746e874008c5eea837e0a0075138d56d21e529affff000085c07505b8'
            '0100eb6989f0e8e4fc1eb87400501eb8c00050b8cf005031c0505050b8800250b850005031c050505650509a'
            'ffff000089c650ff76049affff0000569affff00008d56ec1e5231c05050509affff000085c074168d56ec1e'
            '529affff00008d56ec1e529affff0000ebd78b46f08d66fe5e5dc20a00008c16c201c33bc473132bc4f7d83b'
            '060a0077068cd03b06c2017401c30e5ab8e303bb0100e9c801537461636b204f766572666c6f7721005789c7'
            '88d88ec2578ae0d1e9f3ab13c9f3aa5f89f85fc3eb0203009affff00000bc0750e5033c0ba0f00e86a0258b4'
            '4ccd218c06c801575606535253bef2028936e3018c1ee501268a078804464384c075f589360a005b2bc0509a'
            'ffff000057579affff0000fc1e07b9f202bfaa022bcf32c0f3aab85405a3df01b8ffffa3e101b430cd21a2ca'
            '018826cb019affff0000a2cc018826cd0186e0a3ce0154583bc475120f01e0a90100740ab001a2d001b003a2'
            'd1011ebfeb0157b89000509affff0000b8eb01893ee7018c1ee901b8ff00e86b01e81afee9f9004f70656e20'
            '576174636f6d20432f432b2b31362052756e2d54696d652073797374656d2e20436f70797269676874202863'
            '29204f70656e20576174636f6d20436f6e7472696275746f727320323030322d323032362e20506f7274696f'
            '6e7320436f7079726967687420284329205379626173652c20496e632e20313938382d323030322e00c3cb00'
            '5589e551565781ec800089c689df8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048e'
            'c14626880743ebe031c08ec15026c607001e8d9e7aff531eb8c6005031c0509affff000089f8e94400515689'
            'c189d6e8330185c0750689c889f2eb9889d8e92c0000c3c3535289c3833ea202007404ff16a202ff167c02ba'
            'ff00b81000e8a800ff167c02ff167e0289d8e928fe5289c2ff167c02ff167e0289d0e918fe5589e5535289c3'
            '833f0075078d66fc5a5b5dc31eff171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd9'
            '5589e5535152565789c11ee813ffbfaa0289fabba40289fe88c839fb7316803f02750583c306ebf23a470172'
            'f689de8a4701ebef39fe74158d4402803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc3'
            '5589e55351565789c11ee8bcfe88d5bfaa0289fabbaa0289fe88c839fb7316803f02750583c306ebf23a4701'
            '77f689de8a4701ebef39fe741c3a6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66'
            'f85f5eeb9f803e800200750331c0c35250cceb0657564944454fb8010083c404c300360003015b0001000100'
            '03016b0001000300030183000100050003019a00010004000301b100010004000301c500010004000301d900'
            '010004000301ee0001000400030102010100080003011901010004000301250101000d0003012c0101000900'
            '0301360101000b000301420101000d0003014a0101000200030152010200680003017b010200a40105008601'
            '0100080002008c01010008000500920101001b000200980101001b000301c401030002000301e10103000600'
            '0301f40103000500030102020300070003010f02030003000301160203000c0003013d020200a40103017602'
            '0200270003018a0204005a0003019002050021000301a50204005a000301ab02050021000301b60202002800'
            '0301c402020006000301d60202006b000500040301004e020200090301004e02030117030200ad0003012203'
            '0500570003013d0302003900030172030200290003017d0302002a000301830302007c000301920302006c00'
            '0301a003020071000301aa030200720003010f0404005b0003014e0404001e00030155040200050002006f04'
            '01000a0403018004040003000301b204040031000301a2050200010000000000050000000000000000000000'
            '736f756e643a206f70656e2025642c2073697a652025642c20616363656e742025642c206e6f74652025642c'
            '20746f6e652025642c206261642025642c20636f756e742025642c2073746172742025642c20776169742025'
            '642c20636f756e74202564004f77536e6400534f4d452e444f43006f6c657376723a20256420256420256420'
            '25642025642c206d6f72652025642c2072656c65617365642025642025642c2076657273696f6e20256c5800'
            '4f57534e44004f70656e20576174636f6d000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000000000000000000000000000000000000000000000000000000000ffff0000'
            '0000000000000000000c00000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000c505c505000000000000000000000000000000000000000000000000000000000000000000000000'
            '0020bc030000'
        )),
    'OW31': (
        # wcc -bt=windows -ms ow31.c; wlink @ow31.lnk
        # ---- ow31.c
        #     /*
        #      * ow31.c - the rest of Windows 3.1's USER, GDI and KERNEL: the clipboard
        #      * viewers' chain, ClipCursor, a system timer, a DC's bounds, the device's
        #      * pens and brushes, GDI's objects and modes, palettes, the rasterizer, a
        #      * task's priority; what each gave, in three lines.
        #      */
        #     #include <windows.h>
        #     #include <string.h>
        #
        #     /* not in Watcom's windows.h for Windows 3.0's */
        #     UINT FAR PASCAL SetSystemTimer(HWND, UINT, UINT, FARPROC);
        #     BOOL FAR PASCAL KillSystemTimer(HWND, UINT);
        #     DWORD FAR PASCAL GetTimerResolution(void);
        #     int FAR PASCAL SetRelAbs(HDC, int);
        #     int FAR PASCAL GetRelAbs(HDC);
        #     BOOL FAR PASCAL IsGDIObject(HGDIOBJ);
        #     HFONT FAR PASCAL GetCurLogFont(HDC);
        #     int FAR PASCAL SetPriority(HTASK, int);
        #     HWND FAR PASCAL WindowFromDC(HDC);
        #
        #     static char line1[140] = "", line2[140] = "", line3[140] = "";
        #     static int draws, changes, systimers;
        #     static HWND next_viewer;
        #
        #     int FAR PASCAL CountObj(LPVOID obj, LPARAM lp)
        #     {
        #         (*(int FAR *)lp)++;
        #         return 1;
        #     }
        #
        #     static void run(HWND h, HINSTANCE inst)
        #     {
        #         RECT clip = { 10, 20, 110, 220 }, got, bounds;
        #         POINT pt;
        #         HDC dc;
        #         UINT was, b1, b2;
        #         int pens = 0, brushes = 0, rel, rel2, pri;
        #         FARPROC proc;
        #         HPALETTE pal;
        #         LOGPALETTE lp;
        #         RASTERIZER_STATUS rs;
        #         HGLOBAL mem;
        #         UINT prio[2] = { CF_BITMAP, CF_TEXT };
        #         int pf;
        #         /* the clipboard viewers' chain: a draw now, one after the clipboard changed */
        #         next_viewer = SetClipboardViewer(h);
        #         OpenClipboard(h);
        #         EmptyClipboard();
        #         mem = GlobalAlloc(GMEM_MOVEABLE, 4);
        #         lstrcpy(GlobalLock(mem), "hi");
        #         GlobalUnlock(mem);
        #         SetClipboardData(CF_TEXT, mem);
        #         CloseClipboard();
        #         pf = GetPriorityClipboardFormat(prio, 2);
        #         ChangeClipboardChain(h, next_viewer);
        #         /* the cursor kept in a rectangle, moved out of it */
        #         ClipCursor(&clip);
        #         SetCursorPos(300, 300);
        #         GetCursorPos(&pt);
        #         GetClipCursor(&got);
        #         ClipCursor(NULL);
        #         wsprintf(line1, "clipboard: draws %d, viewer %d, format %d; cursor %d,%d in %d,%d,%d,%d; "
        #                  "timer res %ld", draws, next_viewer != 0, pf, pt.x, pt.y, got.left, got.top,
        #                  got.right, got.bottom, GetTimerResolution());
        #         /* a DC's bounds, its modes, the device's objects */
        #         dc = GetDC(h);
        #         was = SetBoundsRect(dc, NULL, DCB_ENABLE | DCB_RESET);
        #         Rectangle(dc, 5, 6, 25, 16);
        #         b1 = GetBoundsRect(dc, &bounds, DCB_RESET);
        #         b2 = GetBoundsRect(dc, NULL, 0);
        #         SetBoundsRect(dc, NULL, DCB_DISABLE);
        #         proc = MakeProcInstance((FARPROC)CountObj, inst);
        #         EnumObjects(dc, OBJ_PEN, proc, (LPSTR)&pens);
        #         EnumObjects(dc, OBJ_BRUSH, proc, (LPSTR)&brushes);
        #         rel = SetRelAbs(dc, RELATIVE);
        #         rel2 = GetRelAbs(dc);
        #         wsprintf(line2, "dc: was %X, bounds %X %d,%d,%d,%d then %X; pens %d brushes %d; "
        #                  "relabs %d %d; window %d; font %d", was, b1, bounds.left, bounds.top,
        #                  bounds.right, bounds.bottom, b2, pens, brushes, rel, rel2,
        #                  WindowFromDC(dc) == h, GetCurLogFont(dc) != 0);
        #         ReleaseDC(h, dc);
        #         /* GDI's objects, a palette resized, the rasterizer, a priority */
        #         lp.palVersion = 0x300;
        #         lp.palNumEntries = 1;
        #         lp.palPalEntry[0].peRed = 1;
        #         lp.palPalEntry[0].peGreen = 2;
        #         lp.palPalEntry[0].peBlue = 3;
        #         lp.palPalEntry[0].peFlags = 0;
        #         pal = CreatePalette(&lp);
        #         ResizePalette(pal, 5);
        #         _fmemset(&rs, 0xff, sizeof(rs));
        #         GetRasterizerCaps(&rs, sizeof(rs));
        #         pri = SetPriority(GetCurrentTask(), 3);
        #         SetSystemTimer(h, 7, 50, NULL);
        #         wsprintf(line3, "gdi: pen %d, pal %d, none %d; palette %d; rasterizer %d %d; priority %d; "
        #                  "font data %ld", IsGDIObject(GetStockObject(BLACK_PEN)) != 0,
        #                  IsGDIObject(pal) != 0, IsGDIObject((HGDIOBJ)0x1234), GetPaletteEntries(pal, 0, 9, NULL),
        #                  rs.nSize, rs.wFlags, pri, GetFontData(0, 0, 0, NULL, 0));
        #         DeleteObject(pal);
        #     }
        #
        #     long FAR PASCAL WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
        #     {
        #         PAINTSTRUCT ps;
        #         HDC dc;
        #         char t[40];
        #         switch (msg) {
        #         case WM_DRAWCLIPBOARD:
        #             draws++;
        #             return 0;
        #         case WM_CHANGECBCHAIN:
        #             changes++;
        #             return 0;
        #         case 0x118:                         /* WM_SYSTIMER */
        #             if (wp == 7 && ++systimers == 2)
        #                 KillSystemTimer(h, 7);
        #             InvalidateRect(h, NULL, TRUE);
        #             return 0;
        #         case WM_PAINT:
        #             dc = BeginPaint(h, &ps);
        #             TextOut(dc, 8, 8, line1, lstrlen(line1));
        #             TextOut(dc, 8, 28, line2, lstrlen(line2));
        #             TextOut(dc, 8, 48, line3, lstrlen(line3));
        #             wsprintf(t, "system timers %d", systimers);
        #             TextOut(dc, 8, 68, t, lstrlen(t));
        #             EndPaint(h, &ps);
        #             return 0;
        #         case WM_DESTROY:
        #             PostQuitMessage(0);
        #             return 0;
        #         }
        #         return DefWindowProc(h, msg, wp, lp);
        #     }
        #
        #     int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
        #     {
        #         WNDCLASS wc;
        #         HWND h;
        #         MSG m;
        #         _fmemset(&wc, 0, sizeof(wc));
        #         wc.lpfnWndProc = WndProc;
        #         wc.hInstance = hInstance;
        #         wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        #         wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        #         wc.lpszClassName = "Ow31";
        #         if (!hPrev && !RegisterClass(&wc))
        #             return 1;
        #         h = CreateWindow("Ow31", "OW31", WS_OVERLAPPEDWINDOW, 0, 0, 640, 120,
        #                          NULL, NULL, hInstance, NULL);
        #         ShowWindow(h, show);
        #         UpdateWindow(h);
        #         run(h, hInstance);
        #         InvalidateRect(h, NULL, TRUE);
        #         while (GetMessage(&m, NULL, 0, 0)) {
        #             TranslateMessage(&m);
        #             DispatchMessage(&m);
        #         }
        #         return m.wParam;
        #     }
        # ---- ow31.lnk
        #     system windows
        #     name ow31.exe
        #     file ow31.o
        #     export CountObj
        #     import SETSYSTEMTIMER USER.11
        #     import KILLSYSTEMTIMER USER.182
        #     import WINDOWFROMDC USER.117
        bytes.fromhex(
            '4d5a80000100000004000000ffff0000b8000000000000004000000000000000000000000000000000000000'
            '00000000000000000000000000000000800000000e1fba0e00b409cd21b8014ccd2154686973206973206120'
            '57696e646f77732031362d6269742065786563757461626c650d0a240000000000000000000000004e450501'
            '7a000700000000000203020000040020ac05010000000200020003000c004000500050006300690001010000'
            '000001000000020000000000000000038700a608400da6082706e003410cf003044f573331000008434f554e'
            '544f424a010000010006000d00000455534552064b45524e454c0347444901010108000000086f7733312e65'
            '786500000000ccebfd90909090001e5890455589e51e8ed8b80800e84b058b5e068e460826ff07b801001f5d'
            '4dca08005589e550b87200e82f05585351565783ec4050528cd88ec08d7eb8beec02a5a5a5a531c08946ea89'
            '46e6a1f4028946dea1f6028946e0ff76b69affff0000a3e203ff76b69affff00009affff0000b802005031c0'
            '50b80400509affff000089c6509affff000052501eb81000509affff0000569affff0000b8010050569affff'
            '00009affff00008d56de1e52b80200509affff000089c6ff76b6ff36e2039affff00008d56b81e529affff00'
            '00b82c0150509affff00008d56e21e529affff00008d56c01e529affff000031c050509affff00009affff00'
            '005250ff76c6ff76c4ff76c2ff76c0ff76e4ff76e256a1e20385c07403b8010050ff36e0031eb81300501eb8'
            '4801509affff000083c41eff76b69affff000089c68946f65031c05050b80500509affff00008946ec56b805'
            '0050b8060050b8190050b81000509affff0000568d56c81e52b80100509affff00008946f25631c05050509a'
            'ffff00008946f45631c05050b80800509affff0000b8ffff50b8ffff50ff76b49affff000089c78956e856b8'
            '01005052578d56ea1e529affff000056b8020050ff76e8578d56e61e529affff000056b80200509affff0000'
            '89c7569affff00008946ee569affff000085c07403b8010050ff76f69affff00003b46b67505b80100eb0231'
            'c050ff76ee57ff76e6ff76eaff76f4ff76ceff76ccff76caff76c8ff76f2ff76ec1eb86900501eb8d401509a'
            'ffff000083c422ff76b6ff76f69affff0000c746d00003c746d20100c746d40102c746d603008d56d01e529a'
            'ffff000089c68946f050b80500509affff0000b90600bbff008cda8d46d8e812038d56d81e52b80600509aff'
            'ff00009affff000050b80300509affff000089c7ff76b6b8070050b832005031c050509affff000031c05050'
            '505050505050509affff0000525057ff76daff76d85631c050b809005031c050509affff000050b83412509a'
            'ffff000050569affff000085c07403b8010050b80700509affff0000509affff000085c07403b80100501eb8'
            'c900501eb86002509affff000083c41aff76f09affff00008d66f85f5e595b5dc31e5890455589e51e8ed8b8'
            '6000e82002565783ec488b560a8b460c8b7e0e3d1801720f76303d0d0374203d08037410e9f7003d0f00744d'
            '3d02007445e9ea0031c031d2ff06e003e9ed0031c031d2ff06e603e9e20083fa077512ff06e403833ee40302'
            '750757529affff00005731c05050b80100509affff000031c031d2e9b600e99a00578d56da1e529affff0000'
            '89c650b8080050501eb84801501e509affff0000509affff000056b8080050b81c00501eb8d401501e509aff'
            'ff0000509affff000056b8080050b83000501eb86002501e509affff0000509affff0000ff36e4031eb82001'
            '508d56b21e529affff000083c40a56b8080050b84400508d56b21e521e529affff0000509affff0000578d56'
            'da1e529affff0000e95cff31c0509affff0000e951ff575052ff7608ff76069affff00008d66fa5f5e1f5d4d'
            'ca0a005589e5b85200e8e500565783ec2c8b7e0cb91a0031db8cda8d46d0e80201c746d2ffffc746d4ffff89'
            '7eda31c05050b8007f509affff00008946de31c0509affff00008946e0c746e631018c5ee8837e0a0075138d'
            '56d01e529affff000085c07505b80100eb791eb83101501eb8360150b8cf005031c0505050b8800250b87800'
            '5031c050505750509affff000089c650ff76049affff0000569affff000089fa89f0e80ffb5631c05050b801'
            '00509affff00008d56ea1e5231c05050509affff000085c074168d56ea1e529affff00008d56ea1e529affff'
            '0000ebd78b46ee8d66fc5f5e5dc20a008c16f802c33bc473132bc4f7d83b060a0077068cd03b06f8027401c3'
            '0e5ab88505bb0100e9c801537461636b204f766572666c6f7721005789c788d88ec2578ae0d1e9f3ab13c9f3'
            'aa5f89f85fc3eb0203009affff00000bc0750e5033c0ba0f00e86a0258b44ccd218c06fe02575606535253be'
            'e803893619038c1e1b03268a078804464384c075f589360a005b2bc0509affff000057579affff0000fc1e07'
            'b9e803bfe0032bcf32c0f3aab8f606a31503b8ffffa31703b430cd21a20003882601039affff0000a2020388'
            '26030386e0a3040354583bc475120f01e0a90100740ab001a20603b003a207031ebf210357b89000509affff'
            '0000b82103893e1d038c1e1f03b8ff00e86b01e809fee9f9004f70656e20576174636f6d20432f432b2b3136'
            '2052756e2d54696d652073797374656d2e20436f7079726967687420286329204f70656e20576174636f6d20'
            '436f6e7472696275746f727320323030322d323032362e20506f7274696f6e7320436f707972696768742028'
            '4329205379626173652c20496e632e20313938382d323030322e00c3cb005589e551565781ec800089c689df'
            '8d9e7aff8cd98ec2268a0484c074173c0d74138d46f939c3730c268a048ec14626880743ebe031c08ec15026'
            'c607001e8d9e7aff531eb83c015031c0509affff000089f8e94400515689c189d6e8330185c0750689c889f2'
            'eb9889d8e92c0000c3c3535289c3833ed803007404ff16d803ff16b203baff00b81000e8a800ff16b203ff16'
            'b40389d8e928fe5289c2ff16b203ff16b40389d0e918fe5589e5535289c3833f0075078d66fc5a5b5dc31eff'
            '171febf35589e5535289c38b47028b1785c0750485d274df1eff1f1febd95589e5535152565789c11ee813ff'
            'bfe00389fabbda0389fe88c839fb7316803f02750583c306ebf23a470172f689de8a4701ebef39fe74158d44'
            '02803c007508e88affc60402ebcbe89bffebf61f8d66f65f5e5a595b5dc35589e55351565789c11ee8bcfe88'
            'd5bfe00389fabbe00389fe88c839fb7316803f02750583c306ebf23a470177f689de8a4701ebef39fe741c3a'
            '6c017305c60402ebd18d4402803c007505e827ffebeee83bffebe91f8d66f85f5eeb9f803eb60300750331c0'
            'c35250cceb0657564944454fb8010083c404c3005300050096040100330302009b04010033030301a9040100'
            'ad000301b404030057000301cf04010039000301ff040100290003010a0501002a000301100501007c000301'
            '250501007d000301340501006c00030142050100710003014c05010072000301b10502005b000301f0050200'
            '1e000301f70501000500020011060100ac050301220602000300030154060200310003014407010001000301'
            '64000100930003016f00010089000301740001008b000301840002000f0003018c0002001200030198000200'
            '580003019e00020013000301a80001008d000301ad0001008a000301bb00010092010301c900010095000301'
            'd300010010000301dd00010046000301e700010011000301f100010035010301fa00010010000301ff000100'
            '0e00030132010100a40103013d0101004200030150010300c1000301690103001b00030178010300c2000301'
            '86010300c200030197010300c10002009c01010008000500a001010008000301a701020033000301bd010300'
            '47000301d001030047000301da01030005000301e201030056000301eb0103009b010301fb01010075000301'
            '36020100a40103014402010044000301620203006801030171020300700103018d0203003901030192020200'
            '240003019c02020020000301b20201000b000301c202030037010301dc0203006b010301e6020300ce010301'
            'ed020300ce010301fe0203005700030104030300ce0103011b030100a401030126030300450003019b030100'
            'b6000301a90301007d000301be03010027000301d20302005a000301d803030021000301ed0302005a000301'
            'f303030021000301080402005a0003010e0403002100030121040100a4010301390402005a0003013f040300'
            '210003014a040100280003015504010006000301660401006b00000000000500000000000000000000006869'
            '00636c6970626f6172643a2064726177732025642c207669657765722025642c20666f726d61742025643b20'
            '637572736f722025642c256420696e2025642c25642c25642c25643b2074696d65722072657320256c640064'
            '633a207761732025582c20626f756e64732025582025642c25642c25642c2564207468656e2025583b207065'
            '6e7320256420627275736865732025643b2072656c6162732025642025643b2077696e646f772025643b2066'
            '6f6e74202564006764693a2070656e2025642c2070616c2025642c206e6f6e652025643b2070616c65747465'
            '2025643b20726173746572697a65722025642025643b207072696f726974792025643b20666f6e7420646174'
            '6120256c640073797374656d2074696d657273202564004f773331004f57333100004f70656e20576174636f'
            '6d00000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '00000000000000000000000000000000000000000000000000000a0014006e00dc00020001000000ffff0000'
            '0000000000000000000c00000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
            '0000000067076707000000000000000000000000000000000000000000000000000000000000000000000000'
            '00205e050000'
        )),
}

if len(sys.argv) != 3 or sys.argv[1] not in PROGRAMS:
    raise SystemExit('usage: mkne_watcom.py ' + '|'.join(PROGRAMS) + ' OUTPUT.EXE')
with open(sys.argv[2], 'wb') as f:
    f.write(PROGRAMS[sys.argv[1]])
