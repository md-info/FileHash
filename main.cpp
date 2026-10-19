#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <string>
#include <bcrypt.h>
#include <future>
#include <chrono>
#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Comdlg32.lib")

constexpr int chooseButtonId = 1; 
constexpr int resultBoxId = 2;
constexpr UINT_PTR hashTimerId = 3;

struct AppState
{
	std::future<std::wstring> hashJob;
	bool closing{};
};

std::wstring CalculateSha256(const wchar_t* path)
{
	HANDLE file = CreateFileW(
		path, GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr
	);
	if (file == INVALID_HANDLE_VALUE) {
		return L"Could not open the selected file.";
	}
	BCRYPT_HASH_HANDLE hash{};
	NTSTATUS status = BCryptCreateHash(
		BCRYPT_SHA256_ALG_HANDLE, &hash,
		nullptr, 0, nullptr, 0, 0
	);
	if (status < 0) {
		CloseHandle(file);
		return L"Could not initialize SHA-256.";
	}
	BYTE buffer[64 * 1024];
	DWORD bytesRead{};
	while (true) {
		if (!ReadFile(file,buffer,static_cast<DWORD>(sizeof(buffer)),&bytesRead,nullptr)){
			BCryptDestroyHash(hash);
			CloseHandle(file);
			return L"Could not read the selected file.";
		}
		if (bytesRead == 0) {
			break;
		}
		status = BCryptHashData(hash, buffer, bytesRead, 0);
		if (status < 0) {
			BCryptDestroyHash(hash);
			CloseHandle(file);
			return L"Could not hash the selected file.";
		}
	}
	BYTE digest[32]{};
	status = BCryptFinishHash(hash, digest, static_cast<ULONG>(sizeof(digest)), 0);
	if (status < 0) {
		BCryptDestroyHash(hash);
		CloseHandle(file);
		return L"Could not finish SHA-256";
	}
	BCryptDestroyHash(hash);
	CloseHandle(file);
	constexpr wchar_t hexDigits[] = L"0123456789abcdef";
	std::wstring result;
	result.reserve(sizeof(digest) * 2);
	for (BYTE byte : digest) {
		result.push_back(hexDigits[byte / 16]);
		result.push_back(hexDigits[byte % 16]);
	}

	return result;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_CREATE) {
		const auto* creation = reinterpret_cast<const CREATESTRUCT*>(lParam);
		auto* state = static_cast<AppState*>(creation->lpCreateParams); // app state pointer
		if (state == nullptr) return -1;
		SetLastError(0);
		const LONG_PTR previous = SetWindowLongPtrW(
			window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
		if (previous == 0 && GetLastError() != 0) return -1;
		HWND button = CreateWindowExW( 0, L"BUTTON", L"Choose file", 
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			20, 20, 130, 32, window, 
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(chooseButtonId)),
			nullptr, nullptr);
		if (button == nullptr) return -1;
		HWND resultBox = CreateWindowExW( WS_EX_CLIENTEDGE, L"EDIT", 
			L"Choose a file to calculate its SHA-256.",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY,
			20, 70, 470, 80, window,
			reinterpret_cast<HMENU>(static_cast<INT_PTR>(resultBoxId)), 
			nullptr, nullptr);
		if (resultBox == nullptr) return -1;
		return 0;
	}
	auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA)); // state pointer
	if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
	if (message == WM_COMMAND
		&& LOWORD(wParam) == chooseButtonId
		&& HIWORD(wParam) == BN_CLICKED) 
	{
		if (state->closing || state->hashJob.valid()) return 0;
		HWND resultBox = GetDlgItem(window, resultBoxId);
		wchar_t path[32768]{};
		OPENFILENAMEW dialog{};
		dialog.lStructSize = sizeof(dialog);
		dialog.hwndOwner = window;
		dialog.lpstrFile = path;
		dialog.nMaxFile = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
		dialog.lpstrFilter = L"All files \0*.*\0";
		dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameW(&dialog)) {
			if (SetTimer(window, hashTimerId, 50, nullptr) == 0) {
				SetWindowTextW(resultBox, L"Could not start hashing.");
				return 0;
			}
			try {
				state->hashJob = std::async(
					std::launch::async,
					[selectedPath = std::wstring(path)] {
						return CalculateSha256(selectedPath.c_str());
					}
				);
			}
			catch (...) {
				KillTimer(window, hashTimerId);
				SetWindowTextW(resultBox, L"Could not start hashing.");
				return 0;
			}
			EnableWindow(GetDlgItem(window, chooseButtonId), FALSE);
			SetWindowTextW(resultBox, L"Calculating SHA-256...");
		}
		else if (CommDlgExtendedError() != 0) {
			SetWindowTextW(resultBox, L"Could not open the file picker.");
		}
		return 0;
	}
	if (message == WM_TIMER && wParam == hashTimerId) {
		if (!state->hashJob.valid()) return 0;
		if (state->hashJob.wait_for(std::chrono::milliseconds(0)) 
			!= std::future_status::ready){
			return 0;
		}
		KillTimer(window, hashTimerId);

		std::wstring result;
		try {
			result = state->hashJob.get();
		}
		catch (...) {
			result = L"Could not complete hashing.";
		}

		if (state->closing) {
			DestroyWindow(window);
			return 0;
		}

		HWND resultBox = GetDlgItem(window, resultBoxId);
		SetWindowTextW(resultBox, result.c_str());
		EnableWindow(GetDlgItem(window, chooseButtonId), TRUE);
		SetFocus(resultBox);
		SendMessageW(resultBox, EM_SETSEL, 0, -1);

		return 0;
	}
	if (message == WM_CLOSE) {
		if (state->hashJob.valid()) {
			state->closing = true;
			ShowWindow(window, SW_HIDE);
		}
		else {
			DestroyWindow(window);
		}
		return 0;
	}
	if (message == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
	AppState state{};
	const wchar_t className[] = L"FileHashWindow";
	WNDCLASSW windowClass{};
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = className;
	if (RegisterClassW(&windowClass) == 0) {
		MessageBoxW(nullptr, L"Could not register the window.",
			L"Startup error", MB_OK | MB_ICONERROR
		);

		return 1;
	}
	HWND window = CreateWindowExW( //proc
		0, className, L"FileHash", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
		CW_USEDEFAULT, 520, 220, nullptr, nullptr, instance, 
		&state //app
	);
	if (window == nullptr) {
		MessageBoxW(nullptr, L"Could not create the window.",
			L"Startup error", MB_OK | MB_ICONERROR
		);
		
		return 1;
	}
	ShowWindow(window, showCommand);
	SetFocus(GetDlgItem(window, chooseButtonId));
	MSG message{};
	BOOL result;
	while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) { // app loop 
		if (!IsDialogMessageW(window, &message)) {
			TranslateMessage(&message);
			DispatchMessageW(&message); //proc
		}
	}
	if (result == -1) {
		MessageBoxW(nullptr, L"Could not read a window message.",
			L"Runtime error", MB_OK | MB_ICONERROR
		);

		return 1;
	}

	return static_cast<int>(message.wParam);
}
