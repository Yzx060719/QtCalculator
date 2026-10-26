// 编译器包装器：调用真正的 g++，并把 stdout/stderr 通过匿名管道直接透传。
// 用途：qmake 在本机受限环境下调用 QProcess 读取编译器输出会失败
//       （QProcess: CreateFile failed，管道无法创建），
//       用它作为 QMAKE_CXX / QMAKE_CC 绕开该限制。
// 实现说明：用 CreateProcess + 显式管道，避免经 cmd.exe 解析引号与重定向。
#include <cstdio>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static std::string quoteArg(const std::string &arg)
{
    // 按 Windows 命令行规则给参数加引号并转义内部引号与反斜杠
    std::string out = "\"";
    size_t backslashes = 0;
    for (char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
            out += ch;
        } else if (ch == '"') {
            out.append(backslashes + 1, '\\');
            out += '"';
            backslashes = 0;
        } else {
            backslashes = 0;
            out += ch;
        }
    }
    out.append(backslashes, '\\');
    out += '"';
    return out;
}

int main(int argc, char **argv)
{
    static const char *kRealCompiler = "D:\\Qt\\Tools\\mingw1310_64\\bin\\g++.exe";

    std::string cmdLine = quoteArg(kRealCompiler);
    for (int i = 1; i < argc; ++i) {
        cmdLine += " ";
        cmdLine += quoteArg(argv[i]);
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        return 1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back('\0');

    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             0, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return 1;
    }

    // 边读边转发，避免子进程因管道写满而阻塞
    char buffer[8192];
    DWORD readBytes = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &readBytes, nullptr) && readBytes > 0) {
        fwrite(buffer, 1, readBytes, stdout);
        fflush(stdout);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readPipe);
    return static_cast<int>(exitCode);
}
