# 构建 libwulin.so 与引导桩 stub.bin（arm64-v8a，API 23）
$ErrorActionPreference = 'Stop'
$ndk  = 'C:\Users\User\andtools\android-ndk-r27c\toolchains\llvm\prebuilt\windows-x86_64\bin'
$cc   = "$ndk\aarch64-linux-android23-clang.cmd"
$objc = "$ndk\llvm-objcopy.exe"
$src  = 'C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\native'

# v261：注入版本号（构建时间 + git 短 hash），日志第一行就能对上 APK 文件名。
# 之前 common.h 里硬编码 "v179" 从没改过，排查时多次误判「装了新版」实际还是旧版。
$gitHash = 'nogit'
try {
    $g = (& git -C $src rev-parse --short HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $g) { $gitHash = "$g".Trim() }
} catch { }
$stamp = Get-Date -Format 'MMdd-HHmm'
$ver   = "v283-$stamp-$gitHash"

$defineVer = "-DMOD_VER_RAW=$ver"

Push-Location $src
try {
    Write-Host '--- libwulin.so'
    & $cc -shared -fPIC -O2 -Wall -Wextra -o libwulin.so `
        $defineVer `
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
