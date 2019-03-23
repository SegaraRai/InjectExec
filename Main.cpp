#include <array>
#include <iostream>
#include <locale>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using namespace std::literals;

constexpr size_t PathBufferSize = 33000;


template<typename T>
bool ciEndsWith(std::basic_string_view<T> a, std::basic_string_view<T> b) {
  if (a.size() < b.size()) {
    return false;
  }
  return std::equal(b.rbegin(), b.rend(), a.rbegin(), [](const auto& ca, const auto& cb) {
    return tolower(ca, std::locale::classic()) == tolower(cb, std::locale::classic());
  });
}


int printUsage(int argc, wchar_t* argv[]) {
  std::wcerr << L"usage: "sv << (argc >= 1 ? argv[0] : L"InjectExec") << L" [/-R] [/-S] [/-W] <injectee.exe> <injectant.dll> [...arguments for injectee.exe]"sv << std::endl;
  return 1;
}


int wmain(int argc, wchar_t* argv[]) {
  if (argc < 2) {
    return printUsage(argc, argv);
  }

  bool suspendThread = true;
  bool resolveDllPath = true;
  bool waitProcess = true;
  std::array<std::wstring, 2> paths;
  size_t pathCount = 0;
  std::optional<int> exeArgBeginIndex;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == L'/') {
      const bool on = argv[i][1] != '-';
      const wchar_t mode = on ? argv[i][1] : argv[i][2];
      switch (mode) {
        case L'R':
        case L'r':
          resolveDllPath = on;
          break;

        case L'S':
        case L's':
          suspendThread = on;
          break;

        case L'W':
        case L'w':
          waitProcess = on;
          break;

        case L'?':
          return printUsage(argc, argv);

        default:
          if (pathCount == paths.size()) {
            exeArgBeginIndex = i;
            break;
          }
          std::wcerr << L"unknown option: "sv << argv[i] << std::endl;
          return 1;
      }
      continue;
    }
    // exe path or dll path
    if (pathCount >= paths.size()) {
      exeArgBeginIndex = i;
      break;
    }
    paths[pathCount++] = argv[i];
  }

  if (pathCount != paths.size()) {
    std::wcerr << L"missing arguments"sv << std::endl;
    return 1;
  }

  std::optional<size_t> exeIndex;
  std::optional<size_t> dllIndex;
  for (size_t i = 0; i < paths.size(); i++) {
    const auto& path = paths.at(i);
    if (ciEndsWith<wchar_t>(path, L".dll"sv)) {
      dllIndex = i;
    } else if (ciEndsWith<wchar_t>(path, L".exe"sv) || ciEndsWith<wchar_t>(path, L".com"sv) || ciEndsWith<wchar_t>(path, L".scr"sv)) {
      exeIndex = i;
    }
  }

  if (exeIndex == dllIndex) {
    exeIndex = 0;
    dllIndex = 1;
  } else if (!exeIndex) {
    exeIndex = dllIndex.value() ^ 1;
  } else if (!dllIndex) {
    dllIndex = exeIndex.value() ^ 1;
  }

  std::wstring exePath = paths.at(exeIndex.value());
  std::wstring dllPath = paths.at(dllIndex.value());

  if (resolveDllPath) {
    auto tempPathBuffer = std::make_unique<wchar_t[]>(PathBufferSize);
    if (!GetFullPathNameW(dllPath.c_str(), PathBufferSize - 1, tempPathBuffer.get(), NULL)) {
      std::wcerr << L"GetFullPathNameW failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
      return 1;
    }
    dllPath = tempPathBuffer.get();
  }

  HMODULE hmKernel32 = GetModuleHandleW(L"Kernel32.dll");
  if (!hmKernel32) {
    std::wcerr << L"GetModuleHandleW failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
    return 1;
  }

  FARPROC pfLoadLibraryW = GetProcAddress(hmKernel32, u8"LoadLibraryW");
  if (!pfLoadLibraryW) {
    std::wcerr << L"GetProcAddress failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
    return 1;
  }

  std::wstring exeArgs = L"\""s + exePath + L"\""s;
  if (exeArgBeginIndex) {
    for (int i = exeArgBeginIndex.value(); i < argc; i++) {
      exeArgs += L" \""s + std::regex_replace(argv[i], std::wregex(L"[\"\\]"), L"\\\\$&") + L"\""s;
    }
  }
  auto exeArgsBuffer = std::make_unique<wchar_t[]>(exeArgs.size() + 1);
  memcpy(exeArgsBuffer.get(), exeArgs.c_str(), (exeArgs.size() + 1) * sizeof(wchar_t));

  const DWORD creationFlags = suspendThread ? CREATE_SUSPENDED : 0;
  STARTUPINFOW startupInfo{sizeof(startupInfo)};
  PROCESS_INFORMATION processInformation{};
  if (!CreateProcessW(NULL, exeArgsBuffer.get(), NULL, NULL, FALSE, creationFlags, NULL, NULL, &startupInfo, &processInformation)) {
    std::wcerr << L"CreateProcessW failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
    return 1;
  }

  const size_t dllPathDataSize = (dllPath.size() + 1) * sizeof(wchar_t);

  void* remotePtrDllPath = VirtualAllocEx(processInformation.hProcess, NULL, dllPathDataSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remotePtrDllPath) {
    std::wcerr << L"VirtualAllocEx failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return 1;
  }

  if (!WriteProcessMemory(processInformation.hProcess, remotePtrDllPath, dllPath.c_str(), dllPathDataSize, NULL)) {
    std::wcerr << L"WriteProcessMemory failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
    VirtualFreeEx(processInformation.hProcess, remotePtrDllPath, 0, MEM_RELEASE);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return 1;
  }

  HANDLE hRemoteThread = CreateRemoteThread(processInformation.hProcess, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pfLoadLibraryW), remotePtrDllPath, 0, NULL);
  if (!hRemoteThread) {
    std::wcerr << L"CreateRemoteThread failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
    VirtualFreeEx(processInformation.hProcess, remotePtrDllPath, 0, MEM_RELEASE);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return 1;
  }
  WaitForSingleObject(hRemoteThread, INFINITE);
  CloseHandle(hRemoteThread);
  hRemoteThread = NULL;

  VirtualFreeEx(processInformation.hProcess, remotePtrDllPath, 0, MEM_RELEASE);

  if (suspendThread) {
    if (ResumeThread(processInformation.hThread) == static_cast<DWORD>(-1)) {
      std::wcerr << L"ResumeThread failed (GetLastError = "sv << GetLastError() << L")"sv << std::endl;
      CloseHandle(processInformation.hThread);
      CloseHandle(processInformation.hProcess);
      return 1;
    }
  }

  if (waitProcess) {
    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode;
    if (!GetExitCodeProcess(processInformation.hProcess, &exitCode)) {
      std::wcout << L"process exited with code "sv << exitCode << std::endl;
    } else {
      std::wcout << L"process exited"sv;
    }
  }

  CloseHandle(processInformation.hThread);
  CloseHandle(processInformation.hProcess);

  return 0;
}
