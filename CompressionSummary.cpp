#include "CompressionSummary.h"
#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#include "Hunk.h"
#include "Compressor/Compressor.h"

using namespace std;

HTREEITEM Parent;
CompressionSummaryRecord* g_summary;

void buildSummaryList(CompressionSummaryRecord* csr, HWND hwnd) {
	int NUM_COLUMNS = 3;
	int MAX_ITEMLEN = 100;

	if(csr->type != RECORD_ROOT) {
		LV_ITEM item;
		item.mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE;
		item.state = 0;
		item.stateMask = 0;
		item.iItem = INT_MAX;	//index
		item.iSubItem = 0;
		item.pszText = NULL;
		item.cchTextMax = MAX_ITEMLEN;
		item.lParam = (LPARAM)csr;

		//set item columns
		int idx = ListView_InsertItem(hwnd, &item);
		char buff[1024];
		if(csr->type == RECORD_SECTION)
			sprintf_s(buff, sizeof(buff), "%s", csr->name.c_str());
		else if(csr->type == RECORD_PUBLIC)
			sprintf_s(buff, sizeof(buff), "  %s", csr->name.c_str());
		else //LOCAL
			sprintf_s(buff, sizeof(buff), "    %s", csr->name.c_str());

		ListView_SetItemText(hwnd, idx, 0, buff);	//label
		sprintf_s(buff, sizeof(buff), "%d", csr->pos);
		ListView_SetItemText(hwnd, idx, 1, buff);	//pos
		sprintf_s(buff, sizeof(buff), "%d", csr->size);
		ListView_SetItemText(hwnd, idx, 3, buff);	//size
		if(csr->compressedPos >= 0) {
			sprintf_s(buff, sizeof(buff), "%.2f", csr->compressedPos / (BITPREC*8.0f));
			ListView_SetItemText(hwnd, idx, 2, buff);	//compressedpos
			sprintf_s(buff, sizeof(buff), "%.2f", csr->compressedSize / (BITPREC*8.0f));
			ListView_SetItemText(hwnd, idx, 4, buff);	//compressedsize
		}
	}

	for(vector<CompressionSummaryRecord*>::iterator it = csr->children.begin(); it != csr->children.end(); it++) {
		buildSummaryList(*it, hwnd);
	}
}

LRESULT CALLBACK SummaryDlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_INITDIALOG:
	{
		HWND listHwnd = GetDlgItem(hWndDlg, IDC_LIST2);

		//add columns
		LV_COLUMN column;
		column.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
		column.fmt = LVCFMT_LEFT;
		column.cx = 300;
		column.pszText = "label";
		column.iSubItem = 0;
		ListView_InsertColumn(listHwnd, 0, &column);
		column.fmt = LVCFMT_RIGHT;
		column.cx = 60;
		column.pszText = "pos";
		column.iSubItem = 1;
		ListView_InsertColumn(listHwnd, 1, &column);
		column.pszText = "comp-pos";
		column.iSubItem = 2;
		ListView_InsertColumn(listHwnd, 2, &column);
		column.pszText = "size";
		column.iSubItem = 3;
		ListView_InsertColumn(listHwnd, 3, &column);
		column.pszText = "compsize";
		column.iSubItem = 4;
		ListView_InsertColumn(listHwnd, 4, &column);

		buildSummaryList(g_summary, listHwnd);
	
		//center window
		return TRUE;
	}

	case WM_COMMAND:
		switch(wParam) {
	case ID_KILL:
		EndDialog(hWndDlg, 0);
		ExitProcess(-1);
		return TRUE;
		}
		break;
	case WM_CLOSE:
		EndDialog(hWndDlg, 0);
		return TRUE;
	}


	return FALSE;
}


CompressionSummary::CompressionSummary() {
}

CompressionSummary::~CompressionSummary() {
}

void CompressionSummary::Show(CompressionSummaryRecord* csr) {
	HINSTANCE hInstance = GetModuleHandle(NULL);
	HWND hWnd = GetDesktopWindow();
	g_summary = csr;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_COMPRESSION_SUMMARY_DIALOG),
		hWnd, reinterpret_cast<DLGPROC>(SummaryDlgProc));
}