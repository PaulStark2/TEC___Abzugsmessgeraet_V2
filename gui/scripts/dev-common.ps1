# Shared helpers for M14.1 development runtime lifecycle.
# Dot-source from gui/scripts/dev-start.ps1 and gui/scripts/dev-stop.ps1.

$ErrorActionPreference = 'Continue'

$script:GuiRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $script:GuiRoot '..'))
$script:RuntimeDir = Join-Path $script:GuiRoot '.dev-runtime'
$script:GuiPidFile = Join-Path $script:RuntimeDir 'gui.pid'
$script:SimPidFile = Join-Path $script:RuntimeDir 'simulation.pid'
$script:GuiIndexJs = [System.IO.Path]::GetFullPath((Join-Path $script:GuiRoot 'server\index.js'))
$script:SimExe = [System.IO.Path]::GetFullPath(
  (Join-Path $script:RepoRoot 'build\simulation_protocol\simulation_protocol.exe')
)

$script:GuiPort = 3000
$script:SimPort = 8766
# M15 simulation plant channel (virtual sensors, not part of M12).
$script:PlantPort = 8767

function Write-Dev([string]$Message) {
  Write-Host "[DEV] $Message"
}

function Write-Sim([string]$Message) {
  Write-Host "[SIM] $Message"
}

function Write-Gui([string]$Message) {
  Write-Host "[GUI] $Message"
}

function Ensure-RuntimeDir {
  if (-not (Test-Path -LiteralPath $script:RuntimeDir)) {
    New-Item -ItemType Directory -Path $script:RuntimeDir -Force | Out-Null
  }
}

function Save-PidFile([string]$Path, [int]$ProcessId) {
  Ensure-RuntimeDir
  Set-Content -LiteralPath $Path -Value $ProcessId -Encoding ascii -NoNewline
}

function Read-PidFile([string]$Path) {
  if (-not (Test-Path -LiteralPath $Path)) {
    return $null
  }
  $raw = (Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue)
  if ([string]::IsNullOrWhiteSpace($raw)) {
    return $null
  }
  $parsed = 0
  if (-not [int]::TryParse($raw.Trim(), [ref]$parsed)) {
    return $null
  }
  return $parsed
}

function Clear-PidFile([string]$Path) {
  if (Test-Path -LiteralPath $Path) {
    Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
  }
}

function Get-ProcessCommandLine([int]$ProcessId) {
  try {
    $cim = Get-CimInstance -ClassName Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction SilentlyContinue
    if ($null -ne $cim) {
      return [string]$cim.CommandLine
    }
  } catch {
  }
  return $null
}

function Get-ProcessExecutablePath([int]$ProcessId) {
  try {
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -ne $p -and $p.Path) {
      return [System.IO.Path]::GetFullPath($p.Path)
    }
  } catch {
  }
  $cmd = Get-ProcessCommandLine $ProcessId
  if ($cmd) {
    # Best-effort: first quoted token or first token
    if ($cmd -match '^"([^"]+)"') {
      return [System.IO.Path]::GetFullPath($Matches[1])
    }
    $first = ($cmd -split '\s+', 2)[0]
    if ($first) {
      try { return [System.IO.Path]::GetFullPath($first) } catch { }
    }
  }
  return $null
}

function Test-CommandLineContainsPath([string]$CommandLine, [string]$FullPath) {
  if ([string]::IsNullOrWhiteSpace($CommandLine) -or [string]::IsNullOrWhiteSpace($FullPath)) {
    return $false
  }
  $c = $CommandLine.ToLowerInvariant()
  $p1 = $FullPath.ToLowerInvariant()
  $p2 = $p1.Replace('\', '/')
  return ($c.Contains($p1) -or $c.Contains($p2))
}

function Test-CommandLineHasServerIndex([string]$CommandLine) {
  if ([string]::IsNullOrWhiteSpace($CommandLine)) {
    return $false
  }
  return ($CommandLine -match '(?i)server[/\\]index\.js')
}

function Ensure-ProcessCwdHelper {
  if ('DevRuntimeProcessCwd' -as [type]) {
    return
  }
  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class DevRuntimeProcessCwd {
  private const uint PROCESS_QUERY_INFORMATION = 0x0400;
  private const uint PROCESS_VM_READ = 0x0010;

  [StructLayout(LayoutKind.Sequential)]
  private struct PROCESS_BASIC_INFORMATION {
    public IntPtr Reserved1;
    public IntPtr PebBaseAddress;
    public IntPtr Reserved2_0;
    public IntPtr Reserved3;
    public IntPtr UniqueProcessId;
    public IntPtr Reserved4;
  }

  [DllImport("ntdll.dll")]
  private static extern int NtQueryInformationProcess(
    IntPtr processHandle, int processInformationClass,
    ref PROCESS_BASIC_INFORMATION processInformation,
    int processInformationLength, out int returnLength);

  [DllImport("kernel32.dll", SetLastError = true)]
  private static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

  [DllImport("kernel32.dll", SetLastError = true)]
  private static extern bool ReadProcessMemory(
    IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

  [DllImport("kernel32.dll", SetLastError = true)]
  private static extern bool CloseHandle(IntPtr hObject);

  public static string GetWorkingDirectory(int pid) {
    IntPtr h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
    if (h == IntPtr.Zero) return null;
    try {
      PROCESS_BASIC_INFORMATION pbi = new PROCESS_BASIC_INFORMATION();
      int retLen;
      int status = NtQueryInformationProcess(h, 0, ref pbi, Marshal.SizeOf(pbi), out retLen);
      if (status != 0 || pbi.PebBaseAddress == IntPtr.Zero) return null;

      // PEB.ProcessParameters offset: 0x20 (x64)
      byte[] ptrBuf = new byte[IntPtr.Size];
      IntPtr read;
      IntPtr paramsOffset = IntPtr.Add(pbi.PebBaseAddress, IntPtr.Size == 8 ? 0x20 : 0x10);
      if (!ReadProcessMemory(h, paramsOffset, ptrBuf, ptrBuf.Length, out read)) return null;
      IntPtr processParams = BytesToIntPtr(ptrBuf);
      if (processParams == IntPtr.Zero) return null;

      // RTL_USER_PROCESS_PARAMETERS.CurrentDirectory.DosPath (UNICODE_STRING)
      // CurrentDirectory offset: 0x38 (x64) / 0x24 (x86)
      int curDirOffset = IntPtr.Size == 8 ? 0x38 : 0x24;
      byte[] uniBuf = new byte[IntPtr.Size == 8 ? 16 : 8];
      if (!ReadProcessMemory(h, IntPtr.Add(processParams, curDirOffset), uniBuf, uniBuf.Length, out read)) return null;

      ushort length = BitConverter.ToUInt16(uniBuf, 0);
      IntPtr bufferPtr = BytesToIntPtr(uniBuf, IntPtr.Size == 8 ? 8 : 4);
      if (bufferPtr == IntPtr.Zero || length == 0) return null;

      byte[] pathBytes = new byte[length];
      if (!ReadProcessMemory(h, bufferPtr, pathBytes, pathBytes.Length, out read)) return null;
      string path = Encoding.Unicode.GetString(pathBytes);
      return path.TrimEnd('\\', '\0');
    } catch {
      return null;
    } finally {
      CloseHandle(h);
    }
  }

  private static IntPtr BytesToIntPtr(byte[] bytes, int offset = 0) {
    if (IntPtr.Size == 8) return new IntPtr(BitConverter.ToInt64(bytes, offset));
    return new IntPtr(BitConverter.ToInt32(bytes, offset));
  }
}
"@ -ErrorAction Stop
}

function Get-ProcessWorkingDirectory([int]$ProcessId) {
  try {
    Ensure-ProcessCwdHelper
    $cwd = [DevRuntimeProcessCwd]::GetWorkingDirectory($ProcessId)
    if ([string]::IsNullOrWhiteSpace($cwd)) {
      return $null
    }
    return [System.IO.Path]::GetFullPath($cwd.TrimEnd('\'))
  } catch {
    return $null
  }
}

function Test-IsProjectGuiProcess([int]$ProcessId) {
  $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
  if ($null -eq $p) {
    return $false
  }
  if ($p.ProcessName -notin @('node', 'nodejs')) {
    return $false
  }
  $cmd = Get-ProcessCommandLine $ProcessId
  if (-not $cmd) {
    return $false
  }

  # Strong match: absolute path to this project's gui/server/index.js
  if (Test-CommandLineContainsPath $cmd $script:GuiIndexJs) {
    return $true
  }

  # Relative server/index.js needs an ownership signal
  if (-not (Test-CommandLineHasServerIndex $cmd)) {
    return $false
  }

  # CommandLine contains this repo/gui root (e.g. absolute path with spaces)
  if ((Test-CommandLineContainsPath $cmd $script:GuiRoot) -or (Test-CommandLineContainsPath $cmd $script:RepoRoot)) {
    return $true
  }

  # PID file from a previous/current dev:start
  $fromFile = Read-PidFile $script:GuiPidFile
  if ($null -ne $fromFile -and $fromFile -eq $ProcessId) {
    return $true
  }

  # Working directory is this project's gui/ folder (covers "npm start" orphans)
  $cwd = Get-ProcessWorkingDirectory $ProcessId
  if ($cwd -and $cwd.Equals($script:GuiRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    return $true
  }

  return $false
}

function Test-IsProjectSimProcess([int]$ProcessId) {
  $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
  if ($null -eq $p) {
    return $false
  }
  if ($p.ProcessName -ne 'simulation_protocol') {
    return $false
  }
  $exePath = Get-ProcessExecutablePath $ProcessId
  if ($exePath) {
    $expected = $script:SimExe.ToLowerInvariant()
    $actual = $exePath.ToLowerInvariant()
    if ($actual -eq $expected) {
      return $true
    }
  }
  $cmd = Get-ProcessCommandLine $ProcessId
  if ($cmd -and (Test-CommandLineContainsPath $cmd $script:SimExe)) {
    return $true
  }
  # Also accept if path/cmdline references this repo's build\simulation_protocol folder
  $simDir = [System.IO.Path]::GetDirectoryName($script:SimExe)
  $simDirLower = $simDir.ToLowerInvariant()
  $simDirSlash = $simDirLower.Replace('\', '/')
  if ($exePath) {
    $ep = $exePath.ToLowerInvariant()
    if ($ep.Contains($simDirLower) -or $ep.Contains($simDirSlash)) {
      return $true
    }
  }
  if ($cmd) {
    $c = $cmd.ToLowerInvariant()
    if ($c.Contains($simDirLower) -or $c.Contains($simDirSlash)) {
      return $true
    }
  }
  return $false
}

function Get-ListeningPids([int]$Port) {
  $pids = @()
  try {
    $conns = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
    foreach ($c in $conns) {
      if ($c.OwningProcess -and ($pids -notcontains $c.OwningProcess)) {
        $pids += [int]$c.OwningProcess
      }
    }
  } catch {
    # Fallback: netstat parsing
    $lines = netstat -ano -p tcp 2>$null | Select-String ":$Port\s"
    foreach ($line in $lines) {
      $parts = ($line.ToString() -split '\s+') | Where-Object { $_ -ne '' }
      if ($parts.Count -ge 5 -and $parts[3] -match 'LISTEN') {
        $pidVal = 0
        if ([int]::TryParse($parts[-1], [ref]$pidVal) -and ($pids -notcontains $pidVal)) {
          $pids += $pidVal
        }
      }
    }
  }
  return $pids
}

function Test-PortFree([int]$Port) {
  $pids = @(Get-ListeningPids $Port)
  return ($pids.Count -eq 0)
}

function Test-PortReachable([string]$HostName, [int]$Port, [int]$TimeoutMs = 500) {
  $client = $null
  try {
    $client = New-Object System.Net.Sockets.TcpClient
    $iar = $client.BeginConnect($HostName, $Port, $null, $null)
    $ok = $iar.AsyncWaitHandle.WaitOne($TimeoutMs, $false)
    if (-not $ok) {
      return $false
    }
    $client.EndConnect($iar)
    return $client.Connected
  } catch {
    return $false
  } finally {
    if ($null -ne $client) {
      try { $client.Close() } catch { }
    }
  }
}

function Wait-PortReady([string]$HostName, [int]$Port, [int]$TimeoutSec, [string]$Label) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if (Test-PortReachable $HostName $Port) {
      return $true
    }
    Start-Sleep -Milliseconds 250
  }
  Write-Dev "ERROR: $Label did not become ready on port $Port."
  return $false
}

function Find-ProjectGuiPids {
  $found = [System.Collections.Generic.HashSet[int]]::new()

  $fromFile = Read-PidFile $script:GuiPidFile
  if ($null -ne $fromFile -and (Test-IsProjectGuiProcess $fromFile)) {
    [void]$found.Add($fromFile)
  }

  foreach ($pidVal in (Get-ListeningPids $script:GuiPort)) {
    if (Test-IsProjectGuiProcess $pidVal) {
      [void]$found.Add($pidVal)
    }
  }

  # Scan node processes whose command line points at this project's index.js
  try {
    $nodes = Get-CimInstance Win32_Process -Filter "Name = 'node.exe'" -ErrorAction SilentlyContinue
    foreach ($n in $nodes) {
      if (Test-IsProjectGuiProcess ([int]$n.ProcessId)) {
        [void]$found.Add([int]$n.ProcessId)
      }
    }
  } catch {
  }

  return @($found)
}

function Find-ProjectSimPids {
  $found = [System.Collections.Generic.HashSet[int]]::new()

  $fromFile = Read-PidFile $script:SimPidFile
  if ($null -ne $fromFile -and (Test-IsProjectSimProcess $fromFile)) {
    [void]$found.Add($fromFile)
  }

  foreach ($port in @($script:SimPort, $script:PlantPort)) {
    foreach ($pidVal in (Get-ListeningPids $port)) {
      if (Test-IsProjectSimProcess $pidVal) {
        [void]$found.Add($pidVal)
      }
    }
  }

  try {
    $sims = Get-CimInstance Win32_Process -Filter "Name = 'simulation_protocol.exe'" -ErrorAction SilentlyContinue
    foreach ($s in $sims) {
      if (Test-IsProjectSimProcess ([int]$s.ProcessId)) {
        [void]$found.Add([int]$s.ProcessId)
      }
    }
  } catch {
  }

  return @($found)
}

function Stop-ProjectProcess([int]$ProcessId, [string]$Kind) {
  Write-Dev "Stopping $Kind PID $ProcessId ..."
  try {
    Stop-Process -Id $ProcessId -Force -ErrorAction Stop
  } catch {
    Write-Dev "WARNING: Could not stop $Kind PID $ProcessId : $($_.Exception.Message)"
  }
}

function Wait-ProcessGone([int]$ProcessId, [int]$TimeoutSec = 5) {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $p) {
      return $true
    }
    Start-Sleep -Milliseconds 150
  }
  return $false
}

function Report-PortStatus([int]$Port) {
  $pids = @(Get-ListeningPids $Port)
  if ($pids.Count -eq 0) {
    Write-Dev "Port $Port is free."
    return
  }
  foreach ($pidVal in $pids) {
    $name = '?'
    try {
      $proc = Get-Process -Id $pidVal -ErrorAction SilentlyContinue
      if ($proc) { $name = $proc.ProcessName }
    } catch { }
    Write-Dev "WARNING: Port $Port is still in use by PID $pidVal ($name)"
  }
}

function Stop-ProjectGuiProcesses {
  $stoppedAny = $false
  $guiPids = @(Find-ProjectGuiPids)
  if ($guiPids.Count -eq 0) {
    Clear-PidFile $script:GuiPidFile
    return $false
  }
  foreach ($pidVal in $guiPids) {
    if (Test-IsProjectGuiProcess $pidVal) {
      Stop-ProjectProcess $pidVal 'GUI'
      [void](Wait-ProcessGone $pidVal)
      $stoppedAny = $true
    } else {
      Write-Dev "WARNING: Skipping PID $pidVal - not a confirmed project GUI process."
    }
  }
  Clear-PidFile $script:GuiPidFile
  Start-Sleep -Milliseconds 200
  foreach ($pidVal in (Get-ListeningPids $script:GuiPort)) {
    if (Test-IsProjectGuiProcess $pidVal) {
      Write-Dev "WARNING: Project GUI PID $pidVal still listening on port $($script:GuiPort)."
    } else {
      Write-Dev "WARNING: Port $($script:GuiPort) is still in use by PID $pidVal (not this project GUI - left running)."
    }
  }
  return $stoppedAny
}

function Stop-ProjectSimProcesses {
  $stoppedAny = $false
  $simPids = @(Find-ProjectSimPids)
  if ($simPids.Count -eq 0) {
    Clear-PidFile $script:SimPidFile
    return $false
  }
  foreach ($pidVal in $simPids) {
    if (Test-IsProjectSimProcess $pidVal) {
      Stop-ProjectProcess $pidVal 'simulation_protocol'
      [void](Wait-ProcessGone $pidVal)
      $stoppedAny = $true
    } else {
      Write-Dev "WARNING: Skipping PID $pidVal - not a confirmed project simulation_protocol process."
    }
  }
  Clear-PidFile $script:SimPidFile
  Start-Sleep -Milliseconds 200
  foreach ($port in @($script:SimPort, $script:PlantPort)) {
    foreach ($pidVal in (Get-ListeningPids $port)) {
      if (Test-IsProjectSimProcess $pidVal) {
        Write-Dev "WARNING: Project simulation_protocol PID $pidVal still listening on port $port."
      } else {
        Write-Dev "WARNING: Port $port is still in use by PID $pidVal (not this project sim - left running)."
      }
    }
  }
  return $stoppedAny
}

function Start-ProjectSimulationProcess {
  if (-not (Test-Path -LiteralPath $script:SimExe)) {
    Write-Dev 'ERROR: simulation_protocol.exe not found.'
    Write-Dev 'Please build the project first.'
    Write-Dev "Expected: $($script:SimExe)"
    return $null
  }
  foreach ($port in @($script:SimPort, $script:PlantPort)) {
    if (-not (Test-PortFree $port)) {
      $pids = @(Get-ListeningPids $port) -join ', '
      Write-Dev "ERROR: Port $port is not free (PID(s): $pids)."
      return $null
    }
  }
  Write-Sim 'Starting simulation_protocol...'
  Write-Sim "M12 port $($script:SimPort), plant channel port $($script:PlantPort)"
  $simProc = Start-Process -FilePath $script:SimExe `
    -ArgumentList @("$($script:SimPort)", "$($script:PlantPort)") `
    -WorkingDirectory ([System.IO.Path]::GetDirectoryName($script:SimExe)) `
    -PassThru `
    -WindowStyle Hidden
  Save-PidFile $script:SimPidFile $simProc.Id
  Write-Sim "PID $($simProc.Id)"
  if (-not (Wait-PortReady '127.0.0.1' $script:SimPort 15 'simulation_protocol')) {
    if (Test-IsProjectSimProcess $simProc.Id) {
      Stop-ProjectProcess $simProc.Id 'simulation_protocol'
    }
    Clear-PidFile $script:SimPidFile
    return $null
  }
  Write-Sim "Port $($script:SimPort) is ready."
  return $simProc.Id
}

function Start-ProjectGuiProcess {
  if (-not (Test-PortFree $script:GuiPort)) {
    $pids = @(Get-ListeningPids $script:GuiPort) -join ', '
    Write-Dev "ERROR: Port $($script:GuiPort) is not free (PID(s): $pids)."
    return $null
  }
  Write-Gui 'Starting GUI...'
  Write-Gui 'MACHINE_CLIENT=protocol'
  Write-Gui "PROTOCOL_HOST=127.0.0.1 PROTOCOL_PORT=$($script:SimPort)"
  Write-Gui "PLANT_PORT=$($script:PlantPort)"

  $nodeCmd = Get-Command node -ErrorAction Stop
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $nodeCmd.Source
  $psi.Arguments = '"' + $script:GuiIndexJs + '"'
  $psi.WorkingDirectory = $script:GuiRoot
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow = $true
  $psi.EnvironmentVariables['MACHINE_CLIENT'] = 'protocol'
  $psi.EnvironmentVariables['PROTOCOL_HOST'] = '127.0.0.1'
  $psi.EnvironmentVariables['PROTOCOL_PORT'] = "$($script:SimPort)"
  $psi.EnvironmentVariables['PLANT_PORT'] = "$($script:PlantPort)"
  $psi.EnvironmentVariables['PORT'] = "$($script:GuiPort)"

  $guiProc = New-Object System.Diagnostics.Process
  $guiProc.StartInfo = $psi
  if (-not $guiProc.Start()) {
    Write-Dev 'ERROR: Failed to start GUI process.'
    return $null
  }
  Save-PidFile $script:GuiPidFile $guiProc.Id
  Write-Gui "PID $($guiProc.Id)"
  if (-not (Wait-PortReady '127.0.0.1' $script:GuiPort 15 'GUI')) {
    if (Test-IsProjectGuiProcess $guiProc.Id) {
      Stop-ProjectProcess $guiProc.Id 'GUI'
    }
    Clear-PidFile $script:GuiPidFile
    return $null
  }
  Write-Gui "Port $($script:GuiPort) is ready."
  return $guiProc.Id
}
