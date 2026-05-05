[CmdletBinding()]
param (
    [Parameter()][ValidateSet("DEBUG", "RELEASE")][string]$Configuration = "DEBUG",
    [Parameter()][ValidateSet("x64")][string]$Architecture = "x64",
    [Parameter()][bool]$SkipBuildToolsSetup = $false,
    [Parameter()][switch]$RunTests
)

# Forked from walkco/stadia-vigem Build.ps1 (MIT). x86 dropped per Phase 1 spec.
# Targets are added as their source files come into existence:
#   Phase B → libgamepadprofile, tests
#   Phase C → controller-inspector
#   Phase D → nova-xinput

$script:CommonFlags  = @("/Zi", "/W4", "/EHsc", "/DWIN32", "/D_UNICODE", "/DUNICODE", "/utf-8")
$script:DebugFlags   = @("/Od")
$script:ReleaseFlags = @("/GL", "/O2")

function Import-Prerequisites {
    if ($SkipBuildToolsSetup) { return }
    if (Get-Module -ListAvailable -Name VSSetup) {
        Update-Module -Name VSSetup -ErrorAction SilentlyContinue
    } else {
        Install-Module -Name VSSetup -Scope CurrentUser -Force
    }
    Import-Module -Name VSSetup
    if (Get-Module -ListAvailable -Name WintellectPowerShell) {
        Update-Module -Name WintellectPowerShell -ErrorAction SilentlyContinue
    } else {
        Install-Module -Name WintellectPowerShell -Scope CurrentUser -Force
    }
    Import-Module -Name WintellectPowerShell
}

function Invoke-BuildTools {
    param ($Architecture)
    if ($SkipBuildToolsSetup) { return }
    $latest = Get-VSSetupInstance -All | Sort-Object -Property InstallationVersion -Descending | Select-Object -First 1
    Invoke-CmdScript "$($latest.InstallationPath)\VC\Auxiliary\Build\vcvarsall.bat" $Architecture
}

function Invoke-Build-libgamepadprofile {
    param ($Architecture)
    $OutputName = "libgamepadprofile-$Architecture.lib"
    $Flags = If ($Configuration -eq "DEBUG") { $script:DebugFlags } else { $script:ReleaseFlags }

    Write-Host "*** ${OutputName}: Build started ***"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    & "cl.exe" /c $Flags $CommonFlags /Ilibgamepadprofile/include /Ithird_party/cJSON `
        /Foobj/libgamepadprofile/ `
        libgamepadprofile/src/hid.c libgamepadprofile/src/utils.c `
        libgamepadprofile/src/gamepad.c libgamepadprofile/src/profile.c `
        third_party/cJSON/cJSON.c
    if ($LASTEXITCODE -ne 0) { throw "libgamepadprofile compile failed" }
    & "lib.exe" /out:bin/$OutputName obj/libgamepadprofile/*.obj
    if ($LASTEXITCODE -ne 0) { throw "libgamepadprofile lib failed" }

    $sw.Stop()
    Write-Host "*** ${OutputName}: Build finished in $($sw.Elapsed) ***"
}

function Invoke-Build-Tests {
    param ($Architecture)
    $Flags = If ($Configuration -eq "DEBUG") { $script:DebugFlags } else { $script:ReleaseFlags }
    $LibraryPath = "bin/libgamepadprofile-$Architecture.lib"

    Get-ChildItem -Path "tests" -Filter "test_*.c" | ForEach-Object {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($_.Name)
        $exe = "$name-$Architecture.exe"
        Write-Host "*** ${exe}: Build started ***"
        if ($name -eq "test_state_to_xusb") {
            # state_to_xusb.c is not in libgamepadprofile; compile it alongside the test.
            & "cl.exe" $Flags $CommonFlags `
                /Ilibgamepadprofile/include /Inova-xinput/include /Ithird_party/cJSON /Ithird_party/ViGEmClient/include `
                /Foobj/tests/ /Febin/$exe `
                $_.FullName nova-xinput/src/state_to_xusb.c $LibraryPath
        } else {
            & "cl.exe" $Flags $CommonFlags /Ilibgamepadprofile/include /Ithird_party/cJSON `
                /Foobj/tests/ /Febin/$exe $_.FullName $LibraryPath
        }
        if ($LASTEXITCODE -ne 0) { throw "$exe build failed" }
        Write-Host "*** ${exe}: Build finished ***"
    }
}

function Invoke-Build-Inspector {
    param ($Architecture)
    $OutputName = "controller-inspector-$Architecture.exe"
    $Flags = If ($Configuration -eq "DEBUG") { $script:DebugFlags } else { $script:ReleaseFlags }
    $LibraryPath = "bin/libgamepadprofile-$Architecture.lib"

    Write-Host "*** ${OutputName}: Build started ***"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & "cl.exe" $Flags $CommonFlags /Ilibgamepadprofile/include `
        /Foobj/controller-inspector/ /Febin/$OutputName `
        controller-inspector/src/main.c $LibraryPath
    if ($LASTEXITCODE -ne 0) { throw "controller-inspector build failed" }
    $sw.Stop()
    Write-Host "*** ${OutputName}: Build finished in $($sw.Elapsed) ***"
}

function Invoke-Build-NovaXInput {
    param ($Architecture)
    $OutputName = "gamesir-nova-hd-xinput-$Architecture.exe"
    $Flags = If ($Configuration -eq "DEBUG") { $script:DebugFlags } else { $script:ReleaseFlags }
    $LibraryPath = "bin/libgamepadprofile-$Architecture.lib"

    Write-Host "*** ${OutputName}: Build started ***"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    # 1. Compile resources
    & "rc.exe" /foobj/nova-xinput/nova-xinput.res nova-xinput/res/res.rc
    if ($LASTEXITCODE -ne 0) { throw "rc.exe failed" }

    # 2. Compile + link in one cl.exe invocation (mirrors stadia-vigem)
    #    ViGEmClient.cpp must be compiled as C++; cl handles C+CPP TUs side-by-side fine.
    & "cl.exe" $Flags $CommonFlags `
        /Ilibgamepadprofile/include /Inova-xinput/include /Ithird_party/cJSON /Ithird_party/ViGEmClient/include `
        /Foobj/nova-xinput/ /Febin/$OutputName `
        third_party/ViGEmClient/src/ViGEmClient.cpp `
        third_party/cJSON/cJSON.c `
        nova-xinput/src/main.c nova-xinput/src/tray.c nova-xinput/src/log.c `
        nova-xinput/src/bootstrap.c nova-xinput/src/hidhide.c nova-xinput/src/runloop.c `
        nova-xinput/src/state_to_xusb.c `
        obj/nova-xinput/nova-xinput.res `
        $LibraryPath `
        User32.lib Shell32.lib Advapi32.lib Winhttp.lib Setupapi.lib Hid.lib Cfgmgr32.lib Ole32.lib
    if ($LASTEXITCODE -ne 0) { throw "nova-xinput link failed" }

    # 3. Stage profiles next to the exe so the runtime can find them.
    #    The exe loads from <exe_dir>\profiles\*.json on startup.
    New-Item -Path "bin/profiles" -ItemType Directory -Force | Out-Null
    Copy-Item -Path "profiles/*.json" -Destination "bin/profiles/" -Force

    $sw.Stop()
    Write-Host "*** ${OutputName}: Build finished in $($sw.Elapsed) ***"
}

# --- Targets are appended below in later tasks ---

# Entry
New-Item -Path "bin"                          -ItemType Directory -Force | Out-Null
New-Item -Path "obj"                          -ItemType Directory -Force | Out-Null

Import-Prerequisites
Write-Host "-- Build started. Configuration: $Configuration, Architecture: $Architecture --"
Invoke-BuildTools -Architecture $Architecture

# --- Target invocations are appended below in later tasks ---

New-Item -Path "obj/libgamepadprofile" -ItemType Directory -Force | Out-Null
New-Item -Path "obj/tests"             -ItemType Directory -Force | Out-Null

Invoke-Build-libgamepadprofile -Architecture $Architecture
Invoke-Build-Tests             -Architecture $Architecture

New-Item -Path "obj/controller-inspector" -ItemType Directory -Force | Out-Null
Invoke-Build-Inspector -Architecture $Architecture

New-Item -Path "obj/nova-xinput" -ItemType Directory -Force | Out-Null
Invoke-Build-NovaXInput -Architecture $Architecture

if ($RunTests) {
    Write-Host "-- Running tests --"
    $failed = 0
    Get-ChildItem -Path "bin" -Filter "test_*-$Architecture.exe" | ForEach-Object {
        Write-Host "RUN  $($_.Name)"
        & $_.FullName
        if ($LASTEXITCODE -ne 0) { Write-Host "FAIL $($_.Name)"; $failed++ } else { Write-Host "PASS $($_.Name)" }
    }
    if ($failed -gt 0) { throw "$failed test exe(s) failed" }
}

Write-Host "-- Build completed. --"
