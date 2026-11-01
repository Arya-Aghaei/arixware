"""Compile and run the production timezone/cache regression checks."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='arixware-time-') as directory:
    executable = Path(directory) / 'timezone'
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
        '-I'+str(root/'components/net/include'), '-I'+str(root/'tests/net/profile_stubs'),
        str(root/'tests/net/test_timezone.c'), str(root/'components/net/net_timezone.c'),
        '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

    # Use the same cJSON source as ESP-IDF for transport response checks.
    import os
    idf = Path(os.environ.get('IDF_PATH', str(root.parent/'esp-idf')))
    json = idf/'components/json/cJSON'
    lookup = Path(directory)/'lookup'
    subprocess.run(['cc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
        '-I'+str(root/'tests/net/time_stubs'), '-I'+str(root/'tests/net/profile_stubs'),
        '-I'+str(root/'components/net/include'), '-I'+str(json),
        str(root/'tests/net/test_time_lookup.c'), str(json/'cJSON.c'), '-lm', '-o', str(lookup)], check=True)
    subprocess.run([str(lookup)], check=True)
