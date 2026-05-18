# Generate fixture artifacts for Phase 4 download tests. Idempotent: rerun
# overwrites the existing fixtures so tests stay deterministic. Outputs land
# under tests/fixtures/staging/.

[CmdletBinding()]
param(
	# Optional output root. Defaults to tests/fixtures/staging/ next to the
	# script.
	[string] $OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
if (-not $OutputRoot) {
	$OutputRoot = Join-Path $ScriptRoot 'staging'
}

# Wipe + recreate the output root so SHAs are reproducible across runs.
if (Test-Path -LiteralPath $OutputRoot) {
	Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Write-UTF8NoBom {
	param([string] $Path, [string] $Content)
	$enc = New-Object System.Text.UTF8Encoding($false)
	$dir = Split-Path -Parent $Path
	if ($dir -and -not (Test-Path -LiteralPath $dir)) {
		New-Item -ItemType Directory -Force -Path $dir | Out-Null
	}
	[System.IO.File]::WriteAllText($Path, $Content, $enc)
}

function New-RandomBytes {
	param([string] $Path, [int] $Length, [int] $Seed)
	$rand = [System.Random]::new($Seed)
	$bytes = New-Object 'byte[]' $Length
	$rand.NextBytes($bytes)
	$dir = Split-Path -Parent $Path
	if ($dir -and -not (Test-Path -LiteralPath $dir)) {
		New-Item -ItemType Directory -Force -Path $dir | Out-Null
	}
	[System.IO.File]::WriteAllBytes($Path, $bytes)
}

function Get-Sha {
	param([string] $Path)
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# 1. Captions pack fixtures: small binary blobs with stable SHAs.
$captionsDir = Join-Path $OutputRoot 'captions'
New-RandomBytes -Path (Join-Path $captionsDir 'ggml-stub.bin')     -Length 4096 -Seed 1
New-RandomBytes -Path (Join-Path $captionsDir 'silero-stub.onnx')  -Length 4096 -Seed 2
New-RandomBytes -Path (Join-Path $captionsDir 'onnxruntime-stub.dll') -Length 4096 -Seed 3

$ggmlSha    = Get-Sha (Join-Path $captionsDir 'ggml-stub.bin')
$sileroSha  = Get-Sha (Join-Path $captionsDir 'silero-stub.onnx')
$ortDllSha  = Get-Sha (Join-Path $captionsDir 'onnxruntime-stub.dll')

# 2. Captions test manifest. Points each file at a placeholder URL that the
# fixture HTTP server replaces with a 127.0.0.1 route at startup. The runner
# rewrites %FIXTURE_BASE% to http://127.0.0.1:<port>/ before invoking
# install-captions-pack.ps1.
$captionsPacks = @{
	'$schema' = 'wkopenvr-captions-packs/v1'
	packs = @(
		[ordered]@{
			id          = 'harness-base-en'
			label       = 'Harness fixture: English base'
			description = 'Synthesized fixture used by the WKOpenVR test harness; not a real model.'
			files = @(
				[ordered]@{
					name        = 'models/ggml-base.bin'
					url         = '%FIXTURE_BASE%captions/ggml-stub.bin'
					sha256      = $ggmlSha
					destination = 'models\ggml-base.bin'
				},
				[ordered]@{
					name        = 'models/silero_vad.onnx'
					url         = '%FIXTURE_BASE%captions/silero-stub.onnx'
					sha256      = $sileroSha
					destination = 'models\silero_vad.onnx'
				},
				[ordered]@{
					name        = 'runtime/onnxruntime.dll'
					url         = '%FIXTURE_BASE%captions/onnxruntime-stub.dll'
					sha256      = $ortDllSha
					destination = 'runtime\onnxruntime.dll'
				}
			)
		}
	)
}
Write-UTF8NoBom -Path (Join-Path $OutputRoot 'captions-test-packs.json') `
	-Content ($captionsPacks | ConvertTo-Json -Depth 8)

# 3. VRCFT fixture module. Folder containing manifest.json + module.dll bytes.
$vrcftDir  = Join-Path $OutputRoot 'vrcft-stub'
$dllPath   = Join-Path $vrcftDir 'WKOpenVR.FaceTracking.Stub.dll'
New-RandomBytes -Path $dllPath -Length 8192 -Seed 4
$dllSha    = Get-Sha $dllPath

$moduleUuid    = 'f1c7a000-0001-4f00-8000-fb7651000001'
$moduleVersion = '0.1.0'
$manifest = [ordered]@{
	uuid    = $moduleUuid
	version = $moduleVersion
	name    = 'WKOpenVR Test-Harness Stub Module'
	files   = @(
		[ordered]@{
			path   = 'WKOpenVR.FaceTracking.Stub.dll'
			sha256 = $dllSha
		}
	)
}
Write-UTF8NoBom -Path (Join-Path $vrcftDir 'manifest.json') `
	-Content ($manifest | ConvertTo-Json -Depth 6)

# 4. Registry-style payload: zip of the vrcft-stub folder + a manifest that
# includes the payload SHA. The fixture server serves this zip at a known
# path; face-module-sync.ps1 downloads it via -Kind registry.
$registryDir = Join-Path $OutputRoot 'registry'
New-Item -ItemType Directory -Force -Path $registryDir | Out-Null
$zipPath = Join-Path $registryDir "$moduleUuid-$moduleVersion.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Compress-Archive -Path (Join-Path $vrcftDir '*') -DestinationPath $zipPath -CompressionLevel Fastest
$zipSha = Get-Sha $zipPath

$registryIndex = [ordered]@{
	'$schema' = 'wkvrcft-legacy-registry/v1'
	modules = @(
		[ordered]@{
			uuid       = $moduleUuid
			latest     = $moduleVersion
			source_id  = 'wkopenvr-harness-stub'
			label      = 'WKOpenVR harness stub'
			versions = @(
				[ordered]@{
					version       = $moduleVersion
					payload_url   = "%FIXTURE_BASE%registry/$moduleUuid-$moduleVersion.zip"
					payload_sha256 = $zipSha
				}
			)
		}
	)
}
Write-UTF8NoBom -Path (Join-Path $registryDir 'index.json') `
	-Content ($registryIndex | ConvertTo-Json -Depth 8)

# 5. Emit a small summary the test runner can read to know the fixture paths
# without re-deriving them.
$summary = [ordered]@{
	output_root             = $OutputRoot
	captions_manifest       = (Join-Path $OutputRoot 'captions-test-packs.json')
	captions_payload_dir    = $captionsDir
	captions_pack_id        = 'harness-base-en'
	captions_ggml_sha       = $ggmlSha
	captions_silero_sha     = $sileroSha
	captions_ort_sha        = $ortDllSha
	vrcft_module_dir        = $vrcftDir
	vrcft_module_uuid       = $moduleUuid
	vrcft_module_version    = $moduleVersion
	vrcft_module_dll_sha    = $dllSha
	registry_index          = (Join-Path $registryDir 'index.json')
	registry_zip            = $zipPath
	registry_zip_sha        = $zipSha
}
Write-UTF8NoBom -Path (Join-Path $OutputRoot 'fixtures.summary.json') `
	-Content ($summary | ConvertTo-Json -Depth 4)

Write-Host "Fixtures generated under $OutputRoot"
Write-Host "  captions manifest: $($summary.captions_manifest)"
Write-Host "  vrcft module dir:  $($summary.vrcft_module_dir)"
Write-Host "  registry zip:      $($summary.registry_zip)"
