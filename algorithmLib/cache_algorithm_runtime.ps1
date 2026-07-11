param(
  [Parameter(Mandatory = $true)]
  [string]$AlgorithmSourceRoot,
  [Parameter(Mandatory = $true)]
  [string]$AlgorithmRuntimeRoot,
  [Parameter(Mandatory = $false)]
  [string]$AlgorithmBuildConfiguration = 'Debug'
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

function Convert-ToCIdentifier {
  param([Parameter(Mandatory = $true)][string]$Text)

  if ([string]::IsNullOrEmpty($Text)) {
    throw "C identifier source text must not be empty."
  }

  $builder = New-Object System.Text.StringBuilder
  for ($i = 0; $i -lt $Text.Length; ++$i) {
    $ch = $Text[$i]
    $isAsciiLetter =
      ($ch -ge 'a' -and $ch -le 'z') -or
      ($ch -ge 'A' -and $ch -le 'Z')
    $isAsciiDigit = ($ch -ge '0' -and $ch -le '9')
    $isUnderscore = $ch -eq '_'
    if ($i -eq 0 -and $isAsciiDigit) {
      [void]$builder.Append('_')
    }
    if ($isAsciiLetter -or $isAsciiDigit -or $isUnderscore) {
      [void]$builder.Append($ch)
    } else {
      [void]$builder.Append('_')
    }
  }

  $result = $builder.ToString()
  if ([string]::IsNullOrEmpty($result)) {
    throw "Failed to convert text to C identifier: $Text"
  }
  return $result
}

function Test-ReleaseWithDebugInfoRuntimeFileAllowed {
  param(
    [Parameter(Mandatory = $true)][string]$PackageRelativeDir,
    [Parameter(Mandatory = $true)][string]$RelativeFilePath
  )

  $normalizedPackageRelativeDir = $PackageRelativeDir.Replace('\', '/')
  $normalizedRelativeFilePath = $RelativeFilePath.Replace('\', '/')
  $fileName = [System.IO.Path]::GetFileName($normalizedRelativeFilePath)

  if ($fileName.EndsWith('.dll')) {
    return $true
  }

  if ($normalizedPackageRelativeDir.StartsWith('norm/')) {
    return $fileName.Contains('_exec.') -or $fileName.Contains('_result_render.')
  }

  if ($normalizedPackageRelativeDir.StartsWith('pipeline/')) {
    if ($normalizedPackageRelativeDir.EndsWith('_stageBegin')) {
      return $fileName.Contains('_exec.')
    }

    if ($normalizedPackageRelativeDir.EndsWith('_stageEnd')) {
      return $fileName.Contains('_exec.') -or $fileName.Contains('_result_render.')
    }

    if ($normalizedPackageRelativeDir -match '_stage[0-9]+$') {
      return $true
    }
  }

  return $true
}

function Get-AlgorithmPluginBuildSpec {
  param(
    [Parameter(Mandatory = $true)][object]$PackageEntry
  )

  $sourceDir = [string]$PackageEntry.SourceDir
  $packageName = [string]$PackageEntry.PackageName
  $relativeDir = [string]$PackageEntry.RelativeDir

  $effectivePluginSourceDir = $sourceDir
  $shaderSources = @(
    Get-ChildItem -LiteralPath $effectivePluginSourceDir -File -ErrorAction SilentlyContinue |
      Where-Object { $_.Extension -in '.vert', '.frag' }
  )
  $pluginSources = @(
    Get-ChildItem -LiteralPath $effectivePluginSourceDir -File -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -like '*_plugin.cpp' -or $_.Extension -eq '.cu' }
  )

  if ($shaderSources.Count -eq 0 -and $pluginSources.Count -eq 0) {
    $stage0Dir = Join-Path $sourceDir ($packageName + '_stage0')
    if (Test-Path -LiteralPath $stage0Dir -PathType Container) {
      $effectivePluginSourceDir = $stage0Dir
      $shaderSources = @(
        Get-ChildItem -LiteralPath $effectivePluginSourceDir -File -ErrorAction SilentlyContinue |
          Where-Object { $_.Extension -in '.vert', '.frag' }
      )
      $pluginSources = @(
        Get-ChildItem -LiteralPath $effectivePluginSourceDir -File -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -like '*_plugin.cpp' -or $_.Extension -eq '.cu' }
      )
    }
  }

  if ($pluginSources.Count -eq 0) {
    return [PSCustomObject]@{
      HasPluginTarget = $false
      RelativeDir = $relativeDir
      PackageName = $packageName
      RuntimeDir = [string]$PackageEntry.RuntimeDir
      PluginTarget = ''
      EffectivePluginSourceDir = $effectivePluginSourceDir
    }
  }

  return [PSCustomObject]@{
    HasPluginTarget = $true
    RelativeDir = $relativeDir
    PackageName = $packageName
    RuntimeDir = [string]$PackageEntry.RuntimeDir
    PluginTarget = (Convert-ToCIdentifier -Text $relativeDir) + '_plugin'
    EffectivePluginSourceDir = $effectivePluginSourceDir
  }
}

function Invoke-AlgorithmPluginBuild {
  param(
    [Parameter(Mandatory = $true)][string]$AlgorithmLibraryRoot,
    [Parameter(Mandatory = $true)][string]$AlgorithmSourceRoot,
    [Parameter(Mandatory = $true)][string]$AlgorithmRuntimeRoot,
    [Parameter(Mandatory = $true)][string]$AlgorithmBuildConfiguration,
    [Parameter(Mandatory = $true)][object[]]$BuildSpecs
  )

  $pluginSpecs = @($BuildSpecs | Where-Object { $_.HasPluginTarget })
  if ($pluginSpecs.Count -eq 0) {
    return
  }

  $ninjaExe = [string]$env:NINJA_EXE
  if ([string]::IsNullOrWhiteSpace($ninjaExe) -or -not (Test-Path -LiteralPath $ninjaExe -PathType Leaf)) {
    throw "Ninja executable is not available for algorithm plugin build: $ninjaExe"
  }

  $repoRoot = Split-Path -Path $AlgorithmLibraryRoot -Parent
  if ([string]::IsNullOrWhiteSpace($repoRoot) -or -not (Test-Path -LiteralPath $repoRoot -PathType Container)) {
    throw "Failed to resolve repository root for plugin build from '$AlgorithmLibraryRoot'."
  }

  $coreBuildDir = Join-Path $repoRoot 'build'
  if (-not (Test-Path -LiteralPath $coreBuildDir -PathType Container)) {
    throw "Core build directory does not exist for plugin build: $coreBuildDir"
  }

  $pluginBuildDir = Join-Path $env:TEMP 'algo_pkg_plugin_build'
  if (Test-Path -LiteralPath $pluginBuildDir) {
    Remove-Item -LiteralPath $pluginBuildDir -Recurse -Force
  }

  $originalCL = $env:CL
  try {
    $env:CL = '/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH'

    & cmake -G Ninja -S $AlgorithmLibraryRoot -B $pluginBuildDir --fresh `
      -DCMAKE_BUILD_TYPE="$AlgorithmBuildConfiguration" `
      -DCMAKE_MAKE_PROGRAM="$ninjaExe" `
      -DBUILD_ALGORITHM_SAMPLE_PLUGIN=ON `
      -DCORE_BUILD_DIR="$coreBuildDir" `
      -DALGORITHM_LIBRARY_SOURCE_ROOT="$AlgorithmSourceRoot" `
      -DALGORITHM_LIBRARY_RUNTIME_OUTPUT_ROOT="$AlgorithmRuntimeRoot"
    if ($LASTEXITCODE -ne 0) {
      throw "Failed to configure algorithm plugin packaging build."
    }

    $builtTargets = @{}
    foreach ($pluginSpec in $pluginSpecs) {
      $pluginTarget = [string]$pluginSpec.PluginTarget
      if ([string]::IsNullOrWhiteSpace($pluginTarget) -or $builtTargets.ContainsKey($pluginTarget)) {
        continue
      }
      & cmake --build $pluginBuildDir --target $pluginTarget --parallel
      if ($LASTEXITCODE -ne 0) {
        throw "Failed to build algorithm plugin target '$pluginTarget'."
      }
      $builtTargets[$pluginTarget] = $true
    }
  } finally {
    if ($null -eq $originalCL) {
      Remove-Item Env:CL -ErrorAction SilentlyContinue
    } else {
      $env:CL = $originalCL
    }
    if (Test-Path -LiteralPath $pluginBuildDir) {
      Remove-Item -LiteralPath $pluginBuildDir -Recurse -Force
    }
  }
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
$algorithmLibraryRoot = Split-Path -Path $sourceRoot -Parent

if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
  throw "Algorithm source root does not exist: $sourceRoot"
}
if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) {
  throw "Algorithm runtime root does not exist: $runtimeRoot"
}

$pluginHeaderSource = Join-Path $sourceRoot 'algorithm_plugin_api.h'
if (-not (Test-Path -LiteralPath $pluginHeaderSource -PathType Leaf)) {
  throw "Algorithm plugin API header does not exist: $pluginHeaderSource"
}
Copy-Item -LiteralPath $pluginHeaderSource -Destination (Join-Path $runtimeRoot 'algorithm_plugin_api.h') -Force

$manifestFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -Filter 'manifest.json' -File
if ($manifestFiles.Count -eq 0) {
  throw "No algorithm manifests were found under $sourceRoot"
}

$packageEntries = @()
foreach ($manifestFile in $manifestFiles) {
  $sourceDir = $manifestFile.Directory.FullName
  $relativeDir = Get-RelativePath -BasePath $sourceRoot -TargetPath $sourceDir
  if ([string]::IsNullOrWhiteSpace($relativeDir) -or $relativeDir -eq '.') {
    throw "Algorithm algo manifest must not live directly under the source root: $($manifestFile.FullName)"
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
  throw "No built runtime algo directories were found under $runtimeRoot"
}

$packageBuildSpecs = @($packageEntries | ForEach-Object { Get-AlgorithmPluginBuildSpec -PackageEntry $_ })
Invoke-AlgorithmPluginBuild `
  -AlgorithmLibraryRoot $algorithmLibraryRoot `
  -AlgorithmSourceRoot $sourceRoot `
  -AlgorithmRuntimeRoot $runtimeRoot `
  -AlgorithmBuildConfiguration $AlgorithmBuildConfiguration `
  -BuildSpecs $packageBuildSpecs

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

  $stagingDir = Join-Path ([System.IO.Path]::GetTempPath()) ('.algo_pack_' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

  try {
    Copy-Item -LiteralPath $entry.ManifestPath -Destination (Join-Path $runtimeDir 'manifest.json') -Force
    if (Test-Path -LiteralPath $defaultJsonPath -PathType Leaf) {
      Copy-Item -LiteralPath $defaultJsonPath -Destination (Join-Path $runtimeDir 'default.json') -Force
    }

    Copy-Item -LiteralPath $entry.ManifestPath -Destination (Join-Path $stagingDir 'manifest.json') -Force
    if (Test-Path -LiteralPath $defaultJsonPath -PathType Leaf) {
      Copy-Item -LiteralPath $defaultJsonPath -Destination (Join-Path $stagingDir 'default.json') -Force
    }

    $packageDllCandidates = @(
      (Join-Path (Join-Path $runtimeDir 'Debug') ($packageName + '.dll'))
      (Join-Path (Join-Path $runtimeDir 'RelWithDebInfo') ($packageName + '.dll'))
      (Join-Path $runtimeDir ($packageName + '.dll'))
    )
    $resolvedPackageDllPath = $null
    foreach ($packageDllCandidate in $packageDllCandidates) {
      if (Test-Path -LiteralPath $packageDllCandidate -PathType Leaf) {
        $resolvedPackageDllPath = $packageDllCandidate
        break
      }
    }
    if ($null -eq $resolvedPackageDllPath) {
      throw "Runtime package '$packageName' is missing its plugin DLL under '$runtimeDir'."
    }

    $runtimeFiles = Get-ChildItem -LiteralPath $runtimeDir -Recurse -File | Where-Object {
      $_.Extension -ne '.algo' -and
      $_.Extension -notin '.exp', '.lib', '.pdb', '.ilk', '.obj', '.manifest' -and
      $_.FullName -notmatch '[\\/](algocache)[\\/]+' -and
      ($nestedRuntimePackageDirs.Count -eq 0 -or
        -not (Test-PathUnderAnyRoot -ChildPath $_.FullName -RootPaths $nestedRuntimePackageDirs)) -and
      (
        $AlgorithmBuildConfiguration -ne 'RelWithDebInfo' -or
        (Test-ReleaseWithDebugInfoRuntimeFileAllowed `
          -PackageRelativeDir $entry.RelativeDir `
          -RelativeFilePath (Get-RelativePath -BasePath $runtimeDir -TargetPath $_.FullName))
      )
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

  } finally {
    if (Test-Path -LiteralPath $stagingDir) {
      Remove-Item -LiteralPath $stagingDir -Recurse -Force
    }
  }
}
