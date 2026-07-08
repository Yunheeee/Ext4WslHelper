# gen-version.ps1 — read VERSION file, generate src/common/version.h
# Project root = parent of the scripts/ directory this script lives in
$ProjectDir = Split-Path -Parent $PSScriptRoot

$versionFile = Join-Path $ProjectDir 'VERSION'
$outFile     = Join-Path $ProjectDir 'src\common\version.h'

$v = (Get-Content $versionFile -Raw).Trim()
$parts = $v -split '\.'
$major = [int]$parts[0]
$minor = if ($parts.Count -gt 1) { [int]$parts[1] } else { 0 }
$patch = if ($parts.Count -gt 2) { [int]$parts[2] } else { 0 }
$build = if ($parts.Count -gt 3) { [int]$parts[3] } else { 0 }

# FILEVERSION/PRODUCTVERSION 必须是 4 个 16-bit 整数（PE 格式硬性要求）
# 只有 3 段则第 4 位补 0
$comma = "$major,$minor,$patch,$build"

# VERSION_STRING 忠实反映 VERSION 文件的精度：1.0.0 → "1.0.0"，1.0.0.0 → "1.0.0.0"
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
