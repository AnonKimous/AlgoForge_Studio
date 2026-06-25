param(
  [Parameter(Mandatory = $true)]
  [string]$AlgorithmSourceRoot,
  [Parameter(Mandatory = $true)]
  [string]$AlgorithmRuntimeRoot
)

$ErrorActionPreference = 'Stop'

function Get-FullPath {
  param([Parameter(Mandatory = $true)][string]$PathText)
  return [System.IO.Path]::GetFullPath($PathText)
}

function Get-RelativePath {
  param(
    [Parameter(Mandatory = $true)][string]$BasePath,
    [Parameter(Mandatory = $true)][string]$TargetPath
  )

  $normalizedBase = Get-FullPath -PathText $BasePath
  $normalizedTarget = Get-FullPath -PathText $TargetPath

  $baseWithSeparator = $normalizedBase
  if (-not $baseWithSeparator.EndsWith([System.IO.Path]::DirectorySeparatorChar) -and
      -not $baseWithSeparator.EndsWith([System.IO.Path]::AltDirectorySeparatorChar)) {
    $baseWithSeparator += [System.IO.Path]::DirectorySeparatorChar
  }

  $baseUri = New-Object System.Uri($baseWithSeparator)
  $targetUri = New-Object System.Uri($normalizedTarget)
  $relativeUri = $baseUri.MakeRelativeUri($targetUri)
  return [System.Uri]::UnescapeDataString($relativeUri.ToString()).Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

function Test-PathUnderRoot {
  param(
    [Parameter(Mandatory = $true)][string]$ChildPath,
    [Parameter(Mandatory = $true)][string]$RootPath
  )

  $normalizedChild = Get-FullPath -PathText $ChildPath
  $normalizedRoot = Get-FullPath -PathText $RootPath
  if ($normalizedChild.Length -lt $normalizedRoot.Length) {
    return $false
  }
  if (-not $normalizedChild.StartsWith($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    return $false
  }
  if ($normalizedChild.Length -eq $normalizedRoot.Length) {
    return $true
  }
  $separator = [System.IO.Path]::DirectorySeparatorChar
  return $normalizedChild[$normalizedRoot.Length] -eq $separator
}

function Test-PathUnderAnyRoot {
  param(
    [Parameter(Mandatory = $true)][string]$ChildPath,
    [Parameter(Mandatory = $true)][object[]]$RootPaths
  )

  foreach ($rootPath in $RootPaths) {
    if (Test-PathUnderRoot -ChildPath $ChildPath -RootPath ([string]$rootPath)) {
      return $true
    }
  }
  return $false
}

function Get-SafeAlgoRelativePath {
  param(
    [Parameter(Mandatory = $true)][string]$BasePath,
    [Parameter(Mandatory = $true)][string]$TargetPath
  )

  $relativePath = Get-RelativePath -BasePath $BasePath -TargetPath $TargetPath
  if ([string]::IsNullOrWhiteSpace($relativePath) -or $relativePath -eq '.') {
    throw "Algo package entry path must not be empty: $TargetPath"
  }
  if ($relativePath.StartsWith('..')) {
    throw "Algo package entry escaped package root: $TargetPath"
  }
  return $relativePath.Replace('\', '/')
}

function Write-AlgoPackage {
  param(
    [Parameter(Mandatory = $true)][string]$ArchivePath,
    [Parameter(Mandatory = $true)][string]$RootPath
  )

  $stagedFiles = @(
    Get-ChildItem -LiteralPath $RootPath -Recurse -File |
      Sort-Object FullName
  )
  if ($stagedFiles.Count -eq 0) {
    throw "Staging directory is empty while writing algo package: $RootPath"
  }

  $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
  $magic = [byte[]](0x45, 0x41, 0x4C, 0x47, 0x4F, 0x30, 0x30, 0x31)
  $buffer = New-Object byte[] (64KB)

  $archiveDir = Split-Path -Path $ArchivePath -Parent
  if (-not (Test-Path -LiteralPath $archiveDir -PathType Container)) {
    New-Item -ItemType Directory -Path $archiveDir -Force | Out-Null
  }

  $stream = [System.IO.File]::Open($ArchivePath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
  try {
    $writer = New-Object System.IO.BinaryWriter($stream, $utf8, $true)
    try {
      $writer.Write($magic)
      $writer.Write([uint32]1)
      $writer.Write([uint32]$stagedFiles.Count)

      foreach ($stagedFile in $stagedFiles) {
        $relativePath = Get-SafeAlgoRelativePath -BasePath $RootPath -TargetPath $stagedFile.FullName
        $pathBytes = $utf8.GetBytes($relativePath)
        if ($pathBytes.Length -eq 0) {
          throw "Algo package entry path encoded to empty bytes: $relativePath"
        }

        $writer.Write([uint32]$pathBytes.Length)
        $writer.Write([uint64]$stagedFile.Length)
        $writer.Write($pathBytes)
        $writer.Flush()

        $sourceStream = [System.IO.File]::Open($stagedFile.FullName, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
        try {
          while (($bytesRead = $sourceStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $stream.Write($buffer, 0, $bytesRead)
          }
        } finally {
          $sourceStream.Dispose()
        }
      }
    } finally {
      $writer.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
}

$sourceRoot = Get-FullPath -PathText $AlgorithmSourceRoot
$runtimeRoot = Get-FullPath -PathText $AlgorithmRuntimeRoot

if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
  throw "Algorithm source root does not exist: $sourceRoot"
}
if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) {
  throw "Algorithm runtime root does not exist: $runtimeRoot"
}

$manifestFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter '*_package.json' -File
if ($manifestFiles.Count -eq 0) {
  throw "No algorithm package manifests were found under $sourceRoot"
}

$packageEntries = @()
foreach ($manifestFile in $manifestFiles) {
  $sourceDir = $manifestFile.Directory.FullName
  $relativeDir = Get-RelativePath -BasePath $sourceRoot -TargetPath $sourceDir
  if ([string]::IsNullOrWhiteSpace($relativeDir) -or $relativeDir -eq '.') {
    throw "Algorithm package manifest must not live directly under the source root: $($manifestFile.FullName)"
  }

  $runtimeDir = Join-Path $runtimeRoot $relativeDir
  if (-not (Test-Path -LiteralPath $runtimeDir -PathType Container)) {
    continue
  }

  $packageEntries += [PSCustomObject]@{
    ManifestPath = $manifestFile.FullName
    SourceDir = Get-FullPath -PathText $sourceDir
    RuntimeDir = Get-FullPath -PathText $runtimeDir
    RelativeDir = $relativeDir
    PackageName = [System.IO.Path]::GetFileName($sourceDir)
  }
}

if ($packageEntries.Count -eq 0) {
  throw "No built runtime package directories were found under $runtimeRoot"
}

$allRuntimePackageDirs = @($packageEntries | ForEach-Object { $_.RuntimeDir })
$packageEntries = @($packageEntries | Sort-Object {
  ($_.RelativeDir -split '[\\/]').Count
} -Descending)

foreach ($entry in $packageEntries) {
  $runtimeDir = [string]$entry.RuntimeDir
  $sourceDir = [string]$entry.SourceDir
  $packageName = [string]$entry.PackageName
  $archivePath = Join-Path $runtimeDir ($packageName + '.algo')
  $defaultJsonPath = Join-Path $sourceDir 'default.json'

  $nestedRuntimePackageDirs = @(
    $allRuntimePackageDirs |
      Where-Object {
        $_ -ne $runtimeDir -and
        (Test-PathUnderRoot -ChildPath ([string]$_) -RootPath $runtimeDir)
      }
  )

  $stagingDir = Join-Path $runtimeRoot ('.algo_pack_' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

  try {
    Copy-Item -LiteralPath $entry.ManifestPath -Destination (Join-Path $stagingDir ([System.IO.Path]::GetFileName($entry.ManifestPath))) -Force
    if (Test-Path -LiteralPath $defaultJsonPath -PathType Leaf) {
      Copy-Item -LiteralPath $defaultJsonPath -Destination (Join-Path $stagingDir 'default.json') -Force
    }

    $runtimeFiles = Get-ChildItem -LiteralPath $runtimeDir -Recurse -File | Where-Object {
      $_.Extension -ne '.algo' -and
      ($nestedRuntimePackageDirs.Count -eq 0 -or
        -not (Test-PathUnderAnyRoot -ChildPath $_.FullName -RootPaths $nestedRuntimePackageDirs))
    }

    if ($runtimeFiles.Count -eq 0) {
      continue
    }

    foreach ($runtimeFile in $runtimeFiles) {
      $relativeFilePath = Get-RelativePath -BasePath $runtimeDir -TargetPath $runtimeFile.FullName
      if ([string]::IsNullOrWhiteSpace($relativeFilePath) -or $relativeFilePath.StartsWith('..')) {
        throw "Runtime file escaped package root while packaging '$packageName': $($runtimeFile.FullName)"
      }

      $stagedFilePath = Join-Path $stagingDir $relativeFilePath
      $stagedFileDir = Split-Path -Path $stagedFilePath -Parent
      if (-not (Test-Path -LiteralPath $stagedFileDir -PathType Container)) {
        New-Item -ItemType Directory -Path $stagedFileDir -Force | Out-Null
      }
      Copy-Item -LiteralPath $runtimeFile.FullName -Destination $stagedFilePath -Force
    }

    $stagedFiles = Get-ChildItem -LiteralPath $stagingDir -Recurse -File
    if ($stagedFiles.Count -eq 0) {
      throw "Staging directory is empty while packaging '$packageName'"
    }

    if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
      Remove-Item -LiteralPath $archivePath -Force
    }

    Write-AlgoPackage -ArchivePath $archivePath -RootPath $stagingDir

    Get-ChildItem -LiteralPath $runtimeDir -Recurse -File | Where-Object {
      $_.FullName -ne $archivePath -and
      $_.Extension -ne '.algo' -and
      ($nestedRuntimePackageDirs.Count -eq 0 -or
        -not (Test-PathUnderAnyRoot -ChildPath $_.FullName -RootPaths $nestedRuntimePackageDirs))
    } | Remove-Item -Force

    Get-ChildItem -LiteralPath $runtimeDir -Recurse -Directory |
      Sort-Object FullName -Descending |
      Where-Object {
        $_.FullName -ne $runtimeDir -and
        ($nestedRuntimePackageDirs.Count -eq 0 -or
          -not (Test-PathUnderAnyRoot -ChildPath $_.FullName -RootPaths $nestedRuntimePackageDirs))
      } |
      ForEach-Object {
        $remainingChildren = Get-ChildItem -LiteralPath $_.FullName -Force
        if ($remainingChildren.Count -eq 0) {
          Remove-Item -LiteralPath $_.FullName -Force
        }
      }
  } finally {
    if (Test-Path -LiteralPath $stagingDir) {
      Remove-Item -LiteralPath $stagingDir -Recurse -Force
    }
  }
}
