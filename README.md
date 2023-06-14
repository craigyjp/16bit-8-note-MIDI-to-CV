# 16bit-8-note-MIDI-to-CV

* Collaboration with Andreas Oswald Hierzenberger

Newer version of my old 6 note poly CV converter, this time it uses 16 bit DACs and has the facility for autotune (not yet implemented). 

Using a teensy 4.1, the CV's, Pitchbend, CC, gates and triggers will all need some level conversion in hardware which I've covered in the schematic PDF. I've used matching 12k/36k resistors on the CV DAC level converters to give 4x conversion and this gives 1v/octave, Velocity uses 10k/10k for 2x conversion for 0-5v velocity range.

The triggers and gates are currently +5v.

16 CV ouputs in 2 banks of 8 for osc1 & osc2 configurations

8 filter CV outputs

8 velocity outputs

8 gate outputs

8 trigger outputs

Pitchbend, channel aftertouch and CC outputs

Sustain pedal support over MIDI

MIDI Channel selection or Omni

Poly/Unison/Mono modes with note modes

Poly note variable from 2-8 notes

Transpose Mode +/- one Octave

Octave shift 0 to -3 Octaves to give 0V on bottom C

Scaling on individual notes to improve tuning

Up/Down/Select buttons for menu

USB MIDI Host support as well as 5 pin DIN

