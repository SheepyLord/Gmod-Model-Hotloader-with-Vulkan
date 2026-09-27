$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$spec=Get-Content -LiteralPath (Join-Path $root 'validation/session.json') -Raw | ConvertFrom-Json
$owned=Get-CimInstance Win32_Process -Filter "ProcessId=$($spec.pid)"
if (!$owned -or $owned.CommandLine -notlike "*-mmdhl_session $($spec.token)*") { throw 'Owned test process is no longer running' }
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public class MMDHLFocus { [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr hwnd); [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr hwnd,int cmd);}'
$window=(Get-Process -Id $spec.pid).MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'Owned test game has no window' }
[void][MMDHLFocus]::ShowWindow($window,9)
[void][MMDHLFocus]::SetForegroundWindow($window)
