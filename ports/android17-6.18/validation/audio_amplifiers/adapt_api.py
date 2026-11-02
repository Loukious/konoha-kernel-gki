"""One-shot 6.18 API/prototype adaptation, preserving native exports."""
from pathlib import Path
import json
import re

H = Path(__file__).resolve().parent
base = H / 'private-source'
sources = {p: (base / p).read_text() for p in json.loads((H / 'sources.json').read_text())}
log = Path('/tmp/onyx-audio-amplifiers-build2.log').read_text()
prototypes = []
for name in dict.fromkeys(re.findall("no previous prototype for function '([^']+)'", log)):
    candidates = [(p, text) for p, text in sources.items()
                  if p.endswith('.c') and re.search(r'^\w[^\n]*\b' + name + r'\(', text, re.M)]
    assert len(candidates) == 1, (name, [p for p, _ in candidates])
    p, text = candidates[0]
    pattern = r'^(?!static)([a-zA-Z_][\w *]*\b' + name + r'\([^;{}]*\))\s*\{'
    match = re.search(pattern, text, re.M)
    assert match, name
    if re.search(r'EXPORT_SYMBOL(?:_GPL)?\(' + name + r'\)', text) or name == 'sipa_pending_actions':
        assert 'sia91xx/' in p, name
        prototypes.append(match.group(1) + ';')
    else:
        text = text[:match.start()] + 'static ' + text[match.start():]
        sources[p] = text

common = 'qcom/opensource/audio-kernel/asoc/codecs/sia91xx/sipa_common.h'
text = sources[common]
end = text.rfind('#endif')
text = text[:end] + ('struct snd_soc_aux_dev;\nstruct snd_soc_card;\n'
                     'struct snd_soc_codec_conf;\n\n' + '\n'.join(prototypes) + '\n\n') + text[end:]
sources[common] = text

aw = 'qcom/opensource/audio-kernel/asoc/codecs/aw882xx/aw882xx.c'
text = sources[aw]
where = text.index('static struct aw_componet_codec_ops aw_componet_codec_ops')
text = text[:where] + '''static void aw882xx_unregister_component(struct device *dev)
{
	snd_soc_unregister_component(dev);
}

''' + text[where:]
text = text.replace('.unregister_codec = snd_soc_unregister_component,',
                    '.unregister_codec = aw882xx_unregister_component,')
sources[aw] = text
for p, text in sources.items():
    text = text.replace('GPIOF_DIR_IN', 'GPIOF_IN')
    text = text.replace('SND_SOC_DAIFMT_CBS_CFS', 'SND_SOC_DAIFMT_CBC_CFC')
    (base / p).write_text(text)
