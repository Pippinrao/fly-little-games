"""Shared matchers for the Harmony product contract gates.

Kept import-side-effect free so the contract script and its unit tests can
load it from any working directory.
"""
import re

# RunGame.start must forward the selected locator and the autosave preference
# to PlayService.open. The call gained a third argument, so the matcher binds
# all three in order while tolerating whitespace and a trailing comma.
RUN_GAME_OPEN_RE = re.compile(
    r"this\.play\.open\s*\(\s*context\s*,\s*this\.locator\s*,\s*this\.autosaveEnabled\s*,?\s*\)"
)


def run_game_forwards_locator_and_autosave(start_body):
    return RUN_GAME_OPEN_RE.search(start_body) is not None
