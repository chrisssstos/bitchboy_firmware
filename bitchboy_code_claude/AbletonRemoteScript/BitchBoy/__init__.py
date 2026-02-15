# BitchBoy MIDI Remote Script for Ableton Live (10 / 11 / 12)
#
# Installation:
#   Copy the entire "BitchBoy" folder into Ableton's MIDI Remote Scripts dir:
#     Windows:  C:\ProgramData\Ableton\Live <version>\Resources\MIDI Remote Scripts\
#     macOS:    /Applications/Ableton Live <ver>.app/Contents/App-Resources/MIDI Remote Scripts/
#
# Ableton Preferences > Link, Tempo & MIDI:
#   Control Surface : BitchBoy
#   Input           : BitchBoy  (your USB MIDI device name)
#   Output          : BitchBoy
#
#   Also enable the same device under MIDI Ports with "Track: On" so that
#   pad presses, sliders and pots still reach tracks for normal MIDI mapping.

from .bitchboy import BitchBoy


def create_instance(c_instance):
    return BitchBoy(c_instance)
