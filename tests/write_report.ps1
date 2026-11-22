$ErrorActionPreference = 'Stop'

$phases = [ordered]@{
    configure = $env:CONFIGURE_OUTCOME
    build = $env:BUILD_OUTCOME
    ctest = $env:CTEST_OUTCOME
}
$result = if (@($phases.Values | Where-Object { $_ -ne 'success' }).Count -eq 0) {
    'Passed'
} else {
    'Failed or incomplete'
}

$report = [System.Collections.Generic.List[string]]::new()
$report.AddRange([string[]]@(
    '# OnThisDayBackground Windows tests',
    '',
    "- Result: **$result**",
    "- Architecture: $env:TEST_ARCH",
    "- Commit: $env:GITHUB_SHA",
    "- Run: $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
))

foreach ($phase in $phases.Keys) {
    $report.Add('')
    $report.Add("## $phase")
    $report.Add('')
    $report.Add("Outcome: **$($phases[$phase])**")
    $log = "out/test-logs/$phase.log"
    if (Test-Path -LiteralPath $log) {
        $report.Add('')
        $lines = (Get-Content -LiteralPath $log -Tail 80) -join [Environment]::NewLine
        $report.Add("<pre>$([System.Net.WebUtility]::HtmlEncode($lines))</pre>")
    }
}

$report | Set-Content -LiteralPath test_report.md -Encoding utf8
