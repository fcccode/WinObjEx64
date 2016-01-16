/*******************************************************************************
*
*  (C) COPYRIGHT AUTHORS, 2015
*
*  TITLE:       EXTRASSSDT.C
*
*  VERSION:     1.33
*
*  DATE:        01 Dec 2015
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
* TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
* PARTICULAR PURPOSE.
*
*******************************************************************************/

#include "global.h"
#include "extras.h"
#include "extrasSSDT.h"

EXTRASCONTEXT SdtDlgContext;
PSERVICETABLEENTRY g_SdtTable;
ULONG g_cSdtTable;
PVOID g_NtdllModule;

/*
* SdtResize
*
* Purpose:
*
* Dialog resize handler.
*
*/
VOID SdtResize(
	HWND hwndDlg
	)
{
	RECT r1;
	HWND hwnd;
	RtlSecureZeroMemory(&r1, sizeof(r1));

	hwnd = GetDlgItem(hwndDlg, ID_EXTRASLIST);
	GetClientRect(hwndDlg, &r1);

	SetWindowPos(hwnd, 0, 0, 0,
		r1.right - 16,
		r1.bottom - 16,
		SWP_NOMOVE | SWP_NOZORDER);
}

/*
* SdtDlgCompareFunc
*
* Purpose:
*
* KiServiceTable Dialog listview comparer function.
*
*/
INT CALLBACK SdtDlgCompareFunc(
	_In_ LPARAM lParam1,
	_In_ LPARAM lParam2,
	_In_ LPARAM lParamSort
	)
{
	LPWSTR lpItem1, lpItem2;
	INT nResult = 0;
	ULONG id1, id2;
	ULONG_PTR ad1, ad2;

	lpItem1 = supGetItemText(SdtDlgContext.ListView, (INT)lParam1, (INT)lParamSort, NULL);
	lpItem2 = supGetItemText(SdtDlgContext.ListView, (INT)lParam2, (INT)lParamSort, NULL);

	if ((lpItem1 == NULL) && (lpItem2 == NULL)) {
		nResult = 0;
		goto Done;
	}

	if ((lpItem1 == NULL) && (lpItem2 != NULL)) {
		nResult = (SdtDlgContext.bInverseSort) ? 1 : -1;
		goto Done;
	}
	if ((lpItem2 == NULL) && (lpItem1 != NULL)) {
		nResult = (SdtDlgContext.bInverseSort) ? -1 : 1;
		goto Done;
	}

	switch (lParamSort) {

	//sort Index
	case 0:
		id1 = strtoul(lpItem1);
		id2 = strtoul(lpItem2);

		if (SdtDlgContext.bInverseSort)
			nResult = id1 < id2;
		else
			nResult = id1 > id2;

		break;

	//sort Address
	case 2:
		
		ad1 = hextou64(&lpItem1[2]);
		ad2 = hextou64(&lpItem2[2]);

		if (SdtDlgContext.bInverseSort)
			nResult = ad1 < ad2;
		else
			nResult = ad1 > ad2;

		break;

	//sort Name, Module
	case 1:
	case 3:
	default:
		if (SdtDlgContext.bInverseSort)
			nResult = _strcmpi(lpItem2, lpItem1);
		else
			nResult = _strcmpi(lpItem1, lpItem2);
		break;
	}




Done:
	if (lpItem1) {
		HeapFree(GetProcessHeap(), 0, lpItem1);
	}
	if (lpItem2) {
		HeapFree(GetProcessHeap(), 0, lpItem2);
	}
	return nResult;
}

/*
* SdtDlgHandleNotify
*
* Purpose:
*
* WM_NOTIFY processing for SdtDlg listview.
*
*/
VOID SdtDlgHandleNotify(
	LPNMLISTVIEW	nhdr
	)
{
	LVCOLUMNW		col;
	INT				c, k;

	if (nhdr == NULL)
		return;

	if (nhdr->hdr.idFrom != ID_EXTRASLIST)
		return;

	switch (nhdr->hdr.code) {

	case LVN_COLUMNCLICK:

		SdtDlgContext.bInverseSort = !SdtDlgContext.bInverseSort;
		SdtDlgContext.lvColumnToSort = ((NMLISTVIEW *)nhdr)->iSubItem;
		ListView_SortItemsEx(SdtDlgContext.ListView, &SdtDlgCompareFunc, SdtDlgContext.lvColumnToSort);

		RtlSecureZeroMemory(&col, sizeof(col));
		col.mask = LVCF_IMAGE;
		col.iImage = -1;

		for (c = 0; c < SdtDlgContext.lvColumnCount; c++)
			ListView_SetColumn(SdtDlgContext.ListView, c, &col);

		k = ImageList_GetImageCount(ListViewImages);
		if (SdtDlgContext.bInverseSort)
			col.iImage = k - 2;
		else
			col.iImage = k - 1;

		ListView_SetColumn(SdtDlgContext.ListView, ((NMLISTVIEW *)nhdr)->iSubItem, &col);
		break;

	default:
		break;
	}
}

/*
* SdtHandlePopupMenu
*
* Purpose:
*
* Table list popup construction
*
*/
VOID SdtHandlePopupMenu(
	_In_ HWND hwndDlg
	)
{
	POINT pt1;
	HMENU hMenu;

	if (GetCursorPos(&pt1) == FALSE)
		return;

	hMenu = CreatePopupMenu();
	if (hMenu == NULL)
		return;

	InsertMenu(hMenu, 0, MF_BYCOMMAND, ID_OBJECT_COPY, T_SAVETOFILE);

	TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt1.x, pt1.y, 0, hwndDlg, NULL);
	DestroyMenu(hMenu);
}

WCHAR output[0x2000];

/*
* SdtSaveListToFile
*
* Purpose:
*
* Dump table to the selected file
*
*/
VOID SdtSaveListToFile(
	_In_ HWND hwndDlg
	)
{
	
	WCHAR ch;
	INT BufferSize = 0;
	INT	numitems;
	INT	row, subitem;
	SIZE_T sz, k;
	LPWSTR pItem = NULL;
	HCURSOR hSaveCursor;
	HCURSOR hHourGlass;
	WCHAR szTempBuffer[MAX_PATH + 1];

	RtlSecureZeroMemory(szTempBuffer, sizeof(szTempBuffer));

	_strcpy(szTempBuffer, TEXT("list.txt"));
	if (supSaveDialogExecute(hwndDlg, (LPWSTR)&szTempBuffer, TEXT("Text files\0*.txt\0\0"))) {

		hHourGlass = LoadCursorW(NULL, IDC_WAIT);

		ch = (WCHAR)0xFEFF;
		supWriteBufferToFile(szTempBuffer, &ch, sizeof(WCHAR), FALSE, FALSE);

		SetCapture(hwndDlg);
		hSaveCursor = SetCursor(hHourGlass);

		numitems = ListView_GetItemCount(SdtDlgContext.ListView);
		for (row = 0; row < numitems; row++) {

			output[0] = 0;
			for (subitem = 0; subitem < SdtDlgContext.lvColumnCount; subitem++) {

				sz = 0;
				pItem = supGetItemText(SdtDlgContext.ListView, row, subitem, &sz);
				if (pItem) {
					_strcat(output, pItem);
					HeapFree(GetProcessHeap(), 0, pItem);
				}
				if (subitem == 1) {
					for (k = 54; k > sz / sizeof(WCHAR); k--) {
						_strcat(output, TEXT(" "));
					}
				}
				else {
					_strcat(output, TEXT("\t"));
				}
			}
			_strcat(output, L"\r\n");
			BufferSize = (INT)_strlen(output);
			supWriteBufferToFile(szTempBuffer, output, BufferSize * sizeof(WCHAR), FALSE, TRUE);
		}

		SetCursor(hSaveCursor);
		ReleaseCapture();
	}
}

/*
* SdtDialogProc
*
* Purpose:
*
* KiServiceTable Dialog window procedure.
*
*/
INT_PTR CALLBACK SdtDialogProc(
	_In_  HWND hwndDlg,
	_In_  UINT uMsg,
	_In_  WPARAM wParam,
	_In_  LPARAM lParam
	)
{
	LPNMLISTVIEW nhdr = (LPNMLISTVIEW)lParam;

	switch (uMsg) {

	case WM_INITDIALOG:
		supCenterWindow(hwndDlg);
		break;

	case WM_GETMINMAXINFO:
		if (lParam) {
			((PMINMAXINFO)lParam)->ptMinTrackSize.x = 640;
			((PMINMAXINFO)lParam)->ptMinTrackSize.y = 480;
		}
		break;

	case WM_NOTIFY:
		SdtDlgHandleNotify(nhdr);
		break;

	case WM_SIZE:
		SdtResize(hwndDlg);
		break;

	case WM_CLOSE:
		DestroyWindow(hwndDlg);
		g_wobjDialogs[WOBJ_SSDTDLG_IDX] = NULL;
		return TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL) {
			SendMessage(hwndDlg, WM_CLOSE, 0, 0);
			return TRUE;
		}
		if (LOWORD(wParam) == ID_OBJECT_COPY) {
			SdtSaveListToFile(hwndDlg);
			return TRUE;
		}
		break;

	case WM_CONTEXTMENU:
		SdtHandlePopupMenu(hwndDlg);
		break;
	}

	return FALSE;
}

/*
* SdtListTable
*
* Purpose:
*
* KiServiceTable query and list routine.
*
*/
VOID SdtListTable(
	VOID
	)
{
	BOOL                    cond = FALSE;
	PUTable                 Dump = NULL;
	PRTL_PROCESS_MODULES    pModules = NULL;
	PVOID                   Module = NULL; 
	PIMAGE_EXPORT_DIRECTORY pexp = NULL;
	PIMAGE_NT_HEADERS       NtHeaders = NULL;
	DWORD                   ETableVA;
	PDWORD                  names, functions;
	PWORD                   ordinals;
	LVITEMW                 lvitem;
	WCHAR                   szBuffer[MAX_PATH + 1];

	char *name;
	void *addr;
	ULONG number, i;
	INT index;

	__try {

		do {
			pModules = (PRTL_PROCESS_MODULES)supGetSystemInfo(SystemModuleInformation);
			if (pModules == NULL)
				break;

			//if table empty, dump and prepare table
			if (g_SdtTable == NULL) {

				if (g_NtdllModule == NULL) {
					Module = GetModuleHandle(TEXT("ntdll.dll"));
				}
				else {
					Module = g_NtdllModule;
				}

				if (Module == NULL)
					break;

				g_SdtTable = (PSERVICETABLEENTRY)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
					sizeof(SERVICETABLEENTRY) * g_kdctx.KiServiceLimit);

				if (g_SdtTable == NULL)
					break;

				if (!supDumpSyscallTableConverted(&g_kdctx, &Dump))
					break;

				NtHeaders = RtlImageNtHeader(Module);
				if (NtHeaders == NULL)
					break;

				ETableVA = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
				pexp = (PIMAGE_EXPORT_DIRECTORY)((PBYTE)Module + ETableVA);
				names = (PDWORD)((PBYTE)Module + pexp->AddressOfNames),
					functions = (PDWORD)((PBYTE)Module + pexp->AddressOfFunctions);
				ordinals = (PWORD)((PBYTE)Module + pexp->AddressOfNameOrdinals);

				//walk for Nt stubs
				g_cSdtTable = 0;
				for (i = 0; i < pexp->NumberOfNames; i++) {

					name = ((CHAR *)Module + names[i]);
					addr = (PVOID *)((CHAR *)Module + functions[ordinals[i]]);

					if (*(USHORT*)name == 'tN') {

						number = *(ULONG*)((UCHAR*)addr + 4);

						if (number < g_kdctx.KiServiceLimit) {
							MultiByteToWideChar(CP_ACP, 0, name, (INT)_strlen_a(name),
								g_SdtTable[g_cSdtTable].Name, MAX_PATH);

							g_SdtTable[g_cSdtTable].ServiceId = number;
							g_SdtTable[g_cSdtTable].Address = Dump[number];
							g_cSdtTable++;
						}
					}//tN
				}//for
				HeapFree(GetProcessHeap(), 0, Dump);
				Dump = NULL;
			}

			//list table
			for (i = 0; i < g_cSdtTable; i++) {

				//ServiceId
				RtlSecureZeroMemory(&lvitem, sizeof(lvitem));
				lvitem.mask = LVIF_TEXT | LVIF_IMAGE;
				lvitem.iSubItem = 0;
				lvitem.iItem = MAXINT;
				lvitem.iImage = 0;
				RtlSecureZeroMemory(szBuffer, sizeof(szBuffer));
				wsprintf(szBuffer, L"%d", g_SdtTable[i].ServiceId);
				lvitem.pszText = szBuffer;
				index = ListView_InsertItem(SdtDlgContext.ListView, &lvitem);

				//Name
				lvitem.mask = LVIF_TEXT;
				lvitem.iSubItem = 1;
				lvitem.pszText = (LPWSTR)g_SdtTable[i].Name;
				lvitem.iItem = index;
				ListView_SetItem(SdtDlgContext.ListView, &lvitem);

				//Address
				lvitem.mask = LVIF_TEXT;
				lvitem.iSubItem = 2;
				RtlSecureZeroMemory(szBuffer, sizeof(szBuffer));
				wsprintf(szBuffer, L"0x%p", (PVOID)g_SdtTable[i].Address);
				lvitem.pszText = szBuffer;
				lvitem.iItem = index;
				ListView_SetItem(SdtDlgContext.ListView, &lvitem);

				//Module
				lvitem.mask = LVIF_TEXT;
				lvitem.iSubItem = 3;
				RtlSecureZeroMemory(szBuffer, sizeof(szBuffer));

				number = supFindModuleEntryByAddress(pModules, (PVOID)g_SdtTable[i].Address);
				if (number == (ULONG)-1) {
					_strcpy(szBuffer, TEXT("Unknown Module"));
				}
				else {

					MultiByteToWideChar(CP_ACP, 0,
						(LPCSTR)&pModules->Modules[number].FullPathName,
						(INT)_strlen_a((char*)pModules->Modules[number].FullPathName),
						szBuffer,
						MAX_PATH);
				}

				lvitem.pszText = szBuffer;
				lvitem.iItem = index;
				ListView_SetItem(SdtDlgContext.ListView, &lvitem);
			}

		} while (cond);
	}

	__except (exceptFilter(GetExceptionCode(), GetExceptionInformation())) {
		return;
	}

	if (pModules) {
		HeapFree(GetProcessHeap(), 0, pModules);
	}

	if (Dump) {
		HeapFree(GetProcessHeap(), 0, Dump);
	}
}

/*
* extrasCreateSSDTDialog
*
* Purpose:
*
* Create and initialize KiServiceTable Dialog.
*
*/
VOID extrasCreateSSDTDialog(
	_In_ HWND hwndParent
	)
{
	LVCOLUMNW   col;
	HANDLE      hIcon;

	//allow only one dialog
	if (g_wobjDialogs[WOBJ_SSDTDLG_IDX]) {
		if (IsIconic(g_wobjDialogs[WOBJ_SSDTDLG_IDX]))
			ShowWindow(g_wobjDialogs[WOBJ_SSDTDLG_IDX], SW_RESTORE);
		else
			SetActiveWindow(g_wobjDialogs[WOBJ_SSDTDLG_IDX]);
		return;
	}

	RtlSecureZeroMemory(&SdtDlgContext, sizeof(SdtDlgContext));
	SdtDlgContext.hwndDlg = CreateDialogParam(g_hInstance, MAKEINTRESOURCE(IDD_DIALOG_EXTRASLIST),
		hwndParent, &SdtDialogProc, 0);

	if (SdtDlgContext.hwndDlg == NULL) {
		return;
	}

	g_wobjDialogs[WOBJ_SSDTDLG_IDX] = SdtDlgContext.hwndDlg;	

	SetWindowText(SdtDlgContext.hwndDlg, TEXT("System Service Table"));

	hIcon = LoadImage(g_hInstance, MAKEINTRESOURCE(IDI_ICON_MAIN), IMAGE_ICON, 0, 0, LR_SHARED);
	if (hIcon) {
		SetClassLongPtr(SdtDlgContext.hwndDlg, GCLP_HICON, (LONG_PTR)hIcon);
		DestroyIcon(hIcon);
	}

	SdtDlgContext.ListView = GetDlgItem(SdtDlgContext.hwndDlg, ID_EXTRASLIST);
	if (SdtDlgContext.ListView) {

		ListView_SetImageList(SdtDlgContext.ListView, ListViewImages, LVSIL_SMALL);
		ListView_SetExtendedListViewStyle(SdtDlgContext.ListView,
			LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

		//create ObjectList columns
		RtlSecureZeroMemory(&col, sizeof(col));
		col.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT | LVCF_WIDTH | LVCF_ORDER | LVCF_IMAGE;
		col.iSubItem = 1;
		col.pszText = L"Id";
		col.fmt = LVCFMT_LEFT | LVCFMT_BITMAP_ON_RIGHT;
		col.iOrder = 0;
		col.iImage = ImageList_GetImageCount(ListViewImages) - 1;
		col.cx = 80;
		ListView_InsertColumn(SdtDlgContext.ListView, 1, &col);

		col.iSubItem = 2;
		col.pszText = L"Service Name";
		col.iOrder = 1;
		col.iImage = -1;
		col.cx = 200;
		ListView_InsertColumn(SdtDlgContext.ListView, 2, &col);

		col.iSubItem = 3;
		col.pszText = L"Address";
		col.iOrder = 2;
		col.iImage = -1;
		col.cx = 130;
		ListView_InsertColumn(SdtDlgContext.ListView, 3, &col);

		col.iSubItem = 4;
		col.pszText = L"Module";
		col.iOrder = 3;
		col.iImage = -1;
		col.cx = 200;
		ListView_InsertColumn(SdtDlgContext.ListView, 4, &col);

		//remember columns count
		SdtDlgContext.lvColumnCount = 4;

		SdtListTable();
		SendMessage(SdtDlgContext.hwndDlg, WM_SIZE, 0, 0);
		
		ListView_SortItemsEx(SdtDlgContext.ListView, &SdtDlgCompareFunc, 0);
	}
}
