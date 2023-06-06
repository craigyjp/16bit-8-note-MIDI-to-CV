# 16bit-8-note-MIDI-to-CV

Newer version of my old 6 note poly CV converter, this time it uses 16 bit DACs and has the facility for autotune (not yet implemented)

Using a teensy 4.1, the CV's, Pitchbend, CC, gates and triggers will all need some level conversion in hardware which I've covered in the schematic PDF. I've used matching 10k resistors on the DAC level converters to give 2x conversion and this gives 1v/octave, the triggers and gates are currently +5v.

8 note polyphonic

8 velocity outputs

8 gate outputs

8 trigger outputs

Pitchbend and CC outputs

MIDI Channel selection or Omni

Poly/Unison/Mono modes with note modes

Transpose Mode +/- one Octave

Octave shift 0 to -3 Octaves to give 0V on bottom C

Scaling on individual notes to improve tuning

Up/Down/Select buttons for menu

USB MIDI Host support as well as 5 pin DIN

