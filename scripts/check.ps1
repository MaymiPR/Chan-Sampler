# The local check: the steps of the Lint and CI workflows on this machine and the hand-over checks, the one command
# before a build is handed over and the proof on a private repository, where no workflow runs. scripts/check.sh is the
# Linux and macOS form with the same steps and options. Windows PowerShell 5.1 or PowerShell 7.
#
#   powershell -NoProfile -File scripts\check.ps1 [-SkipBuild] [-SkipTests] [-Pluginval] [-PluginvalPath exe]
#       [-FocusCheck] [-NullCheck] [-NullOnly text] [-Targets A,B] [-Jobs 4] [-Clap] [-AllowIncomplete] [-AllowFetch]
#       [-AllowStale] [-CloseFl] [-KeepFl] [-IdleSeconds 60] [-BuildDir build] [-Config Release]
#
# Steps, each with one result in the CHECK line (ok, skipped, incomplete or fail; a step that did not run is never ok):
#   fl         FL Studio holds the VST3 files while it runs. Running, the check stops with a message; -CloseFl closes
#              it without saving (below); -KeepFl builds anyway (the build may fail to write the VST3).
#   repo       scripts/check_repo.py.
#   lint       markdownlint-cli2, yamllint, actionlint, and shellcheck on packaging/*.sh and scripts/*.sh, as the Lint
#              workflow runs them. A tool that is not installed is never installed: the step is incomplete.
#   build      configure (on every run, so the pins are checked on every run), then ci_targets and the targets the
#              options need, at a bounded parallel level (-Jobs, default 4, also /MP<Jobs> for the compiler through
#              CL and _CL_). A first configure passes the options CI passes (the tools on, CLAP off; -Clap turns
#              CLAP on) and takes JUCE and clap-juce-extensions from the local.yaml keys juce_src and clap_src, or
#              from <build>/_deps when the copies are there; it refuses to download them unless -AllowFetch is
#              given. A build folder that is configured already keeps its options.
#   warnings   the configure and build output (build.log) by the rule the CI and release workflows use, the same
#              pattern here and there: any compiler, linker or CMake warning fails.
#   vst3       every VST3 of packaging/plugins.txt is newer than every source it is built from: the plug-in binary in
#              the bundle against the files of the plug-in's Source folder, of shared/ and of cmake/, CMakeLists.txt
#              and the plug-in's plugin.cmake. A source changed without a rebuild fails; a built suite in which
#              nothing changed passes, with -SkipBuild too. A CMake file may be newer than the binary when it is
#              older than the last build this script finished: a CMake change that alters no input of the plug-in
#              rewrites no binary. -AllowStale skips the comparison.
#   ctest      the unit and host tests.
#   regress    scripts/render_regress.py, when goldens exist.
#   pluginval  -Pluginval: pluginval at strictness 10 on every VST3 (the executable from -PluginvalPath, the pluginval
#              key of local.yaml or the PATH; never downloaded).
#   focus      -FocusCheck: tools/FocusCheck on every VST3 (the target is built with the others).
#   null       -NullCheck: nulltest.py --vst3 of every plug-in, at the harness's rate and at 96 kHz (-NullOnly text adds --only text): the built VST3
#              against the offline renderer on every case, no reference needed. Its exit code 0 is ok, 1 (a case
#              above the threshold) fail, 2 (the VST3, the renderer or RefRender not built) incomplete.
# A step that cannot run for want of a tool is incomplete; the exit code is 0 only when no step failed and none is
# incomplete (-AllowIncomplete accepts incomplete steps).
#
# Closing FL Studio (-CloseFl) has one method: only while nobody has used the keyboard or the mouse for -IdleSeconds,
# a close message is posted to FL's main window and the No button of its save prompt is pressed through the button's
# window handle. The process is never ended by force and no key or pointer input is synthesized. When the button is
# not found in time the check stops with a message and FL stays open.
param(
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$Pluginval,
    [string]$PluginvalPath = "",
    [switch]$FocusCheck,
    [switch]$NullCheck,
    [string]$NullOnly = "",
    [string[]]$Targets = @(),
    [int]$Jobs = 4,
    [switch]$Clap,
    [switch]$AllowIncomplete,
    [switch]$AllowFetch,
    [switch]$AllowStale,
    [switch]$CloseFl,
    [switch]$KeepFl,
    [int]$IdleSeconds = 60,
    [string]$BuildDir = "build",
    [string]$Config = "Release"
)
$ErrorActionPreference = "Continue"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$steps = @("fl", "repo", "lint", "build", "warnings", "vst3", "ctest", "regress", "pluginval", "focus", "null")
$state = @{}
$detail = @{}
# -File passes "A,B" as one string: split it
$Targets = @($Targets | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($PluginvalPath) { $Pluginval = $true }
if ($NullOnly) { $NullCheck = $true }
if ($Jobs -lt 1) { $Jobs = 1 }

# One result per step: fail outranks incomplete, incomplete outranks ok, and a step of which one part ran and
# another did not is incomplete.
function Ok($step, $msg) {
    Write-Host "PASS  $msg" -ForegroundColor Green
    if (-not $script:state.ContainsKey($step)) { $script:state[$step] = "ok" }
    elseif ($script:state[$step] -eq "skipped") { $script:state[$step] = "incomplete" }
}
function Fail($step, $msg) { Write-Host "FAIL  $msg" -ForegroundColor Red; $script:state[$step] = "fail" }
function Incomplete($step, $msg) {
    Write-Host "MISS  $msg" -ForegroundColor Yellow
    if ($script:state[$step] -ne "fail") { $script:state[$step] = "incomplete" }
}
function Skip($step, $msg) {
    Write-Host "SKIP  $msg"
    if (-not $script:state.ContainsKey($step)) { $script:state[$step] = "skipped" }
    elseif ($script:state[$step] -eq "ok") { $script:state[$step] = "incomplete" }
}
# The CHECK line and the exit code; nothing runs after it.
function Finish {
    $sha = (git rev-parse --short HEAD 2>$null)
    $parts = foreach ($k in $steps) {
        $v = if ($state.ContainsKey($k)) { $state[$k] } else { "skipped" }
        if ($detail.ContainsKey($k) -and $v -eq "fail") { $v = "$v ($($detail[$k]))" }
        "$k $v"
    }
    Write-Host ("CHECK {0} | {1} | {2} | {3} s" -f (Get-Date -Format "yyyy-MM-dd HH:mm"), $sha, ($parts -join " | "), [int]$sw.Elapsed.TotalSeconds)
    $bad = @($state.Values | Where-Object { $_ -eq "fail" }).Count
    $open = @($state.Values | Where-Object { $_ -eq "incomplete" }).Count
    if ($bad) { exit 1 }
    if ($open -and -not $AllowIncomplete) {
        Write-Host "incomplete: $open step(s) did not run in full (-AllowIncomplete accepts that)"
        exit 1
    }
    exit 0
}

# local.yaml: flat "key: value" lines (local.example.yaml lists the keys); an empty value counts as missing.
$cfg = @{}
$localYaml = Join-Path $root "local.yaml"
if (Test-Path $localYaml) {
    foreach ($line in Get-Content $localYaml) {
        if ($line -match '^\s*([A-Za-z0-9_.]+)\s*:\s*(.*?)\s*$') {
            $k = $Matches[1]; $v = $Matches[2]
            if ($v.StartsWith("'")) { $v = $v.Substring(1, $v.LastIndexOf("'") - 1).Replace("''", "'") }
            elseif ($v.StartsWith('"')) { $v = $v.Substring(1, $v.LastIndexOf('"') - 1) }
            else { $v = ($v -replace '\s+#.*$', '') }
            if ($v) { $cfg[$k] = $v }
        }
    }
} else {
    Write-Host "note: no local.yaml (copy local.example.yaml): the defaults are used"
}
$py = if ($cfg.ContainsKey("python")) { $cfg["python"] } else { "python" }
# a line of a native program's output as text (Windows PowerShell wraps stderr lines; an empty one reads as its type name)
function Line($o) { $t = "$o"; if ($t -eq "System.Management.Automation.RemoteException") { "" } else { $t } }
function FullPath($p) { if ([System.IO.Path]::IsPathRooted($p)) { $p } else { [System.IO.Path]::GetFullPath((Join-Path $root $p)) } }

# The plug-ins of packaging/plugins.txt: target, product name, plug-in folder, and the VST3 bundle once it is built
$plugins = @()
$list = Join-Path $root "packaging/plugins.txt"
if (Test-Path $list) {
    foreach ($line in Get-Content $list) {
        if (-not $line.Trim() -or $line.StartsWith('#')) { continue }
        $t, $p = $line.Split([char]0x7C)[0..1]
        # the plug-in folder: plugin in a suite of one plug-in, plugins/<Id> in a suite of several
        $pdir = if (Test-Path (Join-Path $root "plugin/plugin.cmake")) { "plugin" } else { "plugins/$t" }
        $plugins += [pscustomobject]@{ Target = $t; Name = $p; Dir = $pdir; Bundle = $null }
    }
}
function Find-Bundles {
    foreach ($pl in $script:plugins) {
        $bundle = Join-Path $build "$($pl.Target)_artefacts/$Config/VST3/$($pl.Name).vst3"
        if (-not (Test-Path $bundle)) {   # a suite that adds its plug-ins with add_subdirectory builds them one level down
            $dir = Get-ChildItem $build -Directory -Recurse -Filter "$($pl.Target)_artefacts" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($dir) { $bundle = Join-Path $dir.FullName "$Config/VST3/$($pl.Name).vst3" }
        }
        $pl.Bundle = if (Test-Path $bundle) { (Resolve-Path $bundle).Path } else { $null }
    }
}

# 1. FL Studio holds the VST3 through the plug-in folder. The one way it is closed: the header has the rules.
#    Returns nothing when FL is closed, else the reason it is still open.
function Close-Fl($procs) {
    Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class SuiteCheckWin
{
    [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO p);
    [DllImport("kernel32.dll")] static extern uint GetTickCount();
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc f, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, StringBuilder l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    const uint WM_CLOSE = 0x0010, WM_GETTEXT = 0x000D, BM_CLICK = 0x00F5, SMTO_ABORTIFHUNG = 0x0002;

    // Seconds since the last keyboard or mouse input. Both tick counts are unsigned 32-bit values that wrap
    // together, so their unsigned difference is right at any uptime; -1 when the system does not answer.
    public static double IdleSeconds()
    {
        LASTINPUTINFO l = new LASTINPUTINFO();
        l.cbSize = (uint) Marshal.SizeOf(typeof(LASTINPUTINFO));
        if (! GetLastInputInfo(ref l)) return -1.0;
        return unchecked((uint) (GetTickCount() - l.dwTime)) / 1000.0;
    }

    static string Text(IntPtr h)
    {
        StringBuilder sb = new StringBuilder(256);
        IntPtr r;
        if (SendMessageTimeout(h, WM_GETTEXT, (IntPtr) 256, sb, SMTO_ABORTIFHUNG, 500, out r) == IntPtr.Zero || sb.Length == 0)
        {
            sb = new StringBuilder(256);
            GetWindowText(h, sb, 256);
        }
        return sb.ToString();
    }

    static string Class(IntPtr h) { StringBuilder sb = new StringBuilder(256); GetClassName(h, sb, 256); return sb.ToString(); }

    static List<IntPtr> TopWindows(uint pid)
    {
        List<IntPtr> found = new List<IntPtr>();
        EnumWindows(delegate (IntPtr h, IntPtr l)
        {
            uint p;
            GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h)) found.Add(h);
            return true;
        }, IntPtr.Zero);
        return found;
    }

    // The window handle of a visible control with this label (the accelerator mark ignored) in a visible top-level
    // window of the process, or zero.
    public static IntPtr FindButton(uint pid, string label)
    {
        IntPtr hit = IntPtr.Zero;
        foreach (IntPtr top in TopWindows(pid))
        {
            EnumChildWindows(top, delegate (IntPtr h, IntPtr l)
            {
                if (IsWindowVisible(h) && string.Equals(Text(h).Replace("&", "").Trim(), label, StringComparison.OrdinalIgnoreCase))
                {
                    hit = h;
                    return false;
                }
                return true;
            }, IntPtr.Zero);
            if (hit != IntPtr.Zero) break;
        }
        return hit;
    }

    // Class and title of every visible top-level window of the process, for the message when the button is not found
    public static string Describe(uint pid)
    {
        List<string> rows = new List<string>();
        foreach (IntPtr top in TopWindows(pid)) rows.Add(Class(top) + " '" + Text(top) + "'");
        return string.Join("; ", rows.ToArray());
    }

    public static bool PostClose(IntPtr window) { return PostMessage(window, WM_CLOSE, IntPtr.Zero, IntPtr.Zero); }
    public static bool Click(IntPtr button) { return PostMessage(button, BM_CLICK, IntPtr.Zero, IntPtr.Zero); }
}
"@
    $idle = [SuiteCheckWin]::IdleSeconds()
    if ($idle -lt 0) { return "the time since the last input cannot be read: FL Studio is left open" }
    if ($idle -lt $IdleSeconds) {
        return ("the keyboard or mouse was used {0:F0} s ago (-IdleSeconds {1}): FL Studio is left open" -f $idle, $IdleSeconds)
    }
    foreach ($p in $procs) {
        $main = $p.MainWindowHandle
        if ($main -eq [IntPtr]::Zero) { return "FL Studio (pid $($p.Id)) has no main window to close" }
        if (-not [SuiteCheckWin]::PostClose($main)) { return "FL Studio (pid $($p.Id)) refused the close message" }
        $deadline = (Get-Date).AddSeconds(30)
        $clicked = $null
        while ((Get-Date) -lt $deadline) {
            $p.Refresh()
            if ($p.HasExited) { break }
            # the save prompt appears only with unsaved changes; a further prompt gets the same answer
            if (-not $clicked -or ((Get-Date) - $clicked).TotalSeconds -ge 3) {
                $button = [SuiteCheckWin]::FindButton([uint32]$p.Id, "No")
                if ($button -ne [IntPtr]::Zero) { [SuiteCheckWin]::Click($button) | Out-Null; $clicked = Get-Date }
            }
            Start-Sleep -Milliseconds 250
        }
        $p.Refresh()
        if (-not $p.HasExited) {
            $windows = [SuiteCheckWin]::Describe([uint32]$p.Id)
            if ($clicked) { return "FL Studio (pid $($p.Id)) still runs 30 s after the No button was pressed; its windows: $windows" }
            return "the No button of FL Studio's save prompt was not found within 30 s (pid $($p.Id)); its windows: $windows"
        }
    }
    return $null
}

$fl = @(Get-Process FL64 -ErrorAction SilentlyContinue)
if ($SkipBuild) {
    Skip "fl" "FL Studio: not looked at (-SkipBuild writes no VST3)"
} elseif (-not $fl.Count) {
    Ok "fl" "FL Studio is not running"
} elseif ($KeepFl) {
    Skip "fl" "FL Studio is running and -KeepFl was given: the build may fail to write the VST3"
} elseif ($CloseFl) {
    Write-Host "FL Studio is running (pid $(($fl | ForEach-Object { $_.Id }) -join ', ')): closing it without saving"
    $problem = Close-Fl $fl
    if ($problem) {
        Fail "fl" "$problem. Close FL Studio by hand without saving and run the check again; nothing else ran"
        Finish
    }
    Ok "fl" "FL Studio closed without saving"
} else {
    Fail "fl" "FL Studio is running and holds the VST3 files: close it without saving, or pass -CloseFl (closes it when nobody is at the machine) or -KeepFl (builds anyway); nothing else ran"
    Finish
}

# 2. The repository checks
$out = & $py scripts/check_repo.py 2>&1
if ($LASTEXITCODE -eq 0) { Ok "repo" "check_repo.py" }
else {
    $out | ForEach-Object { Write-Host "$_" }
    $n = @($out | Where-Object { "$_" -match '^\S.*:\d+: [A-Z]+: ' -or "$_" -match ': release: ' }).Count
    $detail["repo"] = "$n findings"
    Fail "repo" "check_repo.py: $n finding(s)"
}

# 3. The lint tools the Lint workflow runs. A tool that is not installed is reported, never installed.
function Lint($name, $exe, $toolArgs) {
    if (-not $exe) { Incomplete "lint" "$name is not installed: not run"; return }
    & $exe @toolArgs 2>&1 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) { Fail "lint" $name } else { Ok "lint" $name }
}
function OnPath($name) { if (Get-Command $name -ErrorAction SilentlyContinue) { $name } else { $null } }
# the tracked files that exist: one deleted in the work tree and not yet staged is no file to lint
$mdFiles = @(git ls-files "*.md" | Where-Object { Test-Path -LiteralPath $_ })
$yamlFiles = @(git ls-files "*.yml" "*.yaml" | Where-Object { Test-Path -LiteralPath $_ })
$shFiles = @(git ls-files "packaging/*.sh" "scripts/*.sh" | Where-Object { Test-Path -LiteralPath $_ })
Lint "markdownlint-cli2" (OnPath "markdownlint-cli2") $mdFiles
$yamlArgs = @("--strict", "-c", ".yamllint.yaml") + $yamlFiles
if (OnPath "yamllint") { Lint "yamllint" "yamllint" $yamlArgs }
else {
    # installed as a module of the interpreter, its launcher not on the PATH
    & $py -c "import yamllint" 2>$null
    if ($LASTEXITCODE -eq 0) { Lint "yamllint" $py (@("-m", "yamllint") + $yamlArgs) } else { Lint "yamllint" $null @() }
}
Lint "actionlint" (OnPath "actionlint") @()
if ($shFiles.Count) { Lint "shellcheck" (OnPath "shellcheck") (@("--severity=warning") + $shFiles) }

# 4. Configure. A first configure passes what CI passes and names the dependency copies; a later one keeps the cache.
$prefix = $null
$cm = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cm -match 'option\((\w+)_TOOLS\b') { $prefix = $Matches[1] } elseif ($cm -match 'option\((\w+)_CLAP\b') { $prefix = $Matches[1] }
$log = Join-Path $root "build.log"
$buildOk = $false
$builtStamp = Join-Path $build ".check-built"      # written when a build of this script finishes
if ($SkipBuild) {
    Skip "build" "build: skipped (-SkipBuild)"
    Skip "warnings" "warnings: not counted (-SkipBuild)"
} else {
    if (Test-Path $log) { Remove-Item $log -Force }
    $conf = @("-S", $root, "-B", $build)
    $refused = $null
    if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
        $conf += @("-A", "x64")
        if ($prefix) {
            $conf += "-D$($prefix)_TOOLS=ON"
            $conf += "-D$($prefix)_CLAP=$(if ($Clap) { 'ON' } else { 'OFF' })"
        }
        $deps = @(@{ Key = "juce_src"; Var = "FETCHCONTENT_SOURCE_DIR_JUCE"; Dir = "juce-src"; Needed = $true },
                  @{ Key = "clap_src"; Var = "FETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS"; Dir = "clap_juce_extensions-src"; Needed = [bool]$Clap })
        foreach ($d in $deps) {
            if (-not $d.Needed) { continue }
            $own = Join-Path $build "_deps/$($d.Dir)"
            if ($cfg.ContainsKey($d.Key)) { $conf += "-D$($d.Var)=$(FullPath $cfg[$d.Key])" }
            elseif (Test-Path $own) { $conf += "-D$($d.Var)=$own" }
            elseif (-not $AllowFetch) {
                $refused = "configure refused: local.yaml does not set $($d.Key) and $BuildDir/_deps/$($d.Dir) does not exist (-AllowFetch lets CMake download it)"
            }
        }
    } elseif ($Clap) {
        Write-Host "note: $BuildDir is configured already and keeps its options: -Clap applies to a first configure"
    }
    if ($refused) { Fail "build" $refused }
    else {
        Write-Host "configure: cmake $($conf -join ' ')"
        & cmake @conf 2>&1 | ForEach-Object { Line $_ } | Tee-Object -FilePath $log
        $code = $LASTEXITCODE
        if ($code -ne 0) { Fail "build" "configure (exit code $code, log: build.log)" }
        else {
            $cache = Get-Content (Join-Path $build "CMakeCache.txt")
            $toolsOn = [bool]($prefix -and ($cache -match "^$($prefix)_TOOLS:BOOL=(ON|TRUE|1|YES)\s*$"))

            # 5. Build ci_targets and what the options need, at a bounded parallel level
            $want = @("ci_targets") + $Targets
            if ($FocusCheck -and $toolsOn) { $want += "FocusCheck" }
            if ($NullCheck -and $toolsOn) { $want += "RefRender" }
            $want = @($want | Select-Object -Unique)
            $env:CMAKE_BUILD_PARALLEL_LEVEL = "$Jobs"
            # CL is read before the command line and _CL_ after it: the second keeps the bound where a target passes /MP itself
            $env:CL = ("/MP$Jobs " + $env:CL).Trim()
            $env:_CL_ = ("$env:_CL_ /MP$Jobs").Trim()
            & cmake --build $build --config $Config --target @want --parallel $Jobs 2>&1 | ForEach-Object { Line $_ } | Tee-Object -FilePath $log -Append
            $code = $LASTEXITCODE
            if ($code -ne 0) { Fail "build" "build (exit code $code, log: build.log)" }
            else {
                $buildOk = $true
                Set-Content -Path $builtStamp -Value (Get-Date -Format o)      # the vst3 step reads its time
                Ok "build" "build: $($want -join ', ')"
            }
        }
    }

    # 6. Warnings in the configure and build output: the rule of the CI and release workflows
    if (Test-Path $log) {
        $hits = @(Select-String -Path $log -CaseSensitive -Pattern 'warning [A-Z]+\d+|warning ?:|\d+ warnings? generated|CMake (Deprecation )?Warning' |
            Where-Object { $_.Line -notmatch '::warning|hint:' })
        if ($hits.Count -gt 0) {
            $hits | ForEach-Object { Write-Host $_.Line -ForegroundColor Yellow }
            $detail["warnings"] = "$($hits.Count)"
            Fail "warnings" "$($hits.Count) warning line(s) in build.log (zero allowed)"
        } elseif ($buildOk) { Ok "warnings" "0 warnings in the configure and build output" }
        else { Skip "warnings" "warnings: none so far, but the build did not finish" }
    } else { Skip "warnings" "warnings: nothing was configured or built" }
}

# 7. Every VST3 is newer than every source it is built from
if ($AllowStale) { Skip "vst3" "VST3 freshness: not compared (-AllowStale)" }
elseif (-not $SkipBuild -and -not $buildOk) { Skip "vst3" "VST3 freshness: not compared (the build did not finish)" }
elseif (-not $plugins.Count) { Skip "vst3" "no plug-in in packaging/plugins.txt: no VST3 to compare" }
else {
    Find-Bundles
    foreach ($pl in $plugins) {
        if (-not $pl.Bundle) {
            if ($SkipBuild) { Incomplete "vst3" "no VST3 of $($pl.Name) under $BuildDir/$($pl.Target)_artefacts (-SkipBuild before its first build): not compared" }
            else { Fail "vst3" "no VST3 of $($pl.Name) under $BuildDir/$($pl.Target)_artefacts" }
            continue
        }
        # the plug-in binary, not the bundle's resources: every build rewrites moduleinfo.json
        $bin = Get-ChildItem $pl.Bundle -Recurse -File |
            Where-Object { $_.FullName -notmatch '[\\/](Resources|_CodeSignature)[\\/]' -and $_.Name -notmatch '\.(json|plist)$' -and $_.Name -ne 'PkgInfo' } |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $bin) { Fail "vst3" "$($pl.Name).vst3 holds no plug-in binary"; continue }
        $built = $bin.LastWriteTime
        # what the VST3 is built from: the plug-in's sources, the shared code and the CMake files
        $src = @(@((Join-Path $pl.Dir "Source"), "shared", "cmake", "CMakeLists.txt", (Join-Path $pl.Dir "plugin.cmake")) |
            Where-Object { Test-Path $_ } |
            ForEach-Object { if (Test-Path $_ -PathType Leaf) { Get-Item $_ } else { Get-ChildItem $_ -Recurse -File } })
        if (-not $src.Count) { Fail "vst3" "$($pl.Name): no source found under $($pl.Dir)/Source: nothing to compare the VST3 with"; continue }
        $newer = @($src | Where-Object { $_.LastWriteTime -gt $built } | Sort-Object LastWriteTime -Descending)
        # a CMake file that the last finished build of this script took in: that build left the binary as it was
        $taken = @()
        if (Test-Path $builtStamp) {
            $stamp = (Get-Item $builtStamp).LastWriteTime
            $taken = @($newer | Where-Object { ($_.Extension -eq '.cmake' -or $_.Name -eq 'CMakeLists.txt') -and $_.LastWriteTime -le $stamp })
            $newer = @($newer | Where-Object { $taken -notcontains $_ })
        }
        if ($newer.Count) {
            $first = $newer[0].FullName.Substring($root.Length).TrimStart('\', '/')
            Fail "vst3" "$($pl.Name).vst3 ($built) is older than $($newer.Count) of its sources, the newest $first ($($newer[0].LastWriteTime)): build it again (-AllowStale skips the comparison)"
        } elseif ($taken.Count) {
            Ok "vst3" "$($pl.Name).vst3 ($built) is newer than its $($src.Count) sources, $($taken.Count) CMake file(s) excepted: older than the last finished build ($stamp), which left the binary as it was"
        } else { Ok "vst3" "$($pl.Name).vst3 ($built) is newer than its $($src.Count) sources" }
    }
}

# 8. Unit tests and the render regression
if ($SkipTests) {
    Skip "ctest" "ctest: skipped (-SkipTests)"
    Skip "regress" "render regression: skipped (-SkipTests)"
} elseif (-not $SkipBuild -and -not $buildOk) {
    Skip "ctest" "ctest: not run (the build did not finish)"
    Skip "regress" "render regression: not run (the build did not finish)"
} else {
    & ctest --test-dir $build -C $Config --output-on-failure 2>&1 | ForEach-Object { Write-Host "$_" }
    if ($LASTEXITCODE -ne 0) { Fail "ctest" "ctest" } else { Ok "ctest" "ctest" }
    if ((Get-ChildItem "tests/regress/golden/*.f32", "tests/regress/*/golden/*.f32" -ErrorAction SilentlyContinue)) {
        & $py scripts/render_regress.py --build $build 2>&1 | ForEach-Object { Write-Host "$_" }
        if ($LASTEXITCODE -ne 0) { Fail "regress" "render regression" } else { Ok "regress" "render regression" }
    } else { Skip "regress" "no goldens in tests/regress yet: the render regression is skipped" }
}

# 9. The hand-over checks on the built VST3s, each on request
$ready = $SkipBuild -or $buildOk
if ($Pluginval -or $FocusCheck -or $NullCheck) { Find-Bundles }
$missing = @($plugins | Where-Object { -not $_.Bundle } | ForEach-Object { $_.Name })

# pluginval at strictness 10; a GUI-subsystem program, so its exit code is read through Start-Process
if (-not $Pluginval) { Skip "pluginval" "pluginval: not requested (-Pluginval)" }
elseif (-not $ready) { Skip "pluginval" "pluginval: not run (the build did not finish)" }
else {
    $pv = $PluginvalPath
    if (-not $pv -and $cfg.ContainsKey("pluginval")) { $pv = $cfg["pluginval"] }
    if (-not $pv) {
        $found = Get-Command pluginval -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { $pv = $found.Source }
    }
    if (-not $pv -or -not (Test-Path $pv)) {
        Incomplete "pluginval" "pluginval is not installed (-PluginvalPath, the pluginval key of local.yaml, the PATH): not run, never downloaded"
    } elseif (-not $plugins.Count) { Skip "pluginval" "pluginval: no plug-in in packaging/plugins.txt" }
    else {
        foreach ($name in $missing) { Fail "pluginval" "pluginval: no VST3 of $name" }
        foreach ($pl in @($plugins | Where-Object { $_.Bundle })) {
            $r = Start-Process -FilePath $pv -NoNewWindow -Wait -PassThru -ArgumentList @("--strictness-level", "10",
                "--skip-gui-tests", "--timeout-ms", "900000", "--validate", "`"$($pl.Bundle)`"")
            if ($r.ExitCode -ne 0) { Fail "pluginval" "pluginval: $($pl.Name) (exit code $($r.ExitCode))" }
            else { Ok "pluginval" "pluginval strictness 10: $($pl.Name)" }
        }
    }
}

# FocusCheck: the space bar still reaches the host after clicks in the editor
if (-not $FocusCheck) { Skip "focus" "FocusCheck: not requested (-FocusCheck)" }
elseif (-not $ready) { Skip "focus" "FocusCheck: not run (the build did not finish)" }
else {
    $fc = Join-Path $build "FocusCheck_artefacts/$Config/FocusCheck.exe"
    if (-not (Test-Path $fc)) {
        $hit = Get-ChildItem $build -Recurse -Filter FocusCheck.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        $fc = if ($hit) { $hit.FullName } else { $null }
    }
    if (-not $fc) { Incomplete "focus" "FocusCheck is not built (the tools option is off, or -SkipBuild before its first build): not run" }
    elseif (-not $plugins.Count) { Skip "focus" "FocusCheck: no plug-in in packaging/plugins.txt" }
    else {
        foreach ($name in $missing) { Fail "focus" "FocusCheck: no VST3 of $name" }
        foreach ($pl in @($plugins | Where-Object { $_.Bundle })) {
            $out = & $fc $pl.Bundle 2>&1 | Out-String
            if ($LASTEXITCODE -ne 0 -or $out -notmatch "PASS") { Write-Host $out; Fail "focus" "FocusCheck: $($pl.Name)" }
            else { Ok "focus" "FocusCheck: $($pl.Name)" }
        }
    }
}

# The built VST3 against the offline renderer on every case of each plug-in's harness; no reference is loaded
if (-not $NullCheck) { Skip "null" "null check: not requested (-NullCheck)" }
elseif (-not $ready) { Skip "null" "null check: not run (the build did not finish)" }
elseif (-not $plugins.Count) { Skip "null" "null check: no plug-in in packaging/plugins.txt" }
else {
    foreach ($pl in $plugins) {
        $script = Join-Path $pl.Dir "nulltest/nulltest.py"
        if (-not (Test-Path $script)) { Skip "null" "null check: $($pl.Name) has no $script"; continue }
        # twice: at the harness's own rate and at 96 kHz
        foreach ($fs in @(0, 96000)) {
            $nullArgs = @($script, "--vst3")
            if ($fs) { $nullArgs += @("--fs", "$fs") }
            if ($NullOnly) { $nullArgs += @("--only", $NullOnly) }
            $at = if ($fs) { "at $fs Hz" } else { "at the harness's rate" }
            & $py @nullArgs 2>&1 | ForEach-Object { Write-Host (Line $_) }
            $code = $LASTEXITCODE
            if ($code -eq 0) { Ok "null" "null check: $($pl.Name) ${at}: the VST3 equals the renderer (nulltest.py --vst3)" }
            elseif ($code -eq 2) { Incomplete "null" "null check: $($pl.Name) ${at}: nulltest.py --vst3 could not run (exit code 2: the VST3, the renderer or RefRender is not built, or no case matches): not compared" }
            else { Fail "null" "null check: $($pl.Name) ${at}: the VST3 differs from the renderer (nulltest.py --vst3, exit code $code)" }
        }
    }
}

Finish
