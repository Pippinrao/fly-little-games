$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$scriptPath = Join-Path $repoRoot 'tools\quality\capture_display_evidence.ps1'
. $scriptPath

function Test-ActionThrows {
    param([Parameter(Mandatory = $true)][scriptblock] $Action)
    try { & $Action; return $false } catch { return $true }
}

Describe 'display evidence capture fail-closed helpers' {
    It 'requires exactly one connected device' {
        Test-ActionThrows {
            Assert-CaptureDevice -Devices @() -RequiredSerial 'one' -RequiredModel 'phone'
        } | Should Be $true
        $two = @(
            [pscustomobject]@{ Serial = 'one'; Model = 'phone' },
            [pscustomobject]@{ Serial = 'two'; Model = 'phone' }
        )
        Test-ActionThrows {
            Assert-CaptureDevice -Devices $two -RequiredSerial 'one' -RequiredModel 'phone'
        } | Should Be $true
    }

    It 'rejects serial or model mismatches' {
        $one = @([pscustomobject]@{ Serial = 'actual'; Model = 'X100_Pro' })
        Test-ActionThrows {
            Assert-CaptureDevice -Devices $one -RequiredSerial 'expected' `
                -RequiredModel 'X100_Pro'
        } | Should Be $true
        Test-ActionThrows {
            Assert-CaptureDevice -Devices $one -RequiredSerial 'actual' `
                -RequiredModel 'other'
        } | Should Be $true
    }

    It 'parses only adb entries whose state is device' {
        $text = "List of devices attached`nabc device product:x model:X100_Pro device:x`noff offline model:nope`n"
        $result = @(Get-ConnectedAdbDevices $text)
        $result.Count | Should Be 1
        $result[0].Serial | Should Be 'abc'
        $result[0].Model | Should Be 'X100_Pro'
    }

    It 'requires one unambiguous game SurfaceView layer' {
        Select-GameSurfaceLayer "Other`nSurfaceView[com.flynes.emu/com.flynes.emu.MainActivity]" |
            Should Be 'SurfaceView[com.flynes.emu/com.flynes.emu.MainActivity]'
        Test-ActionThrows { Select-GameSurfaceLayer 'Other' } | Should Be $true
        Test-ActionThrows {
            Select-GameSurfaceLayer "SurfaceView com.flynes.emu one`nSurfaceView com.flynes.emu two"
        } | Should Be $true
    }

    It 'selects an enabled UI target by stable resource id instead of translated text' {
        $xml = Join-Path $TestDrive 'ui.xml'
        '<hierarchy><node resource-id="com.flynes.emu:id/launch_selected" enabled="true" bounds="[20,40][120,100]" /></hierarchy>' |
            Set-Content -LiteralPath $xml -Encoding utf8
        $center = Get-UiTargetCenter -XmlPath $xml `
            -ResourceId 'com.flynes.emu:id/launch_selected'
        $center.X | Should Be 70
        $center.Y | Should Be 70
        Test-ActionThrows {
            Get-UiTargetCenter -XmlPath $xml -ResourceId 'com.flynes.emu:id/missing'
        } | Should Be $true
    }

    It 'never overwrites an existing evidence directory' {
        $root = Join-Path $TestDrive 'evidence'
        $first = New-EvidenceDirectory -Root $root -Leaf 'run'
        Test-Path -LiteralPath $first | Should Be $true
        Test-ActionThrows { New-EvidenceDirectory -Root $root -Leaf 'run' } | Should Be $true
    }

    It 'preserves spaced arguments and propagates a nonzero child exit' {
        $pwsh = (Get-Process -Id $PID).Path
        $child = Join-Path $TestDrive 'argument child.ps1'
        "param([string] `$Value); if (`$Value -ne 'a b') { exit 9 }" |
            Set-Content -LiteralPath $child -Encoding utf8
        $ok = Invoke-CheckedCaptureProcess -FilePath $pwsh -Arguments @(
            '-NoProfile', '-File', $child, 'a b')
        $ok.ExitCode | Should Be 0
        Test-ActionThrows {
            Invoke-CheckedCaptureProcess -FilePath $pwsh `
                -Arguments @('-NoProfile', '-Command', 'exit 7')
        } | Should Be $true
    }

    It 'bounds and terminates a timed-out child tree in both Windows hosts' {
        $child = Join-Path $TestDrive 'timeout-child.ps1'
        'param([string] $PidFile); Set-Content -LiteralPath $PidFile -Value $PID; Start-Sleep -Seconds 30' |
            Set-Content -LiteralPath $child -Encoding utf8
        $hosts = @((Get-Command powershell.exe -ErrorAction Stop).Source,
                (Get-Command pwsh.exe -ErrorAction Stop).Source) | Select-Object -Unique
        foreach ($hostPath in $hosts) {
            $pidFile = Join-Path $TestDrive ((Split-Path $hostPath -Leaf) + '.pid')
            $watch = [Diagnostics.Stopwatch]::StartNew()
            Test-ActionThrows {
                Invoke-CaptureProcess -FilePath $hostPath -Arguments @(
                    '-NoProfile', '-File', $child, $pidFile) -TimeoutSeconds 1
            } | Should Be $true
            $watch.Stop()
            $watch.Elapsed.TotalSeconds | Should BeLessThan 8
            $childPid = [int](Get-Content -LiteralPath $pidFile)
            (Get-Process -Id $childPid -ErrorAction SilentlyContinue) | Should BeNullOrEmpty
        }
    }

    It 'orders Android build-tools versions with vendor suffixes without throwing' {
        (Get-BuildToolsSortVersion '35.0.0-2').ToString() | Should Be '35.0.0'
        (Get-BuildToolsSortVersion '36.1.0-rc1').ToString() | Should Be '36.1.0'
        (Get-BuildToolsSortVersion 'preview').ToString() | Should Be '0.0.0'
    }

    It 'parses and compares the complete system active display mode' {
        $mode = Get-ActiveDisplayMode 'DisplayDeviceInfo{"panel": uniqueId="x", 2340 x 1080, modeId 3, renderFrameRate 120.00001, defaultModeId 1}'
        $mode.Width | Should Be 2340
        $mode.Height | Should Be 1080
        $mode.ModeId | Should Be 3
        $mode.RefreshHz | Should Be 120.00001
        Test-SameActiveDisplayMode $mode ([pscustomobject]@{
                Width = 2340; Height = 1080; ModeId = 3; RefreshHz = 120.00002 }) |
            Should Be $true
        Test-SameActiveDisplayMode $mode ([pscustomobject]@{
                Width = 1920; Height = 1080; ModeId = 3; RefreshHz = 120.00001 }) |
            Should Be $false
        Test-ActionThrows { Get-ActiveDisplayMode 'requestedRefreshRate=120' } | Should Be $true
    }

    It 'requires one core-confirmed source timing from process logs' {
        Get-CoreConfirmedSourceTiming "I/FlyNES EVIDENCE_SOURCE_TIMING=NTSC_60_0988 sourceMilliHz=60099" |
            Should Be 'NTSC_60_0988'
        Test-ActionThrows { Get-CoreConfirmedSourceTiming 'no timing' } | Should Be $true
        Test-ActionThrows {
            Get-CoreConfirmedSourceTiming "EVIDENCE_SOURCE_TIMING=PAL_50`nEVIDENCE_SOURCE_TIMING=NTSC_60_0988"
        } | Should Be $true
    }

    It 'requires one app-confirmed requested display mode identity' {
        $log = 'I/FlyNES EVIDENCE_DISPLAY_REQUEST generation=4 policy=HZ_120 modeId=3 width=2340 height=1080 refreshMilliHz=120000'
        $mode = Get-AppRequestedDisplayMode $log
        $mode.ModeId | Should Be 3
        $mode.Width | Should Be 2340
        $mode.Height | Should Be 1080
        $mode.RefreshHz | Should Be 120
        Test-ActionThrows { Get-AppRequestedDisplayMode 'no request identity' } | Should Be $true
        Test-ActionThrows {
            Get-AppRequestedDisplayMode ($log + "`n" + $log.Replace('modeId=3', 'modeId=4'))
        } | Should Be $true
    }

    It 'rejects a Perfetto process that exits before the bounded trace duration' {
        $start = [DateTime]::UtcNow
        Assert-PerfettoRuntime -StartedAt $start -LastAliveAt $start.AddSeconds(29.2) `
            -StoppedAt $start.AddSeconds(30) `
            -ExpectedSeconds 30
        Test-ActionThrows {
            Assert-PerfettoRuntime -StartedAt $start -LastAliveAt $start.AddSeconds(27) `
                -StoppedAt $start.AddSeconds(30) `
                -ExpectedSeconds 30
        } | Should Be $true
        Test-ActionThrows {
            Assert-PerfettoRuntime -StartedAt $start -LastAliveAt $start.AddSeconds(90) `
                -StoppedAt $start.AddSeconds(91) `
                -ExpectedSeconds 30
        } | Should Be $true
    }

    It 'requires an exact successful Perfetto wrapper exit status' {
        Assert-PerfettoExitStatus "0`n" | Should Be 0
        Test-ActionThrows { Assert-PerfettoExitStatus '1' } | Should Be $true
        Test-ActionThrows { Assert-PerfettoExitStatus '' } | Should Be $true
        Test-ActionThrows { Assert-PerfettoExitStatus "0`n0" } | Should Be $true
    }

    It 'requires a debuggable APK and pair id for 60/120 trials' {
        Assert-TrialContract -TrialMode PUBLIC -TrialPairId '' -Debuggable $false
        Assert-TrialContract -TrialMode 60 -TrialPairId pair-a -Debuggable $true
        Assert-TrialContract -TrialMode 120 -TrialPairId pair-a -Debuggable $true
        Test-ActionThrows {
            Assert-TrialContract -TrialMode 120 -TrialPairId pair-a -Debuggable $false
        } | Should Be $true
        Test-ActionThrows {
            Assert-TrialContract -TrialMode 60 -TrialPairId '' -Debuggable $true
        } | Should Be $true
    }

    It 'rejects either Android emulator property' {
        Test-IsVirtualDevice -KernelQemu 1 -BootQemu '' | Should Be $true
        Test-IsVirtualDevice -KernelQemu '' -BootQemu 1 | Should Be $true
        Test-IsVirtualDevice -KernelQemu '' -BootQemu '' | Should Be $false
    }

    It 'attempts exact remote cleanup for every capture artifact' {
        Mock Invoke-CaptureProcess { [pscustomobject]@{ ExitCode = 0; StdOut = ''; StdErr = '' } }
        Remove-RemoteCaptureArtifacts -Adb 'adb-test' -AdbBase @('-s', 'serial') `
            -RemotePaths @('/data/local/tmp/a trace', '/data/local/tmp/b.png',
                    '/data/local/tmp/c.xml')
        Assert-MockCalled Invoke-CaptureProcess -Times 6 -Exactly -Scope It
        Assert-MockCalled Invoke-CaptureProcess -Times 1 -Exactly -Scope It `
            -ParameterFilter { $Arguments[-1] -eq '/data/local/tmp/a trace' -and
                    $Arguments[-3] -eq 'rm' -and $Arguments[-2] -eq '-f' }
        Assert-MockCalled Invoke-CaptureProcess -Times 1 -Exactly -Scope It `
            -ParameterFilter { $Arguments[-1] -eq '/data/local/tmp/a trace' -and
                    $Arguments[-4] -eq 'test' -and $Arguments[-3] -eq '!' }
    }

    It 'confirms the unique Perfetto wrapper exited before artifact removal' {
        Mock Invoke-CaptureProcess {
            $joined = $Arguments -join ' '
            if ($joined -match 'pidof perfetto') {
                return [pscustomobject]@{ ExitCode = 0; StdOut = '__FLYNES_OK__'; StdErr = '' }
            }
            if ($joined -match '/proc/1234') {
                return [pscustomobject]@{ ExitCode = 0; StdOut = '__FLYNES_GONE__'; StdErr = '' }
            }
            [pscustomobject]@{ ExitCode = 0; StdOut = ''; StdErr = '' }
        }
        Stop-RemotePerfetto -Adb 'adb-test' -AdbBase @('-s', 'serial') `
            -RemoteTrace '/data/misc/perfetto-traces/flynes-token.pftrace' `
            -WrapperPid '1234'
        Assert-MockCalled Invoke-CaptureProcess -Times 2 -Exactly -Scope It `
            -ParameterFilter { ($Arguments -join ' ') -match '/proc/1234' }
    }

    It 'escalates and confirms an orphan Perfetto even without a wrapper pid' {
        $global:flynesPerfettoKilled = $false
        Mock Invoke-CaptureProcess {
            $joined = $Arguments -join ' '
            if ($joined -match 'pidof perfetto') {
                $text = if ($global:flynesPerfettoKilled) { '__FLYNES_OK__' } `
                    else { '__FLYNES_OK__55' }
                return [pscustomobject]@{ ExitCode = 0; StdOut = $text; StdErr = '' }
            }
            if ($joined -match '/proc/55') {
                return [pscustomobject]@{ ExitCode = 0; StdOut =
                    '__FLYNES_ALIVE__perfetto /data/misc/perfetto-traces/flynes-token.pftrace'; StdErr = '' }
            }
            if ($Arguments -contains '-KILL' -and $Arguments -contains '55') {
                $global:flynesPerfettoKilled = $true
            }
            [pscustomobject]@{ ExitCode = 0; StdOut = ''; StdErr = '' }
        }
        Stop-RemotePerfetto -Adb 'adb-test' -AdbBase @('-s', 'serial') `
            -RemoteTrace '/data/misc/perfetto-traces/flynes-token.pftrace' `
            -WrapperPid '' -PollAttempts 1 -PollMilliseconds 1
        Assert-MockCalled Invoke-CaptureProcess -Times 1 -Exactly -Scope It `
            -ParameterFilter { $Arguments -contains 'kill' -and $Arguments -contains '-KILL' `
                    -and $Arguments -contains '55' }
        Remove-Variable flynesPerfettoKilled -Scope Global -ErrorAction SilentlyContinue
    }

    It 'fails closed when the remote Perfetto list query fails' {
        Mock Invoke-CaptureProcess {
            [pscustomobject]@{ ExitCode = 1; StdOut = ''; StdErr = 'transport failure' }
        }
        Test-ActionThrows {
            Stop-RemotePerfetto -Adb 'adb-test' -AdbBase @('-s', 'serial') `
                -RemoteTrace '/data/misc/perfetto-traces/flynes-token.pftrace' `
                -WrapperPid '' -PollAttempts 1 -PollMilliseconds 1
        } | Should Be $true
    }

    It 'fails closed when a listed Perfetto cmdline cannot be confirmed' {
        Mock Invoke-CaptureProcess {
            $joined = $Arguments -join ' '
            if ($joined -match 'pidof perfetto') {
                return [pscustomobject]@{ ExitCode = 0; StdOut = '__FLYNES_OK__55'; StdErr = '' }
            }
            [pscustomobject]@{ ExitCode = 1; StdOut = ''; StdErr = 'permission denied' }
        }
        Test-ActionThrows {
            Stop-RemotePerfetto -Adb 'adb-test' -AdbBase @('-s', 'serial') `
                -RemoteTrace '/data/misc/perfetto-traces/flynes-token.pftrace' `
                -WrapperPid '' -PollAttempts 1 -PollMilliseconds 1
        } | Should Be $true
    }

    It 'fails closed when the wrapper state query fails' {
        Mock Invoke-CaptureProcess {
            $joined = $Arguments -join ' '
            if ($joined -match 'pidof perfetto') {
                return [pscustomobject]@{ ExitCode = 0; StdOut = '__FLYNES_OK__'; StdErr = '' }
            }
            [pscustomobject]@{ ExitCode = 1; StdOut = ''; StdErr = 'transport failure' }
        }
        Test-ActionThrows {
            Stop-RemotePerfetto -Adb 'adb-test' -AdbBase @('-s', 'serial') `
                -RemoteTrace '/data/misc/perfetto-traces/flynes-token.pftrace' `
                -WrapperPid '1234' -PollAttempts 1 -PollMilliseconds 1
        } | Should Be $true
    }

    It 'fails closed when remote cleanup cannot be verified' {
        Mock Invoke-CaptureProcess {
            if ($Arguments -contains 'test') {
                return [pscustomobject]@{ ExitCode = 1; StdOut = ''; StdErr = 'still present' }
            }
            [pscustomobject]@{ ExitCode = 0; StdOut = ''; StdErr = '' }
        }
        Test-ActionThrows {
            Remove-RemoteCaptureArtifacts -Adb 'adb-test' -AdbBase @('-s', 'serial') `
                -RemotePaths @('/data/local/tmp/flynes-token.pftrace')
        } | Should Be $true
    }
}
