# install-hdf5.ps1 — fetch the HDF5 Windows x64 runtime for the OSF -> HDF5 exporter.
#
# Downloads the official HDF Group binary distribution, extracts the runtime
# DLLs (hdf5.dll plus the MSVC redistributable it links against) into
# .\win64\, records a SHA-256 of hdf5.dll and writes ..\VERSION.txt.
#
# The HDF Group's vs2022_cl release ships zlib statically linked inside
# hdf5.dll, so there is no separate zlib.dll to copy — H5Pset_deflate works
# out of the box.
#
# Binaries are never committed (see lib\.gitignore); rerun this script on a
# fresh checkout. Idempotent.

$ErrorActionPreference = 'Stop'

$HDF5_VERSION = '1.14.4-3'
$HDF5_URL     = 'https://github.com/HDFGroup/hdf5/releases/download/hdf5_1.14.4.3/hdf5-1.14.4-3-win-vs2022_cl.zip'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Win64Dir  = Join-Path $ScriptDir 'win64'
$WorkDir   = Join-Path $env:TEMP ('osf-hdf5-install-' + [Guid]::NewGuid().ToString('N'))
$OuterZip  = Join-Path $WorkDir 'hdf5-win-vs2022_cl.zip'
$OuterDir  = Join-Path $WorkDir 'outer'
$InnerDir  = Join-Path $WorkDir 'inner'

# Runtime DLLs to publish alongside hdf5.dll. zlib*.dll is listed so a
# distribution that ships it dynamically still works; this build links it
# statically, so its absence is not an error.
$WantedDlls = @(
    'hdf5.dll',
    'zlib.dll', 'zlib1.dll', 'libz.dll',
    'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll',
    'concrt140.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll'
)

New-Item -ItemType Directory -Force -Path $Win64Dir | Out-Null
New-Item -ItemType Directory -Force -Path $WorkDir  | Out-Null

try
{
    Write-Host "Downloading HDF5 $HDF5_VERSION (Windows x64) ..."
    Invoke-WebRequest -Uri $HDF5_URL -OutFile $OuterZip -UseBasicParsing

    Write-Host 'Extracting outer archive ...'
    Expand-Archive -Path $OuterZip -DestinationPath $OuterDir -Force

    # The HDF Group package wraps the actual distribution in a nested zip.
    $nested = Get-ChildItem -Path $OuterDir -Recurse -Filter '*.zip' -File | Select-Object -First 1
    if ($nested)
    {
        Write-Host ('Extracting nested archive ' + $nested.Name + ' ...')
        Expand-Archive -Path $nested.FullName -DestinationPath $InnerDir -Force
    }
    else
    {
        # Some packagings are flat — treat the outer extraction as the tree.
        $InnerDir = $OuterDir
    }

    $dll = Get-ChildItem -Path $InnerDir -Recurse -Filter 'hdf5.dll' -File | Select-Object -First 1
    if (-not $dll)
    {
        throw "hdf5.dll was not found inside the downloaded archive."
    }
    $binDir = $dll.DirectoryName
    Write-Host ('Found hdf5.dll in ' + $binDir)

    $copied = 0
    foreach ($name in $WantedDlls)
    {
        $src = Join-Path $binDir $name
        if (Test-Path $src)
        {
            Copy-Item -Path $src -Destination $Win64Dir -Force
            Write-Host ('  copied ' + $name)
            $copied++
        }
    }
    if ($copied -eq 0)
    {
        throw "No runtime DLLs were copied — unexpected archive layout."
    }

    $hdf5Path = Join-Path $Win64Dir 'hdf5.dll'
    $hash = (Get-FileHash -Path $hdf5Path -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -Path (Join-Path $Win64Dir 'hdf5.dll.sha256') `
                -Value ($hash + '  hdf5.dll') -Encoding ascii

    $stamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $versionText = @(
        ('HDF5 version : ' + $HDF5_VERSION),
        ('Source URL   : ' + $HDF5_URL),
        ('Installed UTC: ' + $stamp),
        ('hdf5.dll SHA256: ' + $hash)
    ) -join [Environment]::NewLine
    Set-Content -Path (Join-Path $ScriptDir 'VERSION.txt') `
                -Value $versionText -Encoding utf8

    Write-Host ''
    Write-Host ('HDF5 runtime installed to ' + $Win64Dir)
}
finally
{
    if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
}
