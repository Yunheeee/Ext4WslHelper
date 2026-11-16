# gen-version.ps1 — read VERSION file, generate src/common/version.h
# Project root = parent of the scripts/ directory this script lives in
$ProjectDir = Split-Path -Parent $PSScriptRoot

$versionFile = Join-Path $ProjectDir 'VERSION'
$outFile     = Join-Path $ProjectDir 'src\common\version.h'

$v = (Get-Content $versionFile -Raw).Trim()

# 支持 1.2 / 1.2.3 / 1.2.3.4，以及 -beta.1、-rc.1、+build.5 等后缀。
# 后缀只影响 VERSION_STRING，不参与 FILEVERSION 的数字解析。
$versionPattern = '^(?<major>\d+)\.(?<minor>\d+)(?:\.(?<patch>\d+))?(?:\.(?<build>\d+))?(?:[-+].+)?$'
if (-not ($v -match $versionPattern)) {
    throw "Invalid VERSION format: '$v' (expected e.g. 1.2.3, 1.2.3.4, 1.2.3-beta.1)"
}

$major = [int]$Matches['major']
$minor = [int]$Matches['minor']
$patch = if ($Matches['patch']) { [int]$Matches['patch'] } else { 0 }
$build = if ($Matches['build']) { [int]$Matches['build'] } else { 0 }

# FILEVERSION/PRODUCTVERSION 必须是 4 个 16-bit 整数（PE 格式硬性要求）
# 只有 3 段则第 4 位补 0
$comma = "$major,$minor,$patch,$build"

# VERSION_STRING 忠实反映 VERSION 文件的精度：
# 1.0.0 → "1.0.0"，1.0.0.0 → "1.0.0.0"，1.0.0-beta.1 → "1.0.0-beta.1"
$string = $v

@"
// Auto-generated from VERSION file — do not edit
#pragma once
#define VERSION_MAJOR  $major
#define VERSION_MINOR  $minor
#define VERSION_PATCH  $patch
#define VERSION_BUILD  $build
#define VERSION_COMMA  $comma
#define VERSION_STRING "$string"
"@ | Set-Content -Path $outFile -Encoding ASCII

Write-Host "version.h generated: $string (from $versionFile)"
