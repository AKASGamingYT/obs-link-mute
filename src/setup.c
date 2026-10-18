#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <tlhelp32.h>

extern const unsigned char _binary_plugin_dll_start[];
extern const unsigned char _binary_plugin_dll_end[];

static int silent;

static void note(const wchar_t *text)
{
	if (!silent)
		MessageBoxW(NULL, text, L"OBS Link Mute", MB_OK | MB_ICONINFORMATION);
}

static void fail(const wchar_t *text)
{
	if (!silent)
		MessageBoxW(NULL, text, L"OBS Link Mute", MB_OK | MB_ICONERROR);
}

static int mkdir_p(wchar_t *path)
{
	wchar_t buf[MAX_PATH];
	size_t i;

	if (wcslen(path) >= MAX_PATH)
		return 0;
	wcscpy(buf, path);
	for (i = 0; buf[i]; i++) {
		if ((buf[i] == L'\\' || buf[i] == L'/') && i > 0 && buf[i - 1] != L':') {
			buf[i] = 0;
			CreateDirectoryW(buf, NULL);
			buf[i] = L'\\';
		}
	}
	return CreateDirectoryW(buf, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int plugin_dir(wchar_t *dest, size_t dest_chars)
{
	wchar_t base[MAX_PATH];

	if (FAILED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, base)))
		return 0;
	if (swprintf(dest, dest_chars, L"%ls\\obs-studio\\plugins\\obs-link-mute", base) < 0)
		return 0;
	if (dest[0] == 0 || dest[1] != L':')
		return 0;
	return 1;
}

static int obs_is_running(void)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32W entry;
	int running = 0;

	if (snap == INVALID_HANDLE_VALUE)
		return 0;
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snap, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, L"obs64.exe") == 0) {
				running = 1;
				break;
			}
		} while (Process32NextW(snap, &entry));
	}
	CloseHandle(snap);
	return running;
}

static void register_uninstall(const wchar_t *dir)
{
	HKEY key;
	wchar_t command[MAX_PATH * 2];
	const wchar_t *name = L"OBS Link Mute";
	const wchar_t *version = L"1.0.0";
	DWORD one = 1;

	if (swprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\\uninstall.exe\" /uninstall", dir) < 0)
		return;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\obs-link-mute", 0,
			     NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
		return;
	RegSetValueExW(key, L"DisplayName", 0, REG_SZ, (const BYTE *)name, (DWORD)((wcslen(name) + 1) * sizeof(wchar_t)));
	RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ, (const BYTE *)version,
		       (DWORD)((wcslen(version) + 1) * sizeof(wchar_t)));
	RegSetValueExW(key, L"Publisher", 0, REG_SZ, (const BYTE *)L"AKASGamingYT",
		       (DWORD)(wcslen(L"AKASGamingYT") + 1) * sizeof(wchar_t));
	RegSetValueExW(key, L"UninstallString", 0, REG_SZ, (const BYTE *)command,
		       (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)));
	RegSetValueExW(key, L"InstallLocation", 0, REG_SZ, (const BYTE *)dir,
		       (DWORD)((wcslen(dir) + 1) * sizeof(wchar_t)));
	RegSetValueExW(key, L"NoModify", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
	RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, (const BYTE *)&one, sizeof(one));
	RegCloseKey(key);
}

static int install_plugin(void)
{
	wchar_t dir[MAX_PATH];
	wchar_t bin[MAX_PATH];
	wchar_t dll[MAX_PATH];
	wchar_t self[MAX_PATH];
	wchar_t uninstaller[MAX_PATH];
	HANDLE file;
	DWORD size;
	DWORD wrote = 0;

	if (!plugin_dir(dir, MAX_PATH)) {
		fail(L"Could not find the ProgramData folder.");
		return 1;
	}
	if (swprintf(bin, MAX_PATH, L"%ls\\bin\\64bit", dir) < 0 || !mkdir_p(bin)) {
		fail(L"Could not create the OBS plugin folder.");
		return 1;
	}
	if (swprintf(dll, MAX_PATH, L"%ls\\obs-link-mute.dll", bin) < 0)
		return 1;

	size = (DWORD)(_binary_plugin_dll_end - _binary_plugin_dll_start);
	file = CreateFileW(dll, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		fail(L"Could not write the plugin. Close OBS Studio and run the installer again.");
		return 1;
	}
	if (!WriteFile(file, _binary_plugin_dll_start, size, &wrote, NULL) || wrote != size) {
		CloseHandle(file);
		fail(L"Could not write the plugin.");
		return 1;
	}
	CloseHandle(file);

	if (!GetModuleFileNameW(NULL, self, MAX_PATH))
		return 1;
	if (swprintf(uninstaller, MAX_PATH, L"%ls\\uninstall.exe", dir) > 0)
		CopyFileW(self, uninstaller, FALSE);
	register_uninstall(dir);

	if (obs_is_running())
		note(L"Installed. Restart OBS Studio to load it.");
	else
		note(L"Installed. Open OBS Studio, then use Tools \u2192 Audio Monitoring.");
	return 0;
}

static int uninstall_plugin(void)
{
	wchar_t dir[MAX_PATH];
	wchar_t bat[MAX_PATH];
	wchar_t temp[MAX_PATH];
	FILE *script;
	STARTUPINFOW startup;
	PROCESS_INFORMATION process;

	if (!plugin_dir(dir, MAX_PATH))
		return 1;
	RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\obs-link-mute");
	if (!GetTempPathW(MAX_PATH, temp))
		return 1;
	if (swprintf(bat, MAX_PATH, L"%lsobs-link-mute-uninstall.bat", temp) < 0)
		return 1;
	script = _wfopen(bat, L"wb");
	if (!script)
		return 1;
	{
		unsigned char bom[2] = {0xFF, 0xFE};
		wchar_t body[1024];
		int n = swprintf(body, 1024,
				 L"@echo off\r\n"
				 L"for /l %%%%i in (1,1,20) do (\r\n"
				 L"  rmdir /s /q \"%ls\"\r\n"
				 L"  if not exist \"%ls\" goto done\r\n"
				 L"  ping -n 2 127.0.0.1 >nul\r\n"
				 L")\r\n"
				 L":done\r\n"
				 L"del \"%%~f0\"\r\n",
				 dir, dir);
		fwrite(bom, 1, 2, script);
		if (n > 0)
			fwrite(body, sizeof(wchar_t), (size_t)n, script);
	}
	fclose(script);

	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESHOWWINDOW;
	startup.wShowWindow = SW_HIDE;
	if (swprintf(temp, MAX_PATH, L"cmd.exe /c \"%ls\"", bat) > 0) {
		if (CreateProcessW(NULL, temp, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
			CloseHandle(process.hProcess);
			CloseHandle(process.hThread);
		}
	}
	if (!silent)
		note(L"OBS Link Mute was removed. Restart OBS Studio if it is open.");
	return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, LPWSTR command, int show)
{
	int uninstall = 0;
	int argc = 0;
	LPWSTR *argv;

	(void)instance;
	(void)prev;
	(void)show;
	argv = CommandLineToArgvW(command, &argc);
	if (argv) {
		for (int i = 0; i < argc; i++) {
			if (_wcsicmp(argv[i], L"/uninstall") == 0 || _wcsicmp(argv[i], L"--uninstall") == 0)
				uninstall = 1;
			if (_wcsicmp(argv[i], L"/S") == 0 || _wcsicmp(argv[i], L"/silent") == 0)
				silent = 1;
		}
		LocalFree(argv);
	}
	return uninstall ? uninstall_plugin() : install_plugin();
}
