// SPDX-License-Identifier: GPL-2.0-or-later
//
// ExtIO_RAW.cpp

#include <stdio.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <tchar.h>
#include <new>

#include "ExtIO_RAW.h"
#include "resource.h"

static HWND h_dialog;
static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Input Format
enum {
	EXTIO_RAW_FORMAT_RU08 = 0,
	EXTIO_RAW_FORMAT_RU16,
	EXTIO_RAW_FORMAT_RU32,
	EXTIO_RAW_FORMAT_CU08,
	EXTIO_RAW_FORMAT_CU16,
	EXTIO_RAW_FORMAT_CU32,
};

static const TCHAR *RawFormatArr[] = {
	TEXT("RU08"),
	TEXT("RU16"),
	TEXT("RU32"),
	TEXT("CU08"),
	TEXT("CU16"),
	TEXT("CU32"),
};

// ExtIO Options
static int ExtIORawFormat = 0;         // id: 00 default: RU08
static int ExtIOSampleRate = 2400000;  // id: 01 default: Min Sample Rate
static int ExtIOAutoRestart = 0;       // id: 02 default: true
static int ExtIORawBufSize = 4096;     // id: 03 default: 4kB

// Device
static TCHAR ExtIORawDevice[64];
static char *RawDevice;
static int fd;

// Buffer
static unsigned char *RawBuf;
static uint32_t ExtIOBufCount;
static float *ExtIOBuf;
static int RawReadOn;

static HANDLE RawReadHandle = INVALID_HANDLE_VALUE;
static int RawReadThreadStart(void);
static int RawReadThreadStop(void);

static void (*ExtIOCallback)(int, int, float, void *); // ExtIO Callback

static void RawReadCallback(LPVOID /* lpParam */)
{
	int32_t ReadSize = 0;
	int32_t BufCount = 0;

	while (RawDevice && RawReadOn && RawBuf && ExtIOBuf) {
		ReadSize = read(fd, (void *)RawBuf, ExtIORawBufSize);
			if (ReadSize <= 0) {
				if (!ExtIOAutoRestart)
					EXTIO_SET_STATUS(ExtIOCallback, EXTIO_REQ_STOP);

				close(fd);
				fd = open(RawDevice, O_RDONLY | O_BINARY);
				if (fd < 0) {
					RawReadOn = 0;
					EXTIO_SET_STATUS(ExtIOCallback, EXTIO_REQ_STOP);
				}
				continue;
			}

		switch (ExtIORawFormat) {
		case EXTIO_RAW_FORMAT_RU08:
			BufCount = ReadSize * 2;

			for (int32_t i = 0, j = 0; i < BufCount; i += 2, j++) {
				ExtIOBuf[i] = (float)(RawBuf[j] - 127.5f) / 127.5f;
				ExtIOBuf[i + 1] = (float)0.0f;
			}
			break;
		case EXTIO_RAW_FORMAT_RU16:
			BufCount = ReadSize;

			for (int32_t i = 0; i < BufCount; i += 2) {
				ExtIOBuf[i] = (float)(((unsigned short)
					      (RawBuf[i] | (RawBuf[i + 1] << 8))) - 32767.5f) / 32767.5f;
				ExtIOBuf[i + 1] = (float)0.0f;
			}
			break;
		case EXTIO_RAW_FORMAT_RU32:
			BufCount = ReadSize / 2;
			for (int32_t i = 0, j = 0; i < BufCount; i += 2, j += 4) {
				ExtIOBuf[i] = (float)(((unsigned int)
					      (RawBuf[j] | (RawBuf[j + 1] << 8) |
					      (RawBuf[j + 2] << 16) | (RawBuf[j + 3] << 24))) - 2147483647.5f) / 2147483647.5f;
				ExtIOBuf[i + 1] = (float)0.0f;
			}
			break;
		case EXTIO_RAW_FORMAT_CU08:
			BufCount = ReadSize;

			for (int32_t i = 0; i < BufCount; i++)
				ExtIOBuf[i] = (float)(RawBuf[i] - 127.5f) / 127.5f;
			break;
		case EXTIO_RAW_FORMAT_CU16:
			BufCount = ReadSize / 2;

			for (int32_t i = 0, j = 0; i < BufCount; i++, j += 2) {
				ExtIOBuf[i] = (float)(((unsigned short)
					      (RawBuf[j] | (RawBuf[j + 1] << 8))) - 32767.5f) / 32767.5f;
			}
			break;
		case EXTIO_RAW_FORMAT_CU32:
			BufCount = ReadSize / 4;
			for (int32_t i = 0, j = 0; i < BufCount; i++, j += 4) {
				ExtIOBuf[i] = (float)(((unsigned int)
					      (RawBuf[j] | (RawBuf[j + 1] << 8) |
					      (RawBuf[j + 2] << 16) | (RawBuf[j + 3] << 24))) - 2147483647.5f) / 2147483647.5f;
			}
			break;
		}
		ExtIOCallback(BufCount, 0, 0, (void *)ExtIOBuf);
	}
}

extern "C" bool __stdcall InitHW(char *name, char *model, int &hwtype)
{
	strcpy(name, EXTIO_RAW_NAME);
	strcpy(model, "RAW");
	hwtype = EXTIO_USBFLOAT32;
	return TRUE;
}

extern "C" bool __stdcall OpenHW(void)
{
	h_dialog = CreateDialog(hInst, MAKEINTRESOURCE(DLG_MAIN),
				NULL, (DLGPROC)MainDlgProc);
	ShowWindow(h_dialog, SW_HIDE);
	return TRUE;
}

extern "C" void __stdcall CloseHW(void)
{
	if (h_dialog)
		DestroyWindow(h_dialog);
}

static int RawReadThreadStart(void)
{
	// Exit if already running
	if (RawReadHandle != INVALID_HANDLE_VALUE)
		return -1;

	RawReadHandle = (HANDLE)_beginthread(RawReadCallback, 0, NULL);
	if (RawReadHandle == INVALID_HANDLE_VALUE)
		return -1;

	SetThreadPriority(RawReadHandle, THREAD_PRIORITY_TIME_CRITICAL);
	return 0;
}

static int RawReadThreadStop(void)
{
	if (RawReadHandle == INVALID_HANDLE_VALUE)
		return -1;

	WaitForSingleObject(RawReadHandle, INFINITE);
	CloseHandle(RawReadHandle);
	RawReadHandle = INVALID_HANDLE_VALUE;
	return 0;
}

extern "C" int __stdcall StartHW(long /* LOfreq */)
{
	int RawDeviceStrSize;

	if (!ExtIOBufCount || !ExtIORawBufSize)
		return -1;

	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_DEVICE), FALSE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_FORMAT), FALSE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_BUFFER), FALSE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_BUFFER_CTL), FALSE);

	RawDeviceStrSize = wcstombs(NULL, ExtIORawDevice, 0);
	RawDevice = (char *)malloc(RawDeviceStrSize + 1);
	if (!RawDevice) {
		EXTIO_RAW_ERROR("Couldn't allocate device buffer!");
		return -1;
	}
	wcstombs(RawDevice, ExtIORawDevice, RawDeviceStrSize + 1);

	fd = open(RawDevice, O_RDONLY | O_BINARY);
	if (fd < 0)
		goto free_rawdevice;

	ExtIOBuf = new(std::nothrow) float[ExtIOBufCount];
	if (!ExtIOBuf) {
		EXTIO_RAW_ERROR("Couldn't allocate ExtIO buffer!");
		goto close_fd;
	}

	for (uint32_t i = 0; i < ExtIOBufCount; i++)
		ExtIOBuf[i] = 0.0f;

	RawBuf = new(std::nothrow) unsigned char[ExtIORawBufSize];
	if (!RawBuf) {
		EXTIO_RAW_ERROR("Couldn't allocate read buffer!");
		goto free_extiobuf;
	}

	for (int32_t i = 0; i < ExtIORawBufSize; i++)
		RawBuf[i] = 0;

	RawReadOn = 1;

	if (RawReadThreadStart() < 0) {
		RawReadOn = 0;
		goto free_rawbuf;
	}

	EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
	return (int)(ExtIOBufCount / 2);

free_rawbuf:
	delete[] RawBuf;
	RawBuf = NULL;
free_extiobuf:
	delete[] ExtIOBuf;
	ExtIOBuf = NULL;
close_fd:
	close(fd);
free_rawdevice:
	free(RawDevice);
	RawDevice = NULL;
	return -1;
}

extern "C" void __stdcall StopHW(void)
{
	RawReadOn = 0;
	RawReadThreadStop();

	delete[] RawBuf;
	RawBuf = NULL;
	delete[] ExtIOBuf;
	ExtIOBuf = NULL;
	close(fd);
	free(RawDevice);
	RawDevice = NULL;

	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_DEVICE), TRUE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_FORMAT), TRUE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_BUFFER), TRUE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RAW_BUFFER_CTL), TRUE);
}

extern "C" long __stdcall SetHWLO(long /* LOfreq */)
{
	EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_LO);
	return 0;
}

extern "C" int __stdcall GetStatus(void)
{
	return 0;
}

extern "C" void __stdcall SetCallback(void (*ParentCallback)(int, int, float, void *))
{
	ExtIOCallback = ParentCallback;
}

extern "C" long __stdcall GetHWLO(void)
{
	return 0;
}

extern "C" long __stdcall GetHWSR(void)
{
	return (long)ExtIOSampleRate;
}

extern "C" void __stdcall SwitchGUI(void)
{
	if (IsWindowVisible(h_dialog))
		ShowWindow(h_dialog, SW_HIDE);
	else
		ShowWindow(h_dialog, SW_SHOW);
}

extern "C" void __stdcall ShowGUI(void)
{
	ShowWindow(h_dialog, SW_SHOW);
	SetForegroundWindow(h_dialog);
}

extern "C" void __stdcall HideGUI(void)
{
	ShowWindow(h_dialog, SW_HIDE);
}

static void ExtioRawSetParams(void)
{
	switch (ExtIORawFormat) {
	case EXTIO_RAW_FORMAT_RU08:
		ExtIOBufCount = ExtIORawBufSize * 2;
		break;
	case EXTIO_RAW_FORMAT_RU16:
	case EXTIO_RAW_FORMAT_CU08:
		ExtIOBufCount = ExtIORawBufSize;
		break;
	case EXTIO_RAW_FORMAT_RU32:
	case EXTIO_RAW_FORMAT_CU16:
		ExtIOBufCount = ExtIORawBufSize / 2;
		break;
	case EXTIO_RAW_FORMAT_CU32:
		ExtIOBufCount = ExtIORawBufSize / 4;
		break;
	}
}

static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

	switch(uMsg) {
	case WM_INITDIALOG: {
		TCHAR srate[256];
		TCHAR rawbuf[256];

		for (int i = 0; i < ARRAY_SSIZE(RawFormatArr); i++)
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_RAW_FORMAT),
					   RawFormatArr[i]);
		ExtioRawSetParams();
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_RAW_FORMAT), ExtIORawFormat);

		SendMessage(GetDlgItem(hwndDlg, IDC_RAW_SAMPLE_RATE_CTL),
			    UDM_SETRANGE32, (WPARAM)TRUE,
			    MAKELPARAM(EXTIO_RAW_MAXSRATE, EXTIO_RAW_MINSRATE));

		SendMessage(GetDlgItem(hwndDlg, IDC_RAW_SAMPLE_RATE),
			    EM_SETLIMITTEXT, (WPARAM)8, (LPARAM)0);
		_sntprintf(srate, 256, TEXT("%d"), ExtIOSampleRate);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_RAW_SAMPLE_RATE), srate);

		SendMessage(GetDlgItem(hwndDlg, IDC_RAW_BUFFER_CTL),
			    UDM_SETRANGE, (WPARAM)TRUE,
			    MAKELPARAM(EXTIO_RAW_MAXBUF, EXTIO_RAW_MINBUF));

		SendMessage(GetDlgItem(hwndDlg, IDC_RAW_BUFFER),
			    EM_SETLIMITTEXT, (WPARAM)2, (LPARAM)0);
		_sntprintf(rawbuf, 256, TEXT("%d"), ExtIORawBufSize / 1024);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_RAW_BUFFER), rawbuf);

		Button_SetCheck(GetDlgItem(hwndDlg, IDC_RAW_RESTART),
				ExtIOAutoRestart ? BST_CHECKED : BST_UNCHECKED);

		_sntprintf(ExtIORawDevice, 256, TEXT("%s"), TEXT(""));
		Edit_SetText(GetDlgItem(hwndDlg, IDC_RAW_DEVICE), ExtIORawDevice);
		return TRUE;
	}
	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam)) {
		case IDC_RAW_DEVICE:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE) {
				Edit_GetText((HWND)lParam, ExtIORawDevice, 256);
			}
			return TRUE;
		case IDC_RAW_FORMAT:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE) {
				ExtIORawFormat = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
				ExtioRawSetParams();
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
			}
			return TRUE;
		case IDC_RAW_SAMPLE_RATE:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE) {
				TCHAR srate[256];

				Edit_GetText((HWND)lParam, srate, 256);
				ExtIOSampleRate = srate_validate(_ttoi(srate));
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
			}
			return TRUE;
		case IDC_RAW_BUFFER:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE) {
				TCHAR rawbuf[256];

				Edit_GetText((HWND)lParam, rawbuf, 256);
				ExtIORawBufSize = rawbuf_validate(_ttoi(rawbuf)) * 1024;
				ExtioRawSetParams();
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
			}
			return TRUE;
		case IDC_RAW_RESTART:
			if (Button_GetCheck
				(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED)
				ExtIOAutoRestart = 1;
			else
				ExtIOAutoRestart = 0;
			return TRUE;
		}
		break;
	case WM_VSCROLL:
		if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_RAW_SAMPLE_RATE_CTL)) {
			EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
			return TRUE;
		}
		if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_RAW_BUFFER_CTL)) {
			EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
			return TRUE;
		}
		break;
	case WM_CLOSE:
		ShowWindow(h_dialog, SW_HIDE);
		return TRUE;
	case WM_DESTROY:
		h_dialog = NULL;
		return TRUE;
	}
	return FALSE;
}
