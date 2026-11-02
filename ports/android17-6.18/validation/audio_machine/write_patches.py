from pathlib import Path
import difflib
import json

H = Path(__file__).resolve().parent
assert not (H / 'frozen-source').exists(), 'Machine source is already frozen'
groups = [
    ('patch', 'original', 'private-source', 'sources.json',
     'Port native SUN machine audio ownership',
     'Port the native SUN machine driver to 6.18. Keep copied card, DAI and\n'
     'amplifier names alive for their users; unwind sysfs, headset detection,\n'
     'clock votes and SSR registration before releasing owned state.'),
    ('provider.patch', 'provider-original', 'provider-source', 'provider-paths.json',
     'Release unpublished SND event client lists',
     'Allow the machine driver to release an unpublished SND event client list\n'
     'when master registration fails. Successful master registration keeps its\n'
     'existing ownership contract.'),
]
for filename, original, source, names, subject, description in groups:
    patch = 'Subject: ' + subject + '\n\n' + description + '\n\n---\n\n'
    for name in json.loads((H / names).read_text()):
        patch += ''.join(difflib.unified_diff(
            (H / original / name).read_text().splitlines(True),
            (H / source / name).read_text().splitlines(True),
            fromfile='a/' + name, tofile='b/' + name))
    (H / filename).write_text(patch)
