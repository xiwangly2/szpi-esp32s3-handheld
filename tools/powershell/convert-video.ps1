[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    [Parameter(Mandatory = $true)][string]$OutputFile,
    [ValidateSet(10, 15, 20, 30)][int]$Fps = 15,
    [ValidateRange(2, 15)][int]$Quality = 6,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $InputFile).Path
$output = [IO.Path]::GetFullPath($OutputFile)
if ($source -eq $output -or [IO.Path]::GetExtension($output) -ne '.avi') {
    throw 'Choose a separate .avi output file.'
}
if ((Test-Path -LiteralPath $output) -and -not $Force) {
    throw 'Output already exists. Use -Force to replace it.'
}
$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source
$ffprobe = (Get-Command ffprobe -ErrorAction Stop).Source
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($output)) | Out-Null
$partial = "$output.partial.avi"
$filter = "scale=320:240:force_original_aspect_ratio=decrease:flags=lanczos,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,fps=$Fps"
& $ffmpeg -hide_banner -loglevel warning -nostdin -y -i $source -map 0:v:0 -map '0:a:0?' `
    -vf $filter -c:v mjpeg -pix_fmt yuvj420p -q:v $Quality -threads 2 `
    -c:a pcm_s16le -ar 22050 -ac 1 -map_metadata -1 -f avi $partial
if ($LASTEXITCODE -ne 0) { throw 'FFmpeg conversion failed; output was not replaced.' }
$raw = & $ffprobe -v error -show_streams -show_format -of json $partial
if ($LASTEXITCODE -ne 0) { throw 'Converted video validation failed.' }
$probe = ($raw -join "`n") | ConvertFrom-Json
$video = @($probe.streams | Where-Object codec_type -eq 'video')[0]
if ($video.codec_name -ne 'mjpeg' -or $video.width -ne 320 -or $video.height -ne 240) {
    throw 'Unexpected converted video format.'
}
$packets = & $ffprobe -v error -select_streams v:0 -show_entries packet=size -of csv=p=0 $partial
if ($LASTEXITCODE -ne 0) { throw 'Frame size validation failed.' }
$maxFrame = ($packets | ForEach-Object { [int]$_ } | Measure-Object -Maximum).Maximum
if ($maxFrame -gt 131072 -or (Get-Item -LiteralPath $partial).Length -gt 268435456) {
    throw 'Video exceeds handheld limits. Increase -Quality or shorten the source.'
}
Move-Item -LiteralPath $partial -Destination $output -Force:$Force
$manifest = [ordered]@{
    file = [IO.Path]::GetFileName($output)
    video = 'mjpeg'; width = 320; height = 240; fps = $Fps
    audio = @($probe.streams | Where-Object codec_type -eq 'audio' | Select-Object codec_name,sample_rate,channels)
    duration_seconds = $probe.format.duration
    bytes = (Get-Item -LiteralPath $output).Length
    largest_video_frame = $maxFrame
    sha256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$output.json" -Encoding UTF8
$manifest | ConvertTo-Json -Depth 4
