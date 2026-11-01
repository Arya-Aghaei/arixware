"""Pre-rotate the splash for the mounted panel; requires ffmpeg.
Run from any directory: python3 tools/generate_splash.py
"""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
source = root / 'components/assets/gif/splash.gif'
out = root / 'components/assets/generated/splash_panel.gif'
out.parent.mkdir(parents=True, exist_ok=True)
# The native GIF widget can blit RGB565 directly, without a full-screen rotation layer.
subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', str(source), '-filter_complex',
    '[0:v]transpose=clock,split[a][b];[a]palettegen=reserve_transparent=0[p];[b][p]paletteuse',
    '-fps_mode', 'passthrough', '-loop', '-1', str(out)], check=True)
print(f'{out.name}: {out.stat().st_size:,} bytes')
