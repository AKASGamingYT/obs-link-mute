#include "decide.h"
#include "dialog.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define OBS_SOURCE_AUDIO (1u << 1)
#define OBS_SOURCE_TYPE_INPUT 0

typedef struct obs_source obs_source_t;

#define OBS_IMPORT __declspec(dllimport)

OBS_IMPORT void obs_enum_sources(bool (*enum_proc)(void *, obs_source_t *), void *param);
OBS_IMPORT const char *obs_source_get_name(const obs_source_t *source);
OBS_IMPORT const char *obs_source_get_uuid(const obs_source_t *source);
OBS_IMPORT uint32_t obs_source_get_output_flags(const obs_source_t *source);
OBS_IMPORT int obs_source_get_type(const obs_source_t *source);
OBS_IMPORT obs_source_t *obs_get_source_by_uuid(const char *uuid);
OBS_IMPORT void obs_source_release(obs_source_t *source);

#define ID_LIST 100
#define ID_MODE 101
#define ID_CLOSE 102

static const wchar_t *mode_labels[] = {
	L"Monitor Off",
	L"Monitor Only (mute output)",
	L"Monitoring Enabled",
	L"Monitoring Enabled (mute also mutes monitor)",
};

static HWND dialog;
static HFONT font;
static bool updating;
static bool class_ready;

static void free_list_data(HWND hwnd);

static void wide_copy(wchar_t *dest, size_t dest_chars, const char *utf8)
{
	if (!utf8)
		utf8 = "";
	if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dest, (int)dest_chars))
		dest[0] = 0;
}

static char *selected_uuid(HWND list)
{
	LRESULT index = SendMessageW(list, LB_GETCURSEL, 0, 0);
	LRESULT data;

	if (index == LB_ERR)
		return NULL;
	data = SendMessageW(list, LB_GETITEMDATA, (WPARAM)index, 0);
	if (data == LB_ERR || data == 0)
		return NULL;
	return (char *)data;
}

static void show_choice(HWND hwnd)
{
	HWND list = GetDlgItem(hwnd, ID_LIST);
	HWND mode = GetDlgItem(hwnd, ID_MODE);
	char *uuid = selected_uuid(list);
	obs_source_t *source;
	int choice = LINK_CHOICE_LINKED;

	if (!uuid) {
		EnableWindow(mode, FALSE);
		return;
	}
	source = obs_get_source_by_uuid(uuid);
	EnableWindow(mode, source != NULL);
	if (source) {
		choice = link_mute_current_choice(source);
		obs_source_release(source);
	}
	updating = true;
	SendMessageW(mode, CB_SETCURSEL, (WPARAM)choice, 0);
	updating = false;
}

static bool add_row(void *param, obs_source_t *source)
{
	HWND list = param;
	const char *uuid;
	char *copy;
	wchar_t name[512];
	LRESULT index;

	if (!source || obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return true;
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0)
		return true;
	uuid = obs_source_get_uuid(source);
	if (!uuid || !uuid[0])
		return true;
	copy = _strdup(uuid);
	if (!copy)
		return true;
	wide_copy(name, 512, obs_source_get_name(source));
	index = SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name);
	if (index == LB_ERR) {
		free(copy);
		return true;
	}
	SendMessageW(list, LB_SETITEMDATA, (WPARAM)index, (LPARAM)copy);
	return true;
}

static void fill_list(HWND hwnd)
{
	HWND list = GetDlgItem(hwnd, ID_LIST);
	LRESULT count;
	LRESULT i;

	count = SendMessageW(list, LB_GETCOUNT, 0, 0);
	for (i = 0; i < count; i++) {
		LRESULT data = SendMessageW(list, LB_GETITEMDATA, (WPARAM)i, 0);
		if (data != LB_ERR && data != 0)
			free((void *)data);
	}
	SendMessageW(list, LB_RESETCONTENT, 0, 0);
	obs_enum_sources(add_row, list);
	if (SendMessageW(list, LB_GETCOUNT, 0, 0) > 0)
		SendMessageW(list, LB_SETCURSEL, 0, 0);
	show_choice(hwnd);
}

static void apply_selected(HWND hwnd)
{
	HWND list = GetDlgItem(hwnd, ID_LIST);
	HWND mode = GetDlgItem(hwnd, ID_MODE);
	char *uuid = selected_uuid(list);
	LRESULT choice;
	obs_source_t *source;

	if (updating || !uuid)
		return;
	choice = SendMessageW(mode, CB_GETCURSEL, 0, 0);
	if (choice == CB_ERR)
		return;
	source = obs_get_source_by_uuid(uuid);
	if (!source)
		return;
	link_mute_apply_choice(source, (int)choice);
	obs_source_release(source);
}

static LRESULT CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	(void)lparam;
	switch (message) {
	case WM_COMMAND:
		if (LOWORD(wparam) == ID_LIST && HIWORD(wparam) == LBN_SELCHANGE)
			show_choice(hwnd);
		else if (LOWORD(wparam) == ID_MODE && HIWORD(wparam) == CBN_SELCHANGE)
			apply_selected(hwnd);
		else if (LOWORD(wparam) == ID_CLOSE)
			DestroyWindow(hwnd);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		free_list_data(hwnd);
		dialog = NULL;
		return 0;
	default:
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}
}

static void free_list_data(HWND hwnd)
{
	HWND list = GetDlgItem(hwnd, ID_LIST);
	LRESULT count;
	LRESULT i;

	if (!list)
		return;
	count = SendMessageW(list, LB_GETCOUNT, 0, 0);
	for (i = 0; i < count; i++) {
		LRESULT data = SendMessageW(list, LB_GETITEMDATA, (WPARAM)i, 0);
		if (data != LB_ERR && data != 0)
			free((void *)data);
		SendMessageW(list, LB_SETITEMDATA, (WPARAM)i, 0);
	}
}

void link_mute_close_dialog(void)
{
	if (dialog)
		DestroyWindow(dialog);
}

void link_mute_open_dialog(void)
{
	WNDCLASSW window_class;
	HWND list;
	HWND mode;
	HWND text;
	int i;

	if (dialog) {
		SetForegroundWindow(dialog);
		fill_list(dialog);
		return;
	}

	if (!class_ready) {
		memset(&window_class, 0, sizeof(window_class));
		window_class.lpfnWndProc = dialog_proc;
		window_class.hInstance = GetModuleHandleW(NULL);
		window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
		window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		window_class.lpszClassName = L"LinkMuteAudioMonitoring";
		if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return;
		class_ready = true;
	}

	font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LinkMuteAudioMonitoring", L"Audio Monitoring",
				  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 640, 420, NULL,
				  NULL, GetModuleHandleW(NULL), NULL);
	if (!dialog)
		return;

	text = CreateWindowExW(
		0, L"STATIC",
		L"Monitor Off and Monitoring Enabled are the menu in Advanced Audio Properties. "
		L"Monitor Only (mute output) is preview: you hear it, and the stream does not. "
		L"The last choice is Monitoring Enabled, except mute also silences your headphones. "
		L"Switching here applies immediately.",
		WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 12, 590, 78, dialog, NULL, NULL, NULL);
	list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
				WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 16, 100, 250,
				240, dialog, (HMENU)ID_LIST, NULL, NULL);
	mode = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 280, 100,
				320, 180, dialog, (HMENU)ID_MODE, NULL, NULL);
	CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 520, 340, 84, 28, dialog,
			(HMENU)ID_CLOSE, NULL, NULL);
	SendMessageW(text, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(list, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(mode, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(GetDlgItem(dialog, ID_CLOSE), WM_SETFONT, (WPARAM)font, TRUE);
	for (i = 0; i < 4; i++)
		SendMessageW(mode, CB_ADDSTRING, 0, (LPARAM)mode_labels[i]);
	fill_list(dialog);
	ShowWindow(dialog, SW_SHOW);
	(void)list;
}
