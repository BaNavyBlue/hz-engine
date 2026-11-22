"""The mutations of session31_front.py the runner's first scripts missed
(paths rarely reached: the MUSIC test's last song, the map and TYPE
select with exactly one computer side), run again after the scripts
gained their aims (tools/hzfront_check.py make_script). CHECK 'front'."""

CHECK = 'front'

_ns = {}
exec(open('tools/mutations/session31_front.py').read(), _ns)
_GAPS = ('options: music wrap', 'options: map list width',
         'options: wins letters font', 'options: type first',
         'options: type skip', 'options: option2', 'options: type panel')
MUTATIONS = [m for m in _ns['MUTATIONS'] if m[0] in _GAPS]
