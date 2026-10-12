"""Check that libtogl_client.so exports exactly what the game imports (exports.map).

    python check_exports.py <libtogl_client.so> <exports.map>
"""
import re, subprocess, sys

library, version_script = sys.argv[1], sys.argv[2]
text = open(version_script, encoding='utf8').read()
wanted = set(re.findall(r'^\s+([A-Za-z_][A-Za-z0-9_]*);', text.split('local:')[0], re.M))
out = subprocess.run(['nm', '-D', '--defined-only', library], check=True, capture_output=True, text=True).stdout
exported = {line.split()[-1] for line in out.splitlines() if line.split() and line.split()[-2] in 'TtWwDdBbVvRr'}
missing = sorted(wanted - exported)
extra = sorted(s for s in exported - wanted if not s.startswith('_ZTI') and not s.startswith('_ZTS') and not s.startswith('_ZTV'))
for s in missing:
    print('missing export:', s)
for s in extra:
    print('unexpected export:', s)
print(f'{len(wanted) - len(missing)} of {len(wanted)} game imports exported')
sys.exit(1 if missing or extra else 0)
