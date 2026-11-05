#ifndef POTATO_EDITOR_UPDATE_UTILS_H
#define POTATO_EDITOR_UPDATE_UTILS_H

#include <QString>

namespace UpdateUtils {
inline QString linuxInstallerScript() {
    return QString::fromUtf8(R"SH(#!/bin/bash
set -eu
staged=$1
target=$2
parent_pid=$3
attempt=0
while kill -0 "$parent_pid" 2>/dev/null; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 120 ] || exit 1
    sleep 1
done
if [ ! -f "$staged" ] || [ ! -s "$staged" ] || [ ! -f "$target" ]; then
    exit 1
fi
chmod --reference="$target" -- "$staged"
chmod u+x -- "$staged"
# Both files are in the same directory: replace atomically at the exact path.
mv -fT -- "$staged" "$target"
rm -- "$0"
cd -- "$(dirname -- "$target")"
exec "$target"
)SH");
}

inline QString windowsInstallerScript() {
    return QString::fromUtf8(R"PS(param([string]$staged, [string]$target, [int]$parentPid)
$ErrorActionPreference = 'Stop'
$parentProcess = Get-Process -Id $parentPid -ErrorAction SilentlyContinue
if ($parentProcess) { Wait-Process -Id $parentPid -Timeout 120 }
$folder = [System.IO.Path]::GetDirectoryName($target)
$targetName = [System.IO.Path]::GetFileName($target)
$expanded = $staged + '.contents'
Expand-Archive -LiteralPath $staged -DestinationPath $expanded -Force
$packagedExe = Join-Path $expanded 'PotatoEditor.exe'
if (!(Test-Path -LiteralPath $packagedExe -PathType Leaf)) {
    throw 'Update archive is missing PotatoEditor.exe'
}
$renamedExe = Join-Path $expanded $targetName
if ($targetName -cne 'PotatoEditor.exe') {
    # Use an intermediate name so case-only renames work on Windows as well.
    $intermediate = Join-Path $expanded ([System.Guid]::NewGuid().ToString() + '.tmp')
    Move-Item -LiteralPath $packagedExe -Destination $intermediate
    Move-Item -LiteralPath $intermediate -Destination $renamedExe
}
Get-ChildItem -LiteralPath $expanded -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $folder -Recurse -Force
}
Remove-Item -LiteralPath $expanded -Recurse -Force
Remove-Item -LiteralPath $staged -Force
Start-Process -FilePath $target -WorkingDirectory $folder
Remove-Item -LiteralPath $PSCommandPath -Force
)PS");
}
} // namespace UpdateUtils
#endif
