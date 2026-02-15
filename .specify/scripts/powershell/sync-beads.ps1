#!/usr/bin/env pwsh

# Sync speckit tasks.md to beads issue tracker
#
# This script parses a speckit tasks.md file and creates/updates beads issues
# to maintain a tight coupling between spec-driven planning and execution tracking.
#
# Granularity:
#   - Phase (## Phase N) -> beads epic
#   - Subtask Group (### N.M) -> beads task (child of phase epic)
#   - Individual T-items -> tracked via checkbox state in markdown (not as beads issues)
#
# Status derivation:
#   - All tasks [ ] -> open
#   - Mix of [X] and [ ] -> in_progress
#   - All tasks [X] -> closed
#
# Idempotent: re-running updates existing beads issues based on a .beads-sync.json manifest.
#
# Usage: ./sync-beads.ps1 [OPTIONS]
#
# OPTIONS:
#   -TasksFile <path>   Path to tasks.md (default: auto-detect from current feature)
#   -DryRun             Show what would be done without executing
#   -Json               Output results in JSON format
#   -Force              Re-create issues even if manifest exists
#   -Help, -h           Show help message

[CmdletBinding()]
param(
    [string]$TasksFile,
    [switch]$DryRun,
    [switch]$Json,
    [switch]$Force,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Output @"
Usage: sync-beads.ps1 [OPTIONS]

Sync speckit tasks.md to beads issue tracker.

OPTIONS:
  -TasksFile <path>   Path to tasks.md (default: auto-detect from current feature)
  -DryRun             Show what would be done without executing
  -Json               Output results in JSON format
  -Force              Re-create issues even if manifest exists
  -Help, -h           Show this help message

EXAMPLES:
  # Sync current feature's tasks to beads
  .\sync-beads.ps1

  # Preview what would happen
  .\sync-beads.ps1 -DryRun

  # Sync a specific tasks file
  .\sync-beads.ps1 -TasksFile specs/055-global-filter/tasks.md

"@
    exit 0
}

# Source common functions if available
$commonPath = "$PSScriptRoot/common.ps1"
if (Test-Path $commonPath) {
    . $commonPath
}

# ---------------------------------------------------------------------------
# Utility: run bd and return parsed JSON
# ---------------------------------------------------------------------------
function Invoke-Bd {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )
    $allArgs = $Arguments + @('--json')
    # Suppress stderr warnings (e.g. "Creating issue without description") from
    # becoming PowerShell ErrorRecords that terminate the script
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    $raw = & bd @allArgs 2>&1 | Where-Object { $_ -isnot [System.Management.Automation.ErrorRecord] }
    $ErrorActionPreference = $prevEAP
    if ($LASTEXITCODE -ne 0) {
        Write-Error "bd $($Arguments -join ' ') failed (exit code $LASTEXITCODE): $raw"
        return $null
    }
    try {
        return $raw | ConvertFrom-Json
    } catch {
        # Some bd commands don't return JSON even with --json
        return $raw
    }
}

# ---------------------------------------------------------------------------
# Parsing: extract structured data from tasks.md
# ---------------------------------------------------------------------------
function ConvertFrom-TasksFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $content = Get-Content -Path $Path -Raw
    $lines = $content -split "`n"

    $script:specName = ''
    $script:phases = [System.Collections.ArrayList]::new()
    $script:phaseDeps = [System.Collections.ArrayList]::new()

    $script:currentPhase = $null
    $script:currentGroups = [System.Collections.ArrayList]::new()
    $script:currentGroupNumber = $null
    $script:currentGroupTitle = $null
    $script:currentTasks = [System.Collections.ArrayList]::new()
    $script:currentCheckpoint = $null
    $script:inDependencySection = $false

    function Clear-CurrentGroup {
        if ($null -ne $script:currentGroupNumber -and $null -ne $script:currentGroupTitle) {
            [void]$script:currentGroups.Add(@{
                Number = $script:currentGroupNumber
                Title  = $script:currentGroupTitle
                Tasks  = @($script:currentTasks)
            })
        }
        $script:currentGroupNumber = $null
        $script:currentGroupTitle = $null
        $script:currentTasks = [System.Collections.ArrayList]::new()
    }

    function Clear-CurrentPhase {
        Clear-CurrentGroup
        if ($null -ne $script:currentPhase) {
            $script:currentPhase.SubtaskGroups = @($script:currentGroups)
            $script:currentPhase.Checkpoint = $script:currentCheckpoint
            [void]$script:phases.Add($script:currentPhase)
        }
        $script:currentPhase = $null
        $script:currentGroups = [System.Collections.ArrayList]::new()
        $script:currentCheckpoint = $null
    }

    foreach ($line in $lines) {
        $line = $line.TrimEnd()

        # Top-level spec name
        if ($line -match '^# Tasks:\s*(.+)$' -and $script:specName -eq '') {
            $script:specName = $Matches[1].Trim()
            continue
        }

        # Dependency section
        if ($line -match '^## Dependencies & Execution Order$') {
            Clear-CurrentPhase
            $script:inDependencySection = $true
            continue
        }

        # Parse dependency lines
        if ($script:inDependencySection) {
            # Stop at next non-dependency section
            if ($line -match '^## ' -and $line -notmatch 'Phase Dependencies|User Story Dependencies') {
                $script:inDependencySection = $false
            }
            elseif ($line -match '^\*\*.*Phase (\d+).*\*\*:.*(?:[Dd]epends on)\s+(.+)') {
                $depPhase = [int]$Matches[1]
                $depText = $Matches[2]
                $refs = [System.Collections.ArrayList]::new()
                $refMatches = [regex]::Matches($depText, 'Phase (\d+)')
                foreach ($m in $refMatches) {
                    $ref = [int]$m.Groups[1].Value
                    if ($ref -ne $depPhase -and $refs -notcontains $ref) {
                        [void]$refs.Add($ref)
                    }
                }
                if ($refs.Count -gt 0) {
                    [void]$script:phaseDeps.Add(@{
                        Phase     = $depPhase
                        DependsOn = @($refs)
                    })
                }
            }
            continue
        }

        # Phase heading: ## Phase N: Title
        if ($line -match '^## Phase (\d+):\s*(.+)$') {
            Clear-CurrentPhase

            $phaseNum = [int]$Matches[1]
            $rawTitle = $Matches[2].Trim()

            $priority = $null
            if ($rawTitle -match '\(Priority:\s*P(\d)\)') {
                $priority = [int]$Matches[1]
            }

            $userStory = $null
            if ($rawTitle -match 'User Story (\d+)') {
                $userStory = "US$($Matches[1])"
            }

            $cleanTitle = $rawTitle `
                -replace '\(Priority:\s*P\d\)', '' `
                -replace [char]::ConvertFromUtf32(0x1F3AF), '' `
                -replace 'MVP', '' `
                -replace '\s+', ' ' |
                ForEach-Object { $_.Trim() }

            $script:currentPhase = @{
                Number         = $phaseNum
                Title          = $cleanTitle
                FullTitle      = "Phase ${phaseNum}: ${rawTitle}"
                Priority       = $priority
                UserStory      = $userStory
                SubtaskGroups  = @()
                Checkpoint     = $null
            }
            continue
        }

        # Subtask group heading: ### N.M Title
        if ($line -match '^### (\d+\.\d+)\s+(.+)$' -and $null -ne $script:currentPhase) {
            Clear-CurrentGroup
            $script:currentGroupNumber = $Matches[1]
            $script:currentGroupTitle = $Matches[2].Trim()
            continue
        }

        # Task line: - [X] T001 [P] [US1] Description
        if ($line -match '^\s*- \[([ Xx])\]\s+(T\d+)\s*(\[P\])?\s*(?:\[(US\d+)\])?\s*(.+)$' -and $null -ne $script:currentPhase) {
            $task = @{
                Id          = $Matches[2]
                Description = $Matches[5].Trim()
                State       = if ($Matches[1] -match '[Xx]') { 'checked' } else { 'unchecked' }
                Parallel    = $null -ne $Matches[3]
                UserStory   = $Matches[4]  # may be $null
            }
            [void]$script:currentTasks.Add($task)
            continue
        }

        # Checkpoint
        if ($line -match '^\*\*Checkpoint\*\*:\s*(.+)$') {
            $script:currentCheckpoint = $Matches[1].Trim()
            continue
        }
    }

    Clear-CurrentPhase

    return @{
        SpecName         = $script:specName
        SpecPath         = $Path
        Phases           = @($script:phases)
        PhaseDependencies = @($script:phaseDeps)
    }
}

# ---------------------------------------------------------------------------
# Status computation
# ---------------------------------------------------------------------------
function Get-GroupStatus {
    param([hashtable]$Group)
    if ($Group.Tasks.Count -eq 0) { return 'open' }
    $checked = ($Group.Tasks | Where-Object { $_.State -eq 'checked' }).Count
    if ($checked -eq 0) { return 'open' }
    if ($checked -eq $Group.Tasks.Count) { return 'closed' }
    return 'in_progress'
}

function Get-PhaseStatus {
    param([hashtable]$Phase)
    if ($Phase.SubtaskGroups.Count -eq 0) { return 'open' }
    $statuses = $Phase.SubtaskGroups | ForEach-Object { Get-GroupStatus -Group $_ }
    if (($statuses | Where-Object { $_ -ne 'closed' }).Count -eq 0) { return 'closed' }
    if (($statuses | Where-Object { $_ -ne 'open' }).Count -eq 0) { return 'open' }
    return 'in_progress'
}

function Get-PhaseLabels {
    param([hashtable]$Phase)
    $labels = [System.Collections.Generic.HashSet[string]]::new()
    if ($Phase.UserStory) { [void]$labels.Add($Phase.UserStory) }
    foreach ($group in $Phase.SubtaskGroups) {
        foreach ($task in $group.Tasks) {
            if ($task.UserStory) { [void]$labels.Add($task.UserStory) }
        }
    }
    return @($labels)
}

# ---------------------------------------------------------------------------
# Manifest management (.beads-sync.json)
# ---------------------------------------------------------------------------
function Get-ManifestPath {
    param([string]$TasksFilePath)
    $dir = Split-Path -Parent $TasksFilePath
    return Join-Path $dir '.beads-sync.json'
}

function Read-Manifest {
    param([string]$ManifestPath)
    if (Test-Path $ManifestPath) {
        $json = Get-Content -Path $ManifestPath -Raw | ConvertFrom-Json
        # Convert PSCustomObject to hashtable for compatibility with PS 5.1
        # (ConvertFrom-Json -AsHashtable requires PS 6.0+)
        return ConvertTo-Hashtable -InputObject $json
    }
    return $null
}

function ConvertTo-Hashtable {
    param([Parameter(Mandatory)][object]$InputObject)
    if ($InputObject -is [System.Collections.IDictionary]) { return $InputObject }
    if ($InputObject -is [pscustomobject]) {
        $ht = @{}
        foreach ($prop in $InputObject.PSObject.Properties) {
            $ht[$prop.Name] = ConvertTo-Hashtable -InputObject $prop.Value
        }
        return $ht
    }
    if ($InputObject -is [System.Collections.IEnumerable] -and $InputObject -isnot [string]) {
        return @($InputObject | ForEach-Object { ConvertTo-Hashtable -InputObject $_ })
    }
    return $InputObject
}

function Write-Manifest {
    param(
        [string]$ManifestPath,
        [hashtable]$Manifest
    )
    $Manifest | ConvertTo-Json -Depth 10 | Set-Content -Path $ManifestPath -Encoding UTF8
}

# ---------------------------------------------------------------------------
# Beads sync logic
# ---------------------------------------------------------------------------
function Sync-ToBeads {
    param(
        [Parameter(Mandatory)]
        [hashtable]$Parsed,

        [string]$ManifestPath,
        [switch]$DryRun
    )

    $manifest = $null
    $isUpdate = $false

    if (-not $Force -and (Test-Path $ManifestPath)) {
        $manifest = Read-Manifest -ManifestPath $ManifestPath
        if ($null -ne $manifest) {
            $isUpdate = $true
        }
    }

    if (-not $isUpdate) {
        $manifest = @{
            SpecPath     = $Parsed.SpecPath
            LastSyncedAt = $null
            EpicId       = $null
            Phases       = @{}
        }
    }

    $results = [System.Collections.ArrayList]::new()

    # --- Create or locate top-level epic for the spec ---
    if (-not $manifest.EpicId) {
        $epicTitle = $Parsed.SpecName
        if ($DryRun) {
            Write-Host "[DRY RUN] Would create epic: $epicTitle"
            $manifest.EpicId = 'dry-run-epic'
        } else {
            $result = Invoke-Bd -Arguments @('create', $epicTitle, '-t', 'epic', '-p', '1')
            if ($null -eq $result) {
                Write-Error "Failed to create top-level epic"
                return $null
            }
            $manifest.EpicId = $result.id
            Write-Host "Created epic: $($result.id) - $epicTitle"
        }
    }

    # --- Process each phase ---
    foreach ($phase in $Parsed.Phases) {
        $phaseKey = "$($phase.Number)"
        $phaseStatus = Get-PhaseStatus -Phase $phase
        $phaseLabels = Get-PhaseLabels -Phase $phase
        $priority = if ($null -ne $phase.Priority) { $phase.Priority } else { 2 }
        $labelStr = ($phaseLabels + @("phase-$($phase.Number)")) -join ','

        $phaseEntry = $null
        if ($manifest.Phases.ContainsKey($phaseKey)) {
            $phaseEntry = $manifest.Phases[$phaseKey]
        }

        if ($null -eq $phaseEntry) {
            # Create phase epic
            $phaseTitle = $phase.FullTitle
            if ($DryRun) {
                Write-Host "[DRY RUN] Would create phase epic: $phaseTitle (P$priority, status=$phaseStatus, labels=$labelStr)"
                $phaseBeadsId = "dry-run-phase-$($phase.Number)"
            } else {
                $result = Invoke-Bd -Arguments @(
                    'create', $phaseTitle,
                    '-t', 'epic',
                    '-p', "$priority",
                    '-l', $labelStr
                )
                if ($null -eq $result) {
                    Write-Error "Failed to create phase epic for phase $($phase.Number)"
                    continue
                }
                $phaseBeadsId = $result.id
                Write-Host "Created phase: $phaseBeadsId - $phaseTitle"

                # Set parent dependency to top-level epic
                Invoke-Bd -Arguments @('dep', 'add', $phaseBeadsId, $manifest.EpicId, '--type', 'parent-child') | Out-Null
            }

            $phaseEntry = @{
                BeadsId        = $phaseBeadsId
                SubtaskGroups  = @{}
            }
            $manifest.Phases[$phaseKey] = $phaseEntry
        } else {
            # Update existing phase
            $phaseBeadsId = $phaseEntry.BeadsId
            if (-not $DryRun) {
                if ($phaseStatus -eq 'closed') {
                    Invoke-Bd -Arguments @('close', $phaseBeadsId, '--reason', "All subtasks complete") | Out-Null
                    Write-Host "Closed phase: $phaseBeadsId"
                } elseif ($phaseStatus -eq 'in_progress') {
                    Invoke-Bd -Arguments @('update', $phaseBeadsId, '--status', 'in_progress') | Out-Null
                    Write-Host "Updated phase: $phaseBeadsId -> in_progress"
                }
            } else {
                Write-Host "[DRY RUN] Would update phase $phaseBeadsId -> status=$phaseStatus"
            }
        }

        # --- Process subtask groups within phase ---
        foreach ($group in $phase.SubtaskGroups) {
            $groupKey = $group.Number
            $groupStatus = Get-GroupStatus -Group $group
            $checkedCount = ($group.Tasks | Where-Object { $_.State -eq 'checked' }).Count
            $totalCount = $group.Tasks.Count
            $progressDesc = "[$checkedCount/$totalCount tasks complete]"

            $groupBeadsId = $null
            if ($phaseEntry.SubtaskGroups.ContainsKey($groupKey)) {
                $groupBeadsId = $phaseEntry.SubtaskGroups[$groupKey]
            }

            if ($null -eq $groupBeadsId) {
                # Create subtask group
                $groupTitle = "$($group.Number) $($group.Title)"
                if ($DryRun) {
                    Write-Host "[DRY RUN]   Would create task: $groupTitle (status=$groupStatus, $progressDesc)"
                    $groupBeadsId = "dry-run-group-$($group.Number)"
                } else {
                    $result = Invoke-Bd -Arguments @(
                        'create', $groupTitle,
                        '-t', 'task',
                        '-p', "$priority",
                        '-d', $progressDesc,
                        '-l', $labelStr
                    )
                    if ($null -eq $result) {
                        Write-Error "Failed to create subtask group $($group.Number)"
                        continue
                    }
                    $groupBeadsId = $result.id
                    Write-Host "  Created task: $groupBeadsId - $groupTitle"

                    # Parent-child dep to phase
                    Invoke-Bd -Arguments @('dep', 'add', $groupBeadsId, $phaseBeadsId, '--type', 'parent-child') | Out-Null

                    # Set initial status
                    if ($groupStatus -eq 'in_progress') {
                        Invoke-Bd -Arguments @('update', $groupBeadsId, '--status', 'in_progress') | Out-Null
                    } elseif ($groupStatus -eq 'closed') {
                        Invoke-Bd -Arguments @('close', $groupBeadsId, '--reason', $progressDesc) | Out-Null
                    }
                }
                $phaseEntry.SubtaskGroups[$groupKey] = $groupBeadsId
            } else {
                # Update existing subtask group
                if ($DryRun) {
                    Write-Host "[DRY RUN]   Would update task $groupBeadsId -> status=$groupStatus $progressDesc"
                } else {
                    # Update description with progress
                    Invoke-Bd -Arguments @('update', $groupBeadsId, '-d', $progressDesc) | Out-Null

                    if ($groupStatus -eq 'closed') {
                        Invoke-Bd -Arguments @('close', $groupBeadsId, '--reason', $progressDesc) | Out-Null
                        Write-Host "  Closed task: $groupBeadsId $progressDesc"
                    } elseif ($groupStatus -eq 'in_progress') {
                        Invoke-Bd -Arguments @('update', $groupBeadsId, '--status', 'in_progress') | Out-Null
                        Write-Host "  Updated task: $groupBeadsId -> in_progress $progressDesc"
                    }
                }
            }

            [void]$results.Add(@{
                Group    = $group.Number
                Title    = $group.Title
                BeadsId  = $groupBeadsId
                Status   = $groupStatus
                Progress = $progressDesc
            })
        }
    }

    # --- Add phase-level blocking dependencies ---
    foreach ($dep in $Parsed.PhaseDependencies) {
        $depPhaseKey = "$($dep.Phase)"
        if (-not $manifest.Phases.ContainsKey($depPhaseKey)) { continue }
        $dependentId = $manifest.Phases[$depPhaseKey].BeadsId

        foreach ($blockerPhaseNum in $dep.DependsOn) {
            $blockerKey = "$blockerPhaseNum"
            if (-not $manifest.Phases.ContainsKey($blockerKey)) { continue }
            $blockerId = $manifest.Phases[$blockerKey].BeadsId

            if ($DryRun) {
                Write-Host "[DRY RUN] Would add dependency: Phase $($dep.Phase) blocked by Phase $blockerPhaseNum"
            } else {
                Invoke-Bd -Arguments @('dep', 'add', $dependentId, $blockerId, '--type', 'blocks') | Out-Null
                Write-Host "Added dependency: $dependentId blocked by $blockerId"
            }
        }
    }

    # --- Save manifest ---
    $manifest.LastSyncedAt = (Get-Date).ToString('o')
    if (-not $DryRun) {
        Write-Manifest -ManifestPath $ManifestPath -Manifest $manifest
        Write-Host "Manifest saved: $ManifestPath"
    }

    return @{
        Manifest = $manifest
        Results  = @($results)
    }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

# Resolve tasks file path
if (-not $TasksFile) {
    # Try to auto-detect from speckit common.ps1 paths
    if (Get-Command 'Get-FeaturePathsEnv' -ErrorAction SilentlyContinue) {
        $paths = Get-FeaturePathsEnv
        $TasksFile = $paths.TASKS
    } else {
        Write-Output "ERROR: No -TasksFile specified and cannot auto-detect feature paths."
        Write-Output "Either provide -TasksFile or ensure common.ps1 is available."
        exit 1
    }
}

if (-not (Test-Path $TasksFile -PathType Leaf)) {
    Write-Output "ERROR: Tasks file not found: $TasksFile"
    Write-Output "Run /speckit.tasks first to create the task list."
    exit 1
}

# Verify bd is available
if (-not (Get-Command 'bd' -ErrorAction SilentlyContinue)) {
    Write-Output "ERROR: bd (beads) CLI not found. Install it first:"
    Write-Output "  curl -fsSL https://raw.githubusercontent.com/steveyegge/beads/main/scripts/install.sh | bash"
    exit 1
}

# Parse and sync
Write-Output "Parsing: $TasksFile"
$parsed = ConvertFrom-TasksFile -Path $TasksFile

Write-Output "Spec: $($parsed.SpecName)"
Write-Output "Phases: $($parsed.Phases.Count)"
Write-Output "Phase dependencies: $($parsed.PhaseDependencies.Count)"
Write-Output ""

$manifestPath = Get-ManifestPath -TasksFilePath $TasksFile

$syncResult = Sync-ToBeads `
    -Parsed $parsed `
    -ManifestPath $manifestPath `
    -DryRun:$DryRun

if ($null -eq $syncResult) {
    Write-Output "ERROR: Sync failed"
    exit 1
}

Write-Output ""
Write-Output "Sync complete."

if ($Json) {
    $syncResult | ConvertTo-Json -Depth 10
}
