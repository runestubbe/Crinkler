#include "WindowProgressBar.h"
#include <windows.h>
#include <iostream>
#include <ctime>
#include <commctrl.h>
#include "resource.h"

using namespace std;
volatile HWND g_WndDlg;
volatile HWND g_WndProgress;
volatile HWND g_WndElapsedTime;

void centerWindow(HWND childHwnd, HWND parentHwnd) {
	RECT dialogRect, parentRect, workAreaRect;

	GetWindowRect(childHwnd, &dialogRect);
	int wChild = dialogRect.right - dialogRect.left;
	int hChild = dialogRect.bottom - dialogRect.top;

	// Get the height and width of the parent window.
	GetWindowRect (parentHwnd, &parentRect);
	int wParent = parentRect.right - parentRect.left;
	int hParent = parentRect.bottom - parentRect.top;

	// Get the limits of the "work area".
	BOOL bResult = SystemParametersInfo(SPI_GETWORKAREA, sizeof(RECT), &workAreaRect, 0);
	if (!bResult) {
		workAreaRect.left = workAreaRect.top = 0;
		workAreaRect.right = GetSystemMetrics(SM_CXSCREEN);
		workAreaRect.bottom = GetSystemMetrics(SM_CYSCREEN);
	}

	// Calculate new X position, then adjust for work area.
	int xNew = parentRect.left + ((wParent - wChild) /2);
	if (xNew < workAreaRect.left) {
		xNew = workAreaRect.left;
	} else if ((xNew+wChild) > workAreaRect.right) {
		xNew = workAreaRect.right - wChild;
	}

	// Calculate new Y position, then adjust for work area.
	int yNew = parentRect.top  + ((hParent - hChild) /2);
	if (yNew < workAreaRect.top) {
		yNew = workAreaRect.top;
	} else if ((yNew+hChild) > workAreaRect.bottom) {
		yNew = workAreaRect.bottom - hChild;
	}

	SetWindowPos(childHwnd, NULL, xNew, yNew, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

LRESULT CALLBACK DlgProc(HWND hWndDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch(Msg)
	{
	case WM_INITDIALOG:
		g_WndElapsedTime = GetDlgItem(hWndDlg, IDC_ELAPSED_TIME);
		g_WndProgress = GetDlgItem(hWndDlg, IDC_PROGRESS1);
		g_WndDlg = hWndDlg;

		//center window
		centerWindow(hWndDlg, GetDesktopWindow());
		return TRUE;

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

DWORD WINAPI ThreadProc( LPVOID lpParam ) {
	HINSTANCE hInstance = GetModuleHandle(NULL);
	HWND hWnd = GetDesktopWindow();
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_PROGRESS_DIALOG),
		hWnd, reinterpret_cast<DLGPROC>(DlgProc));
	return 0;
}

void WindowProgressBar::init() {
	g_WndDlg = NULL;

	DWORD threadID;
	m_thread = CreateThread(
		NULL,              // default security attributes
		0,                 // use default stack size  
		ThreadProc,        // thread function 
		NULL,             // argument to thread function 
		0,                 // use default creation flags 
		&threadID);   // returns the thread identifier 

	//wait for window
	while(g_WndDlg == NULL)
		Sleep(1);

	m_maxValue = 256;
	SendMessage(g_WndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, m_maxValue));
}

void WindowProgressBar::beginTask(const char* name) {
	SetWindowText(g_WndDlg, name);
	m_stime = clock();
	m_name = name;
	update(0, 256);
	return;
}

void WindowProgressBar::endTask() {

}

void WindowProgressBar::update(int n, int max) {
	char buff[128];
	//update title
	sprintf_s(buff,sizeof(buff), "%d%% - %s", (100*n)/max, m_name.c_str());
	SetWindowText(g_WndDlg, buff);

	//update progress bar
	if(max != m_maxValue) {
		SendMessage(g_WndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, max));
		m_maxValue = max;
	}
	SendMessage(g_WndProgress, PBM_SETPOS, n, 0);

	//update elapsed time
	int secs = (clock()-m_stime) / CLOCKS_PER_SEC;
	sprintf_s(buff, sizeof(buff), "%3dm%02ds", secs/60, secs%60);
	SendMessage(g_WndElapsedTime, WM_SETTEXT, 0, (LPARAM)buff);
}