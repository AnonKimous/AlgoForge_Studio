#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#elif defined(__unix__) || defined(__APPLE__)
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace debug_tool::crash_dump {

inline const char* _Directory() {
  return "testData/crash_dumps";
}

#if defined(_WIN32)

inline LONG WINAPI _UnhandledExceptionFilter(EXCEPTION_POINTERS* exception_info) {
  static LONG handling = 0;
  if (InterlockedCompareExchange(&handling, 1, 0) != 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  char current_directory[MAX_PATH]{};
  const DWORD current_directory_length = GetCurrentDirectoryA(
    static_cast<DWORD>(sizeof(current_directory)),
    current_directory);
  if (current_directory_length != 0 && current_directory_length < sizeof(current_directory)) {
    const std::string directory = std::string(current_directory) + "\\testData\\crash_dumps";
    const std::string test_data_directory = std::string(current_directory) + "\\testData";
    CreateDirectoryA(test_data_directory.c_str(), nullptr);
    CreateDirectoryA(directory.c_str(), nullptr);

    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    char file_stem[256]{};
    sprintf_s(
      file_stem,
      "debugTool_%04u%02u%02u_%02u%02u%02u_%lu_%lu",
      static_cast<unsigned>(local_time.wYear),
      static_cast<unsigned>(local_time.wMonth),
      static_cast<unsigned>(local_time.wDay),
      static_cast<unsigned>(local_time.wHour),
      static_cast<unsigned>(local_time.wMinute),
      static_cast<unsigned>(local_time.wSecond),
      static_cast<unsigned long>(GetCurrentProcessId()),
      static_cast<unsigned long>(GetCurrentThreadId()));

    const std::string dump_path = directory + "\\" + file_stem + ".dmp";
    BOOL dump_ok = FALSE;
    DWORD dump_error = ERROR_SUCCESS;
    HANDLE dump_file = CreateFileA(
      dump_path.c_str(),
      GENERIC_WRITE,
      FILE_SHARE_READ,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
    if (dump_file != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION dump_exception{};
      dump_exception.ThreadId = GetCurrentThreadId();
      dump_exception.ExceptionPointers = exception_info;
      dump_exception.ClientPointers = FALSE;
      const MINIDUMP_TYPE dump_type = static_cast<MINIDUMP_TYPE>(
        MiniDumpNormal |
        MiniDumpWithDataSegs |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules);
      dump_ok = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        dump_file,
        dump_type,
        &dump_exception,
        nullptr,
        nullptr);
      dump_error = dump_ok ? ERROR_SUCCESS : GetLastError();
      CloseHandle(dump_file);
    }

    const std::string report_path = directory + "\\" + file_stem + ".crash.txt";
    HANDLE report_file = CreateFileA(
      report_path.c_str(),
      GENERIC_WRITE,
      FILE_SHARE_READ,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
    if (report_file != INVALID_HANDLE_VALUE) {
      char report[1024]{};
      const unsigned long exception_code = exception_info && exception_info->ExceptionRecord
        ? exception_info->ExceptionRecord->ExceptionCode
        : 0ul;
      const std::uintptr_t exception_address = exception_info && exception_info->ExceptionRecord
        ? reinterpret_cast<std::uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress)
        : 0u;
      const std::uintptr_t fault_address = exception_info && exception_info->ExceptionRecord &&
          exception_info->ExceptionRecord->NumberParameters > 1u
        ? static_cast<std::uintptr_t>(exception_info->ExceptionRecord->ExceptionInformation[1])
        : 0u;
      const unsigned long access_type = exception_info && exception_info->ExceptionRecord &&
          exception_info->ExceptionRecord->NumberParameters > 0u
        ? static_cast<unsigned long>(exception_info->ExceptionRecord->ExceptionInformation[0])
        : 0ul;
      MEMORY_BASIC_INFORMATION memory_info{};
      const SIZE_T queried_memory = VirtualQuery(
        reinterpret_cast<const void*>(exception_address),
        &memory_info,
        sizeof(memory_info));
      char exception_module[MAX_PATH]{};
      const DWORD exception_module_length = queried_memory == sizeof(memory_info)
        ? GetModuleFileNameA(
            static_cast<HMODULE>(memory_info.AllocationBase),
            exception_module,
            static_cast<DWORD>(sizeof(exception_module)))
        : 0u;
      const std::uintptr_t exception_module_base = queried_memory == sizeof(memory_info)
        ? reinterpret_cast<std::uintptr_t>(memory_info.AllocationBase)
        : 0u;
      const std::uintptr_t exception_module_offset = exception_address - exception_module_base;
      const char* access_name = access_type == 0u ? "read" : (access_type == 1u ? "write" : "execute");
#if defined(_M_X64)
      const std::uintptr_t instruction_pointer = exception_info && exception_info->ContextRecord
        ? static_cast<std::uintptr_t>(exception_info->ContextRecord->Rip)
        : 0u;
      const std::uintptr_t stack_pointer = exception_info && exception_info->ContextRecord
        ? static_cast<std::uintptr_t>(exception_info->ContextRecord->Rsp)
        : 0u;
      const std::uintptr_t register_rcx = exception_info && exception_info->ContextRecord
        ? static_cast<std::uintptr_t>(exception_info->ContextRecord->Rcx)
        : 0u;
      const std::uintptr_t register_rdx = exception_info && exception_info->ContextRecord
        ? static_cast<std::uintptr_t>(exception_info->ContextRecord->Rdx)
        : 0u;
      const std::uintptr_t register_rbx = exception_info && exception_info->ContextRecord
        ? static_cast<std::uintptr_t>(exception_info->ContextRecord->Rbx)
        : 0u;
      const std::uintptr_t register_r8 = exception_info && exception_info->ContextRecord
        ? static_cast<std::uintptr_t>(exception_info->ContextRecord->R8)
        : 0u;
      const std::uintptr_t register_r9 = exception_info && exception_info->ContextRecord
        ? static_cast<std::uintptr_t>(exception_info->ContextRecord->R9)
        : 0u;
#else
      const std::uintptr_t instruction_pointer = 0u;
      const std::uintptr_t stack_pointer = 0u;
      const std::uintptr_t register_rcx = 0u;
      const std::uintptr_t register_rdx = 0u;
      const std::uintptr_t register_rbx = 0u;
      const std::uintptr_t register_r8 = 0u;
      const std::uintptr_t register_r9 = 0u;
#endif
      const int report_length = sprintf_s(
        report,
        "exception_code=0x%08lX\nexception_address=0x%p\nexception_module=%s\nexception_module_base=0x%p\nexception_module_offset=0x%llX\nfault_address=0x%p\naccess=%s\nip=0x%llX\nrsp=0x%llX\nrcx=0x%llX\nrdx=0x%llX\nrbx=0x%llX\nr8=0x%llX\nr9=0x%llX\npid=%lu\ntid=%lu\ndump=%s\ndump_write=%s\ndump_error=%lu\n",
        exception_code,
        reinterpret_cast<void*>(exception_address),
        exception_module_length != 0u ? exception_module : "<unknown>",
        reinterpret_cast<void*>(exception_module_base),
        static_cast<unsigned long long>(exception_module_offset),
        reinterpret_cast<void*>(fault_address),
        access_name,
        static_cast<unsigned long long>(instruction_pointer),
        static_cast<unsigned long long>(stack_pointer),
        static_cast<unsigned long long>(register_rcx),
        static_cast<unsigned long long>(register_rdx),
        static_cast<unsigned long long>(register_rbx),
        static_cast<unsigned long long>(register_r8),
        static_cast<unsigned long long>(register_r9),
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        dump_path.c_str(),
        dump_ok ? "ok" : "failed",
        static_cast<unsigned long>(dump_error));
      DWORD written = 0;
      WriteFile(report_file, report, static_cast<DWORD>(report_length), &written, nullptr);
      CloseHandle(report_file);
    }
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

inline void Install() {
  SetUnhandledExceptionFilter(&_UnhandledExceptionFilter);
  std::set_terminate([]() { std::abort(); });
}

#elif defined(__unix__) || defined(__APPLE__)

inline void _WriteSignalReport(int signal_number, siginfo_t* signal_info) {
  static std::atomic_flag handling = ATOMIC_FLAG_INIT;
  if (handling.test_and_set()) {
    _exit(128 + signal_number);
  }

  mkdir("testData", 0755);
  mkdir(_Directory(), 0755);

  char path[256]{};
  snprintf(
    path,
    sizeof(path),
    "%s/debugTool_%ld_%d.crash.txt",
    _Directory(),
    static_cast<long>(getpid()),
    signal_number);
  const int file_descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file_descriptor >= 0) {
    char header[512]{};
    const int header_length = snprintf(
      header,
      sizeof(header),
      "signal=%d\npid=%ld\naddress=%p\nbacktrace:\n",
      signal_number,
      static_cast<long>(getpid()),
      signal_info ? signal_info->si_addr : nullptr);
    write(file_descriptor, header, static_cast<size_t>(header_length));
    void* frames[64]{};
    const int frame_count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, frame_count, file_descriptor);
    close(file_descriptor);
  }

  signal(signal_number, SIG_DFL);
  raise(signal_number);
}

inline void _SignalHandler(int signal_number, siginfo_t* signal_info, void*) {
  _WriteSignalReport(signal_number, signal_info);
}

inline void Install() {
  struct sigaction action{};
  action.sa_sigaction = &_SignalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGABRT, &action, nullptr);
  sigaction(SIGBUS, &action, nullptr);
  sigaction(SIGFPE, &action, nullptr);
  sigaction(SIGILL, &action, nullptr);
  sigaction(SIGSEGV, &action, nullptr);
  std::set_terminate([]() { std::abort(); });
}

#else

inline void Install() {
  std::set_terminate([]() { std::abort(); });
}

#endif

}  // namespace debug_tool::crash_dump
