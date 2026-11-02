"""Generate only the separate display provider lifetime extension."""
import difflib
import json
from pathlib import Path

H = Path(__file__).resolve().parent
patch = ('Subject: Add synchronized withdrawal of caller-owned display audio codec storage\n\n'
    'Drain all table accesses through the existing publication mutex.\n'
    'Prepare ready notifications under lock, then invoke their independent\n'
    'callback snapshot. Permit identity-checked self-withdrawal from\n'
    'synchronous extcon notifications without recursively acquiring the\n'
    'mutex. Preserve existing exported APIs.\n\n---\n\n')
for path in json.loads((H / 'sources.json').read_text()):
    patch += ''.join(difflib.unified_diff((H / 'original' / path).read_text().splitlines(True),
        (H / 'private-source' / path).read_text().splitlines(True), fromfile='a/' + path, tofile='b/' + path))
(H / 'patch').write_text(patch)
print('Generated separate provider patch.')
