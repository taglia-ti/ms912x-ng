"""Shared checks for the live helper acceptance tools."""
import re

def has_live_helper_mirror(compositor):
    if not re.search(r'^Displays \(\d+ entries\)', compositor, re.MULTILINE):
        raise AssertionError('Unrecognized SurfaceFlinger display inventory')
    # Android 11 lists DisplayDevice records; recent builds also identify each
    # display by numeric ID. RenderEngine's historical cache uses name-first
    # headings and can retain a removed display's name for several seconds.
    return any('ms912x-helper-' in line and (
        re.match(r'^\s*\+\s*DisplayDevice\{', line)
        or re.match(r'^Display \d+\b', line)) for line in compositor.splitlines())
