#!/usr/bin/env python3
"""Reject live mirrors without mistaking Samsung's RenderEngine history for live displays."""
from helper_test_support import has_live_helper_mirror

cached = '''Display identification data:
Display 123 (Virtual display): displayName="scrcpy"
Displays (1 entries)
Display 123 (virtual, "scrcpy")
RenderEngine cache (enabled):
\tDisplay ms912x-helper-7 (456)
\tDisplay Cache (cached for 6 secs):
'''
assert not has_live_helper_mirror(cached)
assert has_live_helper_mirror(cached + '\nDisplay 456 (virtual, "ms912x-helper-7")\n')
assert has_live_helper_mirror('Displays (1 entries)\n+ DisplayDevice{456, virtual, "ms912x-helper-7"}\n')
assert has_live_helper_mirror('Displays (1 entries)\nDisplay 456 (Virtual display): displayName="ms912x-helper-7"\n')
try:
    has_live_helper_mirror('Permission denied')
    raise AssertionError('Missing inventory was accepted')
except AssertionError as failure:
    assert 'Unrecognized' in str(failure)
print('PASS: helper mirror parser (API 30/37 active records, historical cache, missing inventory)')
