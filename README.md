# FileHash

FileHash is a small native Windows app that calculates the SHA-256 digest of a file. Choose a file, wait for the result, then press Ctrl+C to copy the selected digest. Hashing runs in the background so the window can continue responding. Closing during a hash hides the window and exits when that hash finishes.

The digest identifies file contents. FileHash does not decide whether a file is safe or trustworthy; compare its digest with one supplied by a source you trust.

## Requirements

- Windows 10 or 11, x64
- To build: Visual Studio 2026 with **Desktop development with C++**

## Build and test

From this directory in PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run.ps1 -Benchmark
```

The build creates `x64\Release\FileHash.exe`. The test runner links against the app's actual `CalculateSha256` function and checks known SHA-256 vectors, binary data, 64 KiB read boundaries, a Unicode path, missing files, and file access failures. The benchmark hashes a generated 64 MiB file once to warm up, then five times for measurement. It writes wall time, process CPU time, and process working-set measurements to `tests\.build\benchmark.json`. These warm-cache measurements describe this machine and run; they are not a pass/fail performance target.

To create a ZIP and checksum after all checks pass:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\package.ps1
```

This creates `dist\FileHash-0.1.0-win-x64.zip` and a `.sha256` checksum file. The Release executable uses the static C++ runtime, so the package does not require a separate Visual C++ Redistributable installation. The executable is unsigned.

## Implementation

The Win32 window procedure handles input, timer checks, and shutdown. A `std::async` job reads the selected file in 64 KiB blocks and feeds each block to Windows CNG SHA-256. The worker owns a copy of the path and does not access window controls. The window timer checks whether the future is ready, retrieves the digest, and updates the result box. Closing during an active job waits for that job to finish before destroying the window.

