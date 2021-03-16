// StubExecutable.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "StubExecutable.h"

#include "semver200.h"
#using <System.ServiceProcess.dll>
#using <System.dll>

using namespace std;


wchar_t* FindRootAppDir()
{
	wchar_t* ourDirectory = new wchar_t[MAX_PATH];

	GetModuleFileName(GetModuleHandle(NULL), ourDirectory, MAX_PATH);
	wchar_t* lastSlash = wcsrchr(ourDirectory, L'\\');
	if (!lastSlash) {
		delete[] ourDirectory;
		return NULL;
	}

	// Null-terminate the string at the slash so now it's a directory
	*lastSlash = 0x0;
	return ourDirectory;
}

wchar_t* FindOwnExecutableName()
{
	wchar_t* ourDirectory = new wchar_t[MAX_PATH];

	GetModuleFileName(GetModuleHandle(NULL), ourDirectory, MAX_PATH);
	wchar_t* lastSlash = wcsrchr(ourDirectory, L'\\');
	if (!lastSlash) {
		delete[] ourDirectory;
		return NULL;
	}

	wchar_t* ret = _wcsdup(lastSlash + 1);
	delete[] ourDirectory;
	return ret;
}


bool DeleteDirectory(const std::wstring &path) {
	std::vector<std::wstring::value_type> doubleNullTerminatedPath;
	std::copy(path.begin(), path.end(), std::back_inserter(doubleNullTerminatedPath));
	doubleNullTerminatedPath.push_back(L'\0');
	doubleNullTerminatedPath.push_back(L'\0');

	SHFILEOPSTRUCTW fileOperation;
	fileOperation.wFunc = FO_DELETE;
	fileOperation.pFrom = &doubleNullTerminatedPath[0];
	fileOperation.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION;

	return ::SHFileOperationW(&fileOperation) == 0;
}

bool DirectoryExists(const std::wstring &path)
{
	DWORD dwAttrib = GetFileAttributes(path.c_str());

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void DoUpdate(const std::wstring& rootDir)
{
	// if "currentTemp" folder exists - copy/move it's content to "current" folder
	auto tempDir = rootDir + L"\\currentTemp";
	if (!DirectoryExists(tempDir))
	{
		::OutputDebugString(L"[Squirrel.Stub] No update was found");
		return;
	}
	auto currentDir = rootDir + L"\\current";

	// attemp to update files from temp to current
	if (DeleteDirectory(currentDir))
	{
		::OutputDebugString(L"[Squirrel.Stub] Update is performing...");
		::MoveFile(tempDir.c_str(), currentDir.c_str());
	}
	else
	{
		// Delegate the update process to BlueJeansUpdater service if the process is unable to gain enough privelages
		::OutputDebugString(L"[Squirrel.Stub] Delegate updating to Updater Service ");
		System::ServiceProcess::ServiceController^ updateService = gcnew System::ServiceProcess::ServiceController("BlueJeansUpdater");
		updateService->ExecuteCommand(128);
		updateService->WaitForStatus(System::ServiceProcess::ServiceControllerStatus::Stopped, System::TimeSpan::FromSeconds(10));
		updateService->WaitForStatus(System::ServiceProcess::ServiceControllerStatus::Running, System::TimeSpan::FromSeconds(10));
	}
}

void ApplyUpdate(const std::wstring &rootDir)
{
	// prevent simultaneous access
	auto mutex = ::CreateMutex(NULL, FALSE, L"{5B61E1CF-3813-4622-BC3B-755BF9851D9D}");
	if (mutex == NULL)
	{
		::OutputDebugString(L"[Squirrel.Stub] Can't synchronize update, verification aborted");
		return;
	}

	::OutputDebugString(L"[Squirrel.Stub] Start update");
	auto waitResult = ::WaitForSingleObject(mutex, 60000);

	switch (waitResult)
	{
	case WAIT_TIMEOUT:
		::OutputDebugString(L"[Squirrel.Stub] Can't wait compeltition update, verification aborted");
		return;
	case WAIT_OBJECT_0:
	{
		__try
		{
			DoUpdate(rootDir); // wraped to finction to prevent SEH unwind
		}
		__finally
		{
			::ReleaseMutex(mutex);
			mutex = nullptr;
		}
	}
	default:
		::ReleaseMutex(mutex);
		mutex = nullptr;
	}

	::OutputDebugString(L"[Squirrel.Stub] Update finished");
}


std::wstring FindLatestAppDir(std::wstring appName)
{
	std::wstring ourDir;
	ourDir.assign(FindRootAppDir());

	ApplyUpdate(ourDir);

	//If current exists, just use that
	std::wstring currDir, currFile;
	currDir.assign(FindRootAppDir());
	currDir += L"\\current";
	currFile = currDir + L"\\" + appName;;

	WIN32_FIND_DATA currInfo = { 0 };
	HANDLE currFileHandle = FindFirstFile(currFile.c_str(), &currInfo);
	if (currFileHandle != INVALID_HANDLE_VALUE) {
		FindClose(currFileHandle);
		return currDir.c_str();
	}

	ourDir += L"\\app-*";

	WIN32_FIND_DATA fileInfo = { 0 };
	HANDLE hFile = FindFirstFile(ourDir.c_str(), &fileInfo);
	if (hFile == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	version::Semver200_version acc("0.0.0");
	std::wstring acc_s;

	do {
		std::wstring appVer = fileInfo.cFileName;
		appVer = appVer.substr(4);   // Skip 'app-'
		if (!(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		std::string s(appVer.begin(), appVer.end());

		version::Semver200_version thisVer(s);

		if (thisVer > acc) {
			acc = thisVer;
			acc_s = appVer;
		}
	} while (FindNextFile(hFile, &fileInfo));

	if (acc == version::Semver200_version("0.0.0")) {
		return NULL;
	}

	ourDir.assign(FindRootAppDir());
	std::wstringstream ret;
	ret << ourDir << L"\\app-" << acc_s;

	FindClose(hFile);
	return ret.str();
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	std::wstring appName;
	appName.assign(FindOwnExecutableName());

	std::wstring fullPath = FindLatestAppDir(appName);
	fullPath += L"\\" + appName;

	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };

	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = nCmdShow;

	std::wstring cmdLine(L"\"");
	cmdLine += fullPath;
	cmdLine += L"\" ";
	cmdLine += lpCmdLine;

	wchar_t* buf = wcsdup(cmdLine.c_str());
	if (!CreateProcess(NULL, buf, NULL, NULL, true, 0, NULL, NULL, &si, &pi)) {
		return -1;
	}

	AllowSetForegroundWindow(pi.dwProcessId);
	WaitForInputIdle(pi.hProcess, 5 * 1000);
	return 0;
}
