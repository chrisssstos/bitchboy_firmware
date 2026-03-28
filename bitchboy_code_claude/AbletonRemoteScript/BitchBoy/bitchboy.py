"""
BitchBoy  --  Ableton Live MIDI Remote Script
Sends real-time clip-state LED feedback to the BitchBoy controller via
Note On/Off messages using the same note numbers the firmware expects.
Pad presses auto-fire the corresponding clip slot (no manual MIDI mapping).

Clip-state -> velocity mapping (matches firmware velocityToColor()):
  playing        -> 21  bright green  #00FF00  (solid)
  triggered      -> 40  FLASHING_VELOCITY       (firmware blinks)
  recording      ->  5  bright red    #FF0000  (solid)
  stopped w/clip -> nearest palette match to Ableton clip colour
  group w/clip   -> 48  purple        #874CFF  (solid)
  empty          -> Note Off                   (LED off)

Grid layout:
  Rows 0-3 (4x8)  = scrollable session grid (scenes x tracks)
  Row 4     (8)    = excluded (separate physical row)
  Rows 5-7 (3x3)  = D-pad navigation (up/down/left/right)

Session highlight rectangle is shown in Ableton's Session View.
A periodic refresh timer ensures LEDs stay in sync after clip stops.
Group tracks check child tracks for clip presence.
"""

from __future__ import absolute_import
from _Framework.ControlSurface import ControlSurface
from _Framework.SessionComponent import SessionComponent
from _Framework.ButtonElement import ButtonElement
from _Framework.InputControlElement import MIDI_NOTE_TYPE, MIDI_CC_TYPE
from _Framework.SliderElement import SliderElement

# ---------------------------------------------------------------------------
# Pad-to-note table  (must match firmware padToNote[])
# ---------------------------------------------------------------------------
PAD_NOTES = [
    [64, 65, 66, 67, 96, 97, 98, 99],  # Row 0
    [60, 61, 62, 63, 92, 93, 94, 95],  # Row 1
    [56, 57, 58, 59, 88, 89, 90, 91],  # Row 2
    [52, 53, 54, 55, 84, 85, 86, 87],  # Row 3
    [48, 49, 50, 51, 80, 81, 82, 83],  # Row 4 (excluded from grid)
    [44, 45, 46],                       # Row 5
    [40, 41, 42],                       # Row 6
    [36, 37, 38],                       # Row 7
]

# Session grid = rows 0-3 only (row 4 is physically separate)
SESSION_ROWS = 4
SESSION_COLS = 8

# D-pad navigation notes (from the 3x3 area)
NAV_UP    = 45
NAV_DOWN  = 37
NAV_LEFT  = 40
NAV_RIGHT = 42
NAV_NOTES = frozenset([NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT])

# All notes in the 3x3 control area (rows 5-7)
CONTROL_NOTES = frozenset([44, 45, 46, 40, 41, 42, 36, 37, 38])

# Excluded row 4 notes
ROW4_NOTES = [48, 49, 50, 51, 80, 81, 82, 83]

# ---------------------------------------------------------------------------
# Velocity codes  (must match firmware velocityToColor())
# ---------------------------------------------------------------------------
VEL_PLAYING   = 21   # bright green  #00FF00
VEL_TRIGGERED = 40   # FLASHING_VELOCITY (firmware blinks the LED)
VEL_RECORDING = 5    # bright red    #FF0000
VEL_GROUP     = 48   # purple        #874CFF (group track with clip)
VEL_NAV_ON    = 9    # orange        #FF5400 (arrow can scroll)
VEL_NAV_OFF   = 1    # dim gray      #1E1E1E (arrow at edge)

MIDI_CH = 0  # channel 1
MIDI_NOTE_ON  = 0x90 | MIDI_CH
MIDI_NOTE_OFF = 0x80 | MIDI_CH

# Vertical fader sliders: firmware sliderNums 1-8 -> CC 0-7
FADER_CC_START = 0
FADER_CC_END   = 7
NUM_FADERS     = 8

SLOT_LISTENERS = ("has_clip", "is_triggered", "playing_status", "is_playing")
REFRESH_TICKS = 3

# ---------------------------------------------------------------------------
# Firmware colour palette (vel 0-127) — from velocityToColor() in firmware.
# Used to find the nearest velocity for a given Ableton clip RGB colour.
# ---------------------------------------------------------------------------
PALETTE = [
    (0x06, 0x06, 0x06),  # 0
    (0x1E, 0x1E, 0x1E),  # 1
    (0x7F, 0x7F, 0x7F),  # 2
    (0xFF, 0xFF, 0xFF),  # 3
    (0xFF, 0x4C, 0x4C),  # 4
    (0xFF, 0x00, 0x00),  # 5
    (0x59, 0x00, 0x00),  # 6
    (0x19, 0x00, 0x00),  # 7
    (0xFF, 0xBD, 0x6C),  # 8
    (0xFF, 0x54, 0x00),  # 9
    (0x59, 0x1D, 0x00),  # 10
    (0x27, 0x1B, 0x00),  # 11
    (0xFF, 0xFF, 0x4C),  # 12
    (0xFF, 0xFF, 0x00),  # 13
    (0x59, 0x59, 0x00),  # 14
    (0x19, 0x19, 0x00),  # 15
    (0x88, 0xFF, 0x4C),  # 16
    (0x54, 0xFF, 0x00),  # 17
    (0x1D, 0x59, 0x00),  # 18
    (0x14, 0x2B, 0x00),  # 19
    (0x4C, 0xFF, 0x4C),  # 20
    (0x00, 0xFF, 0x00),  # 21
    (0x00, 0x59, 0x00),  # 22
    (0x00, 0x19, 0x00),  # 23
    (0x4C, 0xFF, 0x5E),  # 24
    (0x00, 0xFF, 0x19),  # 25
    (0x00, 0x59, 0x0D),  # 26
    (0x00, 0x19, 0x02),  # 27
    (0x4C, 0xFF, 0x88),  # 28
    (0x00, 0xFF, 0x55),  # 29
    (0x00, 0x59, 0x1D),  # 30
    (0x00, 0x1F, 0x12),  # 31
    (0xD6, 0x35, 0x00),  # 32
    (0x00, 0xFF, 0x99),  # 33
    (0x00, 0x59, 0x35),  # 34
    (0x00, 0x19, 0x12),  # 35
    (0x4C, 0xC3, 0xFF),  # 36
    (0x00, 0xA9, 0xFF),  # 37
    (0x00, 0x41, 0x52),  # 38
    (0x00, 0x10, 0x19),  # 39
    (0x4C, 0x88, 0xFF),  # 40
    (0x00, 0x55, 0xFF),  # 41
    (0x00, 0x1D, 0x59),  # 42
    (0x00, 0x08, 0x19),  # 43
    (0x4C, 0x4C, 0xFF),  # 44
    (0x00, 0x00, 0xFF),  # 45
    (0x00, 0x00, 0x59),  # 46
    (0x00, 0x00, 0x19),  # 47
    (0x87, 0x4C, 0xFF),  # 48
    (0x54, 0x00, 0xFF),  # 49
    (0x19, 0x00, 0x64),  # 50
    (0x0F, 0x00, 0x30),  # 51
    (0xFF, 0x4C, 0xFF),  # 52
    (0xFF, 0x00, 0xFF),  # 53
    (0x59, 0x00, 0x59),  # 54
    (0x19, 0x00, 0x19),  # 55
    (0xFF, 0x4C, 0x87),  # 56
    (0xFF, 0x00, 0x54),  # 57
    (0x59, 0x00, 0x1D),  # 58
    (0x22, 0x00, 0x13),  # 59
    (0xFF, 0x15, 0x00),  # 60
    (0x99, 0x35, 0x00),  # 61
    (0x79, 0x51, 0x00),  # 62
    (0x43, 0x64, 0x00),  # 63
    (0x18, 0x18, 0x00),  # 64
    (0x00, 0x57, 0x35),  # 65
    (0x00, 0x54, 0x7F),  # 66
    (0x00, 0x00, 0xFF),  # 67
    (0x00, 0x45, 0x4F),  # 68
    (0x25, 0x00, 0xCC),  # 69
    (0x7F, 0x7F, 0x7F),  # 70
    (0x20, 0x20, 0x20),  # 71
    (0xFF, 0x00, 0x00),  # 72
    (0xBD, 0xFF, 0x2D),  # 73
    (0xAF, 0xED, 0x06),  # 74
    (0x64, 0xFF, 0x09),  # 75
    (0x10, 0x8B, 0x00),  # 76
    (0x00, 0xFF, 0x87),  # 77
    (0x00, 0xA9, 0xFF),  # 78
    (0x00, 0x2A, 0xFF),  # 79
    (0x3F, 0x00, 0xFF),  # 80
    (0x7A, 0x00, 0xFF),  # 81
    (0xB2, 0x1A, 0x7D),  # 82
    (0x40, 0x21, 0x00),  # 83
    (0xFF, 0x4A, 0x00),  # 84
    (0x88, 0xE1, 0x06),  # 85
    (0x72, 0xFF, 0x15),  # 86
    (0x00, 0xFF, 0x00),  # 87
    (0x3B, 0xFF, 0x26),  # 88
    (0x59, 0xFF, 0x71),  # 89
    (0x38, 0xFF, 0xCC),  # 90
    (0x5B, 0x8A, 0xFF),  # 91
    (0x31, 0x51, 0xC6),  # 92
    (0x87, 0x7F, 0xE9),  # 93
    (0xD3, 0x1D, 0xFF),  # 94
    (0x00, 0x20, 0x00),  # 95
    (0xFF, 0x7F, 0x00),  # 96
    (0xB9, 0xB0, 0x00),  # 97
    (0x90, 0xFF, 0x00),  # 98
    (0x83, 0x5D, 0x07),  # 99
    (0x39, 0x2B, 0x00),  # 100
    (0x14, 0x4C, 0x10),  # 101
    (0x0D, 0x50, 0x38),  # 102
    (0x15, 0x15, 0x2A),  # 103
    (0x16, 0x20, 0x5A),  # 104
    (0x69, 0x3C, 0x1C),  # 105
    (0xA8, 0x00, 0x0A),  # 106
    (0xDE, 0x51, 0x3D),  # 107
    (0xD8, 0x6A, 0x1C),  # 108
    (0xFF, 0xE1, 0x26),  # 109
    (0x9E, 0xE1, 0x2F),  # 110
    (0x67, 0xB5, 0x0F),  # 111
    (0x1E, 0x1E, 0x30),  # 112
    (0xDC, 0xFF, 0x6B),  # 113
    (0x80, 0xFF, 0xBD),  # 114
    (0x9A, 0x99, 0xFF),  # 115
    (0x8E, 0x66, 0xFF),  # 116
    (0x40, 0x40, 0x40),  # 117
    (0x75, 0x75, 0x75),  # 118
    (0xE0, 0xFF, 0xFF),  # 119
    (0xA0, 0x00, 0x00),  # 120
    (0x35, 0x00, 0x00),  # 121
    (0x1A, 0xD0, 0x00),  # 122
    (0x07, 0x42, 0x00),  # 123
    (0xB9, 0xB0, 0x00),  # 124
    (0x3F, 0x31, 0x00),  # 125
    (0xB3, 0x5F, 0x00),  # 126
    (0x00, 0x20, 0x00),  # 127
]

# Velocities to skip when matching clip colours (0 = Note Off, 40 = flash)
_SKIP_VELOCITIES = frozenset([0, 40])


class BitchBoy(ControlSurface):

    def __init__(self, c_instance):
        super(BitchBoy, self).__init__(c_instance)
        with self.component_guard():
            self._slot_listeners = []
            self._last_sent = {}
            self._buttons = []
            self._pad_cbs = {}
            self._track_offset = 0
            self._scene_offset = 0
            self._timer_active = False
            self._color_cache = {}
            self._session = SessionComponent(SESSION_COLS, SESSION_ROWS)
            self._session.set_offsets(0, 0)
            self.set_highlighting_session_component(self._session)
            self._build_note_lookup()
            self._create_pad_buttons()
            self._create_fader_sliders()
            self._attach_global_listeners()
            self._attach_slot_listeners()
            self.schedule_message(1, self._refresh_all)
            self._start_refresh_timer()
        self.log_message("BitchBoy: ready")

    def disconnect(self):
        self._timer_active = False
        self._detach_slot_listeners()
        self._detach_global_listeners()
        self._all_leds_off()
        self._destroy_fader_sliders()
        self._destroy_pad_buttons()
        self.log_message("BitchBoy: disconnected")
        super(BitchBoy, self).disconnect()

    # ------------------------------------------------------------------
    # Periodic refresh timer (catches stale states after clip stop)
    # ------------------------------------------------------------------
    def _start_refresh_timer(self):
        self._timer_active = True
        self.schedule_message(REFRESH_TICKS, self._timed_refresh)

    def _timed_refresh(self):
        if not self._timer_active:
            return
        self._poll_clip_states()
        self.schedule_message(REFRESH_TICKS, self._timed_refresh)

    def _poll_clip_states(self):
        for row in range(SESSION_ROWS):
            for col in range(SESSION_COLS):
                note = PAD_NOTES[row][col]
                if note >= 0:
                    self._send_led(note, self._velocity_for(row, col))

    # ------------------------------------------------------------------
    # Clip colour → firmware velocity matching
    # ------------------------------------------------------------------
    def _color_to_velocity(self, ableton_color):
        """Map an Ableton clip RGB int to the nearest firmware velocity."""
        key = ableton_color & 0xFFFFFF
        cached = self._color_cache.get(key)
        if cached is not None:
            return cached
        r = (key >> 16) & 0xFF
        g = (key >> 8) & 0xFF
        b = key & 0xFF
        best_vel = 1
        best_dist = float('inf')
        for vel, (pr, pg, pb) in enumerate(PALETTE):
            if vel in _SKIP_VELOCITIES:
                continue
            dist = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb)
            if dist < best_dist:
                best_dist = dist
                best_vel = vel
        self._color_cache[key] = best_vel
        return best_vel

    # ------------------------------------------------------------------
    # Note lookup
    # ------------------------------------------------------------------
    def _build_note_lookup(self):
        self._note_to_pos = {}
        for row in range(len(PAD_NOTES)):
            for col in range(len(PAD_NOTES[row])):
                n = PAD_NOTES[row][col]
                if n >= 0:
                    self._note_to_pos[n] = (row, col)

    # ------------------------------------------------------------------
    # Pad buttons
    # ------------------------------------------------------------------
    def _create_pad_buttons(self):
        for row in range(len(PAD_NOTES)):
            for col in range(len(PAD_NOTES[row])):
                note = PAD_NOTES[row][col]
                if note >= 0:
                    button = ButtonElement(True, MIDI_NOTE_TYPE, MIDI_CH, note)
                    cb = lambda value, r=row, c=col: self._on_pad_value(r, c, value)
                    button.add_value_listener(cb)
                    self._buttons.append(button)
                    self._pad_cbs[id(button)] = cb

    def _destroy_pad_buttons(self):
        for button in self._buttons:
            cb = self._pad_cbs.get(id(button))
            if cb:
                try:
                    button.remove_value_listener(cb)
                except RuntimeError:
                    pass
        self._buttons = []
        self._pad_cbs = {}

    # ------------------------------------------------------------------
    # Fader sliders -> track volume
    # ------------------------------------------------------------------
    def _create_fader_sliders(self):
        self._faders = []
        self._fader_cbs = {}
        for i in range(NUM_FADERS):
            cc = FADER_CC_START + i
            slider = SliderElement(MIDI_CC_TYPE, MIDI_CH, cc)
            cb = lambda value, idx=i: self._on_fader_value(idx, value)
            slider.add_value_listener(cb)
            self._faders.append(slider)
            self._fader_cbs[id(slider)] = cb

    def _destroy_fader_sliders(self):
        for slider in self._faders:
            cb = self._fader_cbs.get(id(slider))
            if cb:
                try:
                    slider.remove_value_listener(cb)
                except RuntimeError:
                    pass
        self._faders = []
        self._fader_cbs = {}

    def _on_fader_value(self, fader_idx, value):
        track_idx = fader_idx + self._track_offset
        tracks = self.song().tracks
        if track_idx >= len(tracks):
            return
        track = tracks[track_idx]
        vol = track.mixer_device.volume
        vol.value = vol.min + (vol.max - vol.min) * (value / 127.0)

    def _on_pad_value(self, row, col, value):
        if value <= 0:
            return
        note = PAD_NOTES[row][col]
        if note in NAV_NOTES:
            self._navigate(note)
        elif row < SESSION_ROWS:
            self._fire_clip(row, col)

    # ------------------------------------------------------------------
    # D-pad navigation
    # ------------------------------------------------------------------
    def _navigate(self, note):
        num_tracks = len(self.song().tracks)
        num_scenes = len(self.song().scenes)
        moved = False

        if note == NAV_UP and self._scene_offset > 0:
            self._scene_offset -= 1
            moved = True
        elif note == NAV_DOWN and self._scene_offset + SESSION_ROWS < num_scenes:
            self._scene_offset += 1
            moved = True
        elif note == NAV_LEFT and self._track_offset > 0:
            self._track_offset -= 1
            moved = True
        elif note == NAV_RIGHT and self._track_offset + SESSION_COLS < num_tracks:
            self._track_offset += 1
            moved = True

        if moved:
            self._detach_slot_listeners()
            self._attach_slot_listeners()
            self._refresh_all()

    # ------------------------------------------------------------------
    # Clip launching
    # ------------------------------------------------------------------
    def _fire_clip(self, row, col):
        scene_idx = row + self._scene_offset
        track_idx = col + self._track_offset
        tracks = self.song().tracks
        scenes = self.song().scenes
        if scene_idx >= len(scenes) or track_idx >= len(tracks):
            return
        track = tracks[track_idx]
        if track.is_foldable:
            # Fire only child tracks in this group for this scene
            for t in tracks:
                try:
                    if t.group_track == track:
                        t.clip_slots[scene_idx].fire()
                except (AttributeError, IndexError):
                    continue
            return
        slot = track.clip_slots[scene_idx]
        slot.fire()

    # ------------------------------------------------------------------
    # Global listeners (track / scene list changes)
    # ------------------------------------------------------------------
    def _attach_global_listeners(self):
        self.song().add_tracks_listener(self._on_list_changed)
        self.song().add_scenes_listener(self._on_list_changed)

    def _detach_global_listeners(self):
        for fn in (self.song().remove_tracks_listener,
                   self.song().remove_scenes_listener):
            try:
                fn(self._on_list_changed)
            except RuntimeError:
                pass

    def _on_list_changed(self):
        self._detach_slot_listeners()
        num_tracks = len(self.song().tracks)
        num_scenes = len(self.song().scenes)
        self._track_offset = min(self._track_offset, max(0, num_tracks - SESSION_COLS))
        self._scene_offset = min(self._scene_offset, max(0, num_scenes - SESSION_ROWS))
        self._session.set_offsets(self._track_offset, self._scene_offset)
        self._attach_slot_listeners()
        self._refresh_all()

    # ------------------------------------------------------------------
    # Per-slot listeners
    # ------------------------------------------------------------------
    def _attach_slot_listeners(self):
        tracks = self.song().tracks
        num_tracks = len(tracks)
        num_scenes = len(self.song().scenes)

        for row in range(SESSION_ROWS):
            scene_idx = row + self._scene_offset
            if scene_idx >= num_scenes:
                break
            for col in range(SESSION_COLS):
                track_idx = col + self._track_offset
                if track_idx >= num_tracks:
                    continue
                if PAD_NOTES[row][col] < 0:
                    continue
                track = tracks[track_idx]
                slot = track.clip_slots[scene_idx]
                cb = lambda r=row, c=col: self._on_slot_changed(r, c)
                for prop in SLOT_LISTENERS:
                    adder = getattr(slot, "add_%s_listener" % prop, None)
                    if adder is not None:
                        try:
                            adder(cb)
                            self._slot_listeners.append((slot, prop, cb))
                        except RuntimeError:
                            pass
                # For group tracks, also listen on child track slots
                if track.is_foldable:
                    for t in tracks:
                        try:
                            if t.group_track == track:
                                child_slot = t.clip_slots[scene_idx]
                                for prop in SLOT_LISTENERS:
                                    adder = getattr(child_slot, "add_%s_listener" % prop, None)
                                    if adder is not None:
                                        try:
                                            adder(cb)
                                            self._slot_listeners.append((child_slot, prop, cb))
                                        except RuntimeError:
                                            pass
                        except (AttributeError, IndexError):
                            continue

    def _detach_slot_listeners(self):
        for obj, prop, cb in self._slot_listeners:
            remover = getattr(obj, "remove_%s_listener" % prop, None)
            if remover is not None:
                try:
                    remover(cb)
                except (RuntimeError, TypeError, Exception):
                    pass
        self._slot_listeners = []

    # ------------------------------------------------------------------
    # State evaluation
    # ------------------------------------------------------------------
    def _velocity_for(self, row, col):
        scene_idx = row + self._scene_offset
        track_idx = col + self._track_offset
        tracks = self.song().tracks
        scenes = self.song().scenes
        if scene_idx >= len(scenes) or track_idx >= len(tracks):
            return 0
        track = tracks[track_idx]

        # Group tracks: check child tracks for clip state
        if track.is_foldable:
            return self._group_velocity(track, scene_idx, tracks)

        # Normal track
        slot = track.clip_slots[scene_idx]
        if not slot.has_clip:
            return 0
        if slot.is_triggered:
            return VEL_TRIGGERED
        if slot.is_recording:
            return VEL_RECORDING
        if slot.is_playing:
            return VEL_PLAYING
        # Stopped — match the clip's Ableton colour
        clip = slot.clip
        if clip is not None:
            try:
                return self._color_to_velocity(clip.color)
            except (AttributeError, RuntimeError):
                pass
        return VEL_GROUP  # fallback

    def _group_velocity(self, group_track, scene_idx, tracks):
        """Determine velocity for a group track by checking its children."""
        has_triggered = False
        has_playing = False
        has_clip = False
        for t in tracks:
            try:
                if t.group_track == group_track:
                    slot = t.clip_slots[scene_idx]
                    if slot.has_clip:
                        has_clip = True
                        if slot.is_triggered:
                            has_triggered = True
                        if slot.is_playing:
                            has_playing = True
            except (AttributeError, IndexError):
                continue
        if has_triggered:
            return VEL_TRIGGERED
        if has_playing:
            return VEL_PLAYING
        if has_clip:
            return VEL_GROUP
        return 0

    # ------------------------------------------------------------------
    # LED output
    # ------------------------------------------------------------------
    def _send_led(self, note, velocity):
        if note < 0:
            return
        if self._last_sent.get(note) == velocity:
            return
        self._last_sent[note] = velocity
        if velocity > 0:
            self._send_midi((MIDI_NOTE_ON, note, velocity))
        else:
            self._send_midi((MIDI_NOTE_OFF, note, 0))

    def _on_slot_changed(self, row, col):
        note = PAD_NOTES[row][col]
        self._send_led(note, self._velocity_for(row, col))

    def _refresh_all(self):
        self._last_sent = {}

        # Session grid (rows 0-3)
        for row in range(SESSION_ROWS):
            for col in range(SESSION_COLS):
                note = PAD_NOTES[row][col]
                if note >= 0:
                    self._send_led(note, self._velocity_for(row, col))

        # Turn off excluded row 4
        for note in ROW4_NOTES:
            self._send_led(note, 0)

        # Nav arrows + unused control pads
        self._update_nav_leds()

        # Session highlight in Ableton's Session View
        self._update_session_highlight()

    def _update_nav_leds(self):
        num_tracks = len(self.song().tracks)
        num_scenes = len(self.song().scenes)

        can_up    = self._scene_offset > 0
        can_down  = self._scene_offset + SESSION_ROWS < num_scenes
        can_left  = self._track_offset > 0
        can_right = self._track_offset + SESSION_COLS < num_tracks

        self._send_led(NAV_UP,    VEL_NAV_ON if can_up    else VEL_NAV_OFF)
        self._send_led(NAV_DOWN,  VEL_NAV_ON if can_down  else VEL_NAV_OFF)
        self._send_led(NAV_LEFT,  VEL_NAV_ON if can_left  else VEL_NAV_OFF)
        self._send_led(NAV_RIGHT, VEL_NAV_ON if can_right else VEL_NAV_OFF)

        # Turn off unused pads in the 3x3 area (corners + center)
        for note in CONTROL_NOTES - NAV_NOTES:
            self._send_led(note, 0)

    def _update_session_highlight(self):
        self._session.set_offsets(self._track_offset, self._scene_offset)

    def _all_leds_off(self):
        for row in PAD_NOTES:
            for note in row:
                if note >= 0:
                    self._send_midi((MIDI_NOTE_OFF, note, 0))
        self._last_sent = {}
