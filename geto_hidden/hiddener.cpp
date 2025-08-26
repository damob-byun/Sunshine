#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <cwchar>

// UTF-8 to UTF-16 conversiona
static std::wstring utf8_to_wide(const std::string& utf8) {
	if (utf8.empty()) return L"";
	int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (need <= 0) return L"";
	std::wstring wide(static_cast<size_t>(need - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), need);
	return wide;
}

static DWORD find_process_id_by_name_utf8(const std::string& exe_name_utf8) {
	std::wstring exe_name_w = utf8_to_wide(exe_name_utf8);
	if (exe_name_w.empty()) return 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return 0;

	PROCESSENTRY32W proc_entry{};
	proc_entry.dwSize = sizeof(PROCESSENTRY32W);
	DWORD pid = 0;

	if (Process32FirstW(snapshot, &proc_entry)) {
		do {
			if (_wcsicmp(proc_entry.szExeFile, exe_name_w.c_str()) == 0) {
				pid = proc_entry.th32ProcessID;
				break;
			}
		} while (Process32NextW(snapshot, &proc_entry));
	}

	CloseHandle(snapshot);
	return pid;
}

struct EnumData {
	DWORD pid;
	std::vector<HWND> hwnds;
};

static BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM l_param) {
	EnumData* data = reinterpret_cast<EnumData*>(l_param);
	DWORD window_pid = 0;
	GetWindowThreadProcessId(hwnd, &window_pid);
	if (window_pid == data->pid && IsWindowVisible(hwnd)) {
		// Consider top-level, visible windows only
		if (GetWindow(hwnd, GW_OWNER) == nullptr) {
			data->hwnds.push_back(hwnd);
		}
	}
	return TRUE;
}

// Move window far outside the virtual desktop so it is not visible nor clickable
static const int k_offscreen_offset = 10000;
static RECT get_virtual_screen_rect() {
	RECT virtual_screen{};
	virtual_screen.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	virtual_screen.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
	virtual_screen.right = virtual_screen.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
	virtual_screen.bottom = virtual_screen.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
	return virtual_screen;
}

static void move_off_screen(HWND hwnd) {
	RECT virtual_screen = get_virtual_screen_rect();
	int x = virtual_screen.right + k_offscreen_offset;  // far to the right of all monitors
	int y = virtual_screen.bottom + k_offscreen_offset; // far below all monitors
	SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static bool is_off_screen(HWND hwnd) {
	RECT virtual_screen = get_virtual_screen_rect();
	RECT rect{};
	if (!GetWindowRect(hwnd, &rect)) return false;
	return (rect.left >= virtual_screen.right + k_offscreen_offset - 1) && (rect.top >= virtual_screen.bottom + k_offscreen_offset - 1);
}

int main() {
	// Keep source encoded as UTF-8; pass UTF-8 here.
	std::string target_exe_utf8 = "WmCLt.exe";
	const DWORD k_sleep_ms = 100; // fixed sleep interval

	unsigned long long count = 0; // loop counter
	bool pending_verify = false;   // verify moved windows on next tick
	std::vector<HWND> last_hwnds;  // windows moved in previous tick

	for (;;) {
		++count;

		// Exit after approximately 1 minute has elapsed
		if (count * (unsigned long long)k_sleep_ms >= 60000ULL) {
			// one minute passed
			return 0;
		}

		DWORD pid = find_process_id_by_name_utf8(target_exe_utf8);
		if (pid == 0) {
			pending_verify = false;
			last_hwnds.clear();
			Sleep(k_sleep_ms);
			continue; // keep looping until process appears
		}

		EnumData enum_data{ pid, {} };
		EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&enum_data));
		if (enum_data.hwnds.empty()) {
			pending_verify = false;
			last_hwnds.clear();
			Sleep(k_sleep_ms);
			continue; // wait for windows to be created
		}

		// Move current windows off-screen every tick
		for (HWND hwnd_item : enum_data.hwnds) {
			move_off_screen(hwnd_item);
		}

		// If we moved windows in the previous tick, verify now
		bool all_moved = false;
		if (pending_verify && !last_hwnds.empty()) {
			all_moved = true;
			for (HWND hwnd_item : last_hwnds) {
				if (!is_off_screen(hwnd_item)) { all_moved = false; break; }
			}
			if (all_moved) break; // success
		}

		// Prepare for next tick verification
		last_hwnds = enum_data.hwnds;
		pending_verify = true;

		Sleep(k_sleep_ms); // fixed 100ms interval
	}

	return 0;
}
