#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "Psapi.lib")

// Link the real implementation from ../main.cpp; do not copy the hash code.
std::wstring CalculateSha256(const wchar_t* path);

namespace {
namespace fs = std::filesystem;

int failures = 0;
int checks = 0;

void Check(const char* name, bool passed)
{
    ++checks;
    std::cout << (passed ? "PASS: " : "FAIL: ") << name << '\n';
    if (!passed) ++failures;
}

fs::path WriteBytes(const fs::path& directory, const wchar_t* name,
    const std::vector<unsigned char>& bytes)
{
    const fs::path path = directory / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("Could not write test fixture.");
    return path;
}

std::vector<unsigned char> Pattern(std::size_t size)
{
    std::vector<unsigned char> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<unsigned char>(i % 256);
    }
    return bytes;
}

void CheckDigest(const char* name, const fs::path& path,
    const wchar_t* expected)
{
    const bool correct = CalculateSha256(path.c_str()) == expected;
    HANDLE reopened = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const bool released = reopened != INVALID_HANDLE_VALUE;
    if (released) CloseHandle(reopened);
    Check(name, correct && released);
}

ULONGLONG ProcessCpu100ns()
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        throw std::runtime_error("GetProcessTimes failed.");
    }
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}

void RunBenchmark(const fs::path& directory)
{
    constexpr std::size_t fileSize = 64 * 1024 * 1024;
    const fs::path path = directory / L"benchmark-64mib.bin";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const auto block = Pattern(64 * 1024);
        for (std::size_t i = 0; i < fileSize / block.size(); ++i) {
            out.write(reinterpret_cast<const char*>(block.data()),
                static_cast<std::streamsize>(block.size()));
        }
        out.close();
        if (!out) throw std::runtime_error("Could not write benchmark file.");
    }

    const std::wstring expected = CalculateSha256(path.c_str()); // warm-up
    if (expected.size() != 64) throw std::runtime_error("Benchmark warm-up failed.");

    std::vector<double> wallMs;
    std::vector<double> cpuMs;
    for (int i = 0; i < 5; ++i) {
        const auto cpuStart = ProcessCpu100ns();
        const auto wallStart = std::chrono::steady_clock::now();
        const std::wstring digest = CalculateSha256(path.c_str());
        const auto wallEnd = std::chrono::steady_clock::now();
        const auto cpuEnd = ProcessCpu100ns();
        if (digest != expected) throw std::runtime_error("Benchmark hash changed.");
        wallMs.push_back(std::chrono::duration<double, std::milli>(wallEnd - wallStart).count());
        cpuMs.push_back(static_cast<double>(cpuEnd - cpuStart) / 10000.0);
    }

    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        throw std::runtime_error("GetProcessMemoryInfo failed.");
    }

    const fs::path report = directory.parent_path() / L"benchmark.json";
    std::ofstream out(report, std::ios::trunc);
    out << "{\n  \"file_mib\": 64,\n  \"warmup_runs\": 1,\n"
        << "  \"measured_runs\": 5,\n  \"wall_ms\": [";
    for (std::size_t i = 0; i < wallMs.size(); ++i) {
        if (i) out << ", ";
        out << wallMs[i];
    }
    out << "],\n  \"process_cpu_ms\": [";
    for (std::size_t i = 0; i < cpuMs.size(); ++i) {
        if (i) out << ", ";
        out << cpuMs[i];
    }
    out << "],\n  \"working_set_bytes_after\": " << memory.WorkingSetSize
        << ",\n  \"peak_working_set_bytes\": " << memory.PeakWorkingSetSize
        << "\n}\n";
    out.close();
    if (!out) throw std::runtime_error("Could not write benchmark report.");
    fs::remove(path);
    std::cout << "Benchmark saved: " << report.string() << '\n';
}
}

int wmain(int argc, wchar_t* argv[])
{
    try {
        const fs::path directory = fs::current_path() / L".build" / L"fixtures";
        fs::create_directories(directory);

        CheckDigest("empty", WriteBytes(directory, L"empty.bin", {}),
            L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        CheckDigest("abc", WriteBytes(directory, L"abc.bin", {'a', 'b', 'c'}),
            L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        const std::string longVector =
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        CheckDigest("56-byte standard vector", WriteBytes(directory, L"56.bin",
            std::vector<unsigned char>(longVector.begin(), longVector.end())),
            L"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        CheckDigest("binary 0..255", WriteBytes(directory, L"binary.bin", Pattern(256)),
            L"40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");

        struct Boundary { std::size_t size; const wchar_t* expected; };
        constexpr Boundary boundaries[] = {
            {65535, L"5f1bf999bcba5e05d4c34a13710d2e4bff005877874dcce49ac87af61076231e"},
            {65536, L"7daca2095d0438260fa849183dfc67faa459fdf4936e1bc91eec6b281b27e4c2"},
            {65537, L"2deb0bd2129a9d3aed91e3cff58b3993752be549642890a3e853ec1065f9b617"},
            {131072, L"59f410ae5e17962412e2aed4f815918f634932f2abf084f00bb638c4db017850"},
            {131073, L"59143c73fbc669c18bdeb36c7cfa13888c03a2d935b18d939c628692360c16ff"},
        };
        for (const auto& boundary : boundaries) {
            const auto path = WriteBytes(directory, L"boundary.bin", Pattern(boundary.size));
            CheckDigest("64-KiB read boundary", path, boundary.expected);
        }

        CheckDigest("Unicode path", WriteBytes(directory, L"na\u00efve.bin", {'a', 'b', 'c'}),
            L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        Check("missing file", CalculateSha256((directory / L"missing.bin").c_str()) ==
            L"Could not open the selected file.");

        const auto lockedPath = WriteBytes(directory, L"locked.bin", {'a', 'b', 'c'});
        HANDLE exclusive = CreateFileW(lockedPath.c_str(), GENERIC_READ, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (exclusive == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not lock fixture.");
        Check("sharing violation", CalculateSha256(lockedPath.c_str()) ==
            L"Could not open the selected file.");
        CloseHandle(exclusive);

        HANDLE rangeHandle = CreateFileW(lockedPath.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (rangeHandle == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not open lock fixture.");
        OVERLAPPED overlap{};
        if (!LockFileEx(rangeHandle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap)) {
            CloseHandle(rangeHandle);
            throw std::runtime_error("Could not lock byte range.");
        }
        Check("locked byte range", CalculateSha256(lockedPath.c_str()) ==
            L"Could not read the selected file.");
        UnlockFileEx(rangeHandle, 0, 1, 0, &overlap);
        CloseHandle(rangeHandle);

        std::cout << checks - failures << '/' << checks << " checks passed.\n";
        if (failures) return 1;
        if (argc > 1 && std::wstring(argv[1]) == L"--benchmark") RunBenchmark(directory);
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "TEST ERROR: " << error.what() << '\n';
        return 2;
    }
}

