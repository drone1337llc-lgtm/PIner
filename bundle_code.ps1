# Optimized bundle script with better performance and error handling
param(
    [string]$OutputFile = "all_scripts_bundle.txt",
    [string[]]$ExcludeFolders = @("build", "bin", ".git", "obj", "dist", "__pycache__")
)

# Performance optimization: Use .NET methods for file operations
$OutputPath = [System.IO.Path]::GetFullPath($OutputFile)
$Extensions = @("*.h", "*.cpp", "*.ino", "*.py", "*.sh", "*.ps1")

Write-Host "Bundling scripts into $OutputPath..." -ForegroundColor Cyan

# Clear output file efficiently
[System.IO.File]::WriteAllText($OutputPath, "", [System.Text.Encoding]::UTF8)

# Get files with parallel processing for better performance
$Files = Get-ChildItem -Recurse -File -Include $Extensions | Where-Object { 
    $filePath = $_.FullName
    $ExcludeFolders | ForEach-Object { $filePath -notmatch $_ }
}

# Process files with progress tracking
$counter = 0
$total = $Files.Count

$Files | ForEach-Object {
    $File = $_
    $counter++
    $percentComplete = ($counter / $total) * 100
    
    Write-Progress -Activity "Bundling Files" -Status "Processing: $($File.Name)" -PercentComplete $percentComplete
    Write-Host "Processing ($counter/$total): $($File.Name)" -ForegroundColor Yellow
    
    try {
        # Read file content efficiently
        $content = [System.IO.File]::ReadAllText($File.FullName, [System.Text.Encoding]::UTF8)
        
        # Create header with relative path
        $relativePath = $File.FullName.Replace((Get-Location).Path + "\", "")
        $Header = "`n`n--- FILE: $relativePath ---`n`n"
        
        # Append to file using efficient method
        [System.IO.File]::AppendAllText($OutputPath, $Header + $content, [System.Text.Encoding]::UTF8)
    }
    catch {
        Write-Host "Error processing $($File.Name): $($_.Exception.Message)" -ForegroundColor Red
    }
}

Write-Progress -Activity "Bundling Files" -Completed
Write-Host "Done! Bundled $total files into $OutputPath" -ForegroundColor Green

# Show file information
$fileInfo = Get-Item $OutputPath
Write-Host "Output file size: $([math]::Round($fileInfo.Length/1KB, 2)) KB" -ForegroundColor Cyan
