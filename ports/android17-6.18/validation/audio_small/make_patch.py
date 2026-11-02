"""Generate replayable diffs without touching the source snapshots."""
import difflib
import json
from pathlib import Path

H = Path(__file__).resolve().parent
subject = 'Port native LPI, SDCA, SoundWire DMIC/haptics and HDMI audio helpers to 6.18'
body = ('Keep all six native one-object modules, configuration branches and\n'
        'audio tables.\n'
        'Use public pinctrl APIs and 6.18 callback conventions, bound debugfs reads,\n'
        'and balance notifier, component, supplier and PM lifetimes.\n'
        'HDMI requires the preceding display codec withdrawal provider extension.\n')
patch = 'Subject: ' + subject + '\n\n' + body + '\n---\n\n'
for path in json.loads((H / 'sources.json').read_text()):
    patch += ''.join(difflib.unified_diff((H / 'original' / path).read_text().splitlines(True),
        (H / 'private-source' / path).read_text().splitlines(True), fromfile='a/' + path, tofile='b/' + path))
(H / 'patch').write_text(patch)
print('Generated small-audio patch.')
