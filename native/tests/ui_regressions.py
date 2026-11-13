#!/usr/bin/env python3
"""Actual worker/Deck/GLES regressions with disposable synthetic IPC fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="syn-ui-regressions-") as temp:
    args = [str(Path(sys.argv[1]).resolve()), temp]
    xvfb = shutil.which("xvfb-run")
    if xvfb:
        args = [xvfb, "-a", "-s", "-screen 0 3840x2160x24", *args]
    env = dict(os.environ, LIBGL_ALWAYS_SOFTWARE="1", MESA_GLES_VERSION_OVERRIDE="2.0")
    result = subprocess.run(args, env=env, timeout=40)
    sys.exit(result.returncode)
