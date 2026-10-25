# 构建 libwulin.so 与引导桩 stub.bin（arm64-v8a，API 23）
$ErrorActionPreference = 'Stop'
$ndk  = 'C:\Users\User\andtools\android-ndk-r27c\toolchains\llvm\prebuilt\windows-x86_64\bin'
$cc   = "$ndk\aarch64-linux-android23-clang.cmd"
$objc = "$ndk\llvm-objcopy.exe"
$src  = 'C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\native'

Push-Location $src
try {
    Write-Host '--- libwulin.so'
    & $cc -shared -fPIC -O2 -Wall -Wextra -o libwulin.so `
        src\wulin_main.cpp src\trade.cpp src\ui.cpp src\hook.cpp src\probe.cpp src\water.cpp src\flatten.cpp src\join.cpp -llog -lm `
        '-Wl,-soname,libwulin.so' '-Wl,--build-id=none'
    if ($LASTEXITCODE) { throw 'clang (libwulin) failed' }

    Write-Host '--- stub.o / stub.bin'
    & $cc -c -O2 -o stub.o src\stub.s
    if ($LASTEXITCODE) { throw 'clang (stub) failed' }
    & $objc -O binary --only-section=.text stub.o stub.bin
    if ($LASTEXITCODE) { throw 'objcopy failed' }

    Get-ChildItem libwulin.so, stub.bin | Select-Object Name, Length | Format-Table -AutoSize
} finally {
    Pop-Location
}
