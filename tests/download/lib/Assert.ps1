# Tiny assertion helpers used by Phase 4 download tests. Loaded via dot-source
# (`. .\lib\Assert.ps1`). Each helper throws on failure with a descriptive
# message; the runner's outer try/catch maps that to a failed case.

function Assert-True {
	param([Parameter(Mandatory = $true)][bool] $Condition,
		  [Parameter(Mandatory = $true)][string] $Message)
	if (-not $Condition) {
		throw "ASSERT-TRUE failed: $Message"
	}
}

function Assert-FileExists {
	param([Parameter(Mandatory = $true)][string] $Path,
		  [string] $Label = '')
	if (-not (Test-Path -LiteralPath $Path)) {
		$tag = if ($Label) { "$Label ($Path)" } else { $Path }
		throw "ASSERT-FILE-EXISTS failed: $tag is missing"
	}
}

function Assert-FileMissing {
	param([Parameter(Mandatory = $true)][string] $Path,
		  [string] $Label = '')
	if (Test-Path -LiteralPath $Path) {
		$tag = if ($Label) { "$Label ($Path)" } else { $Path }
		throw "ASSERT-FILE-MISSING failed: $tag exists but should not"
	}
}

function Assert-Sha256Matches {
	param([Parameter(Mandatory = $true)][string] $Path,
		  [Parameter(Mandatory = $true)][string] $ExpectedSha)
	if (-not (Test-Path -LiteralPath $Path)) {
		throw "ASSERT-SHA256 failed: $Path is missing"
	}
	$actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
	$expected = $ExpectedSha.ToLowerInvariant()
	if ($actual -ne $expected) {
		throw "ASSERT-SHA256 failed: $Path expected=$expected actual=$actual"
	}
}

function Assert-ThrowsWith {
	param([Parameter(Mandatory = $true)][scriptblock] $Action,
		  [Parameter(Mandatory = $true)][string] $ContainsPattern,
		  [Parameter(Mandatory = $true)][string] $CaseLabel)
	try {
		& $Action | Out-Null
	} catch {
		$msg = $_.Exception.Message
		if ($msg -notmatch [regex]::Escape($ContainsPattern)) {
			throw "ASSERT-THROWS-WITH ($CaseLabel) failed: message did not match '$ContainsPattern' -- got: $msg"
		}
		return
	}
	throw "ASSERT-THROWS-WITH ($CaseLabel) failed: action did not throw"
}

function Write-CaseStart {
	param([Parameter(Mandatory = $true)][string] $Name)
	Write-Host ""
	Write-Host "--- case: $Name" -ForegroundColor Cyan
}

function Write-CasePass {
	param([Parameter(Mandatory = $true)][string] $Name)
	Write-Host "    PASS $Name" -ForegroundColor Green
}
