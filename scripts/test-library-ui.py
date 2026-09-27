"""Library behavior is exercised by the QoL suite using disposable fixtures."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).with_name('test-qol.py')),run_name='__main__')
