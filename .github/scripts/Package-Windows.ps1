[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $OutputNameLegacy = "${ProductName}-${ProductVersion}-windows-legacy-${Target}"
    $OutputNamePortableLegacy = "${ProductName}-${ProductVersion}-windows-portable-legacy-${Target}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = "${ProjectRoot}/release/${Configuration}/${ProductName}"
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    $LegacyPath = "${ProjectRoot}/release/${Configuration}/${ProductName}_legacy"

    $PortableLegacyPath = "${ProjectRoot}/release/${Configuration}/${ProductName}_portable_legacy"

    if ( Test-Path -Path $LegacyPath -PathType Container ) {
        Log-Group "Archiving ${ProductName} Legacy..."
        $CompressArgs = @{
            Path = (Get-ChildItem -Path $LegacyPath -Exclude "${OutputNameLegacy}*.*")
            CompressionLevel = 'Optimal'
            DestinationPath = "${ProjectRoot}/release/${OutputNameLegacy}.zip"
            Verbose = ($Env:CI -ne $null)
        }
        Compress-Archive -Force @CompressArgs
        Log-Group
    }

    $PortableLegacyPath = "${ProjectRoot}/release/${Configuration}/${ProductName}_portable_legacy"
    if ( Test-Path -Path $PortableLegacyPath -PathType Container ) {
        Log-Group "Archiving ${ProductName} Portable Legacy..."
        $CompressArgs = @{
            Path = (Get-ChildItem -Path $PortableLegacyPath -Exclude "${OutputNamePortableLegacy}*.*")
            CompressionLevel = 'Optimal'
            DestinationPath = "${ProjectRoot}/release/${OutputNamePortableLegacy}.zip"
            Verbose = ($Env:CI -ne $null)
        }
        Compress-Archive -Force @CompressArgs
        Log-Group
    }
}

Package
