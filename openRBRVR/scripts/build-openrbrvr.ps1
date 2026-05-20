$ErrorActionPreference = "Stop"

if (-not (Get-Command zig -ErrorAction SilentlyContinue)) {
    throw "Zig is not on PATH. Install the Zig version required by this repository before building."
}

zig build --release=fast
