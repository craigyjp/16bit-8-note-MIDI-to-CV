# 16bit-8-note-MIDI-to-CV

* Collaboration with Andreas Oswald "MrsColumbo" Hierzenberger

Newer version of my old 6 note poly CV converter, this time it uses 16 bit DACs and has the facility for autotune (not yet implemented). 

Using a teensy 4.1, the CV's, Pitchbend, CC, gates and triggers will all need some level conversion in hardware which I've covered in the schematic PDF. I've used matching 12k/36k resistors on the CV DAC level converters to give 4x conversion and this gives 1v/octave, Velocity uses 10k/10k for 2x conversion for 0-5v velocity range.

* The triggers and gates are currently +5v.

* 16 CV ouputs in 2 banks of 8 for osc1 & osc2 configurations

* 8 filter CV outputs

* 8 velocity CV outputs

* 8 gate outputs

* 8 trigger outputs

* Pitchbend, channel aftertouch and CC outputs

* Sustain pedal support over MIDI

* MIDI Channel selection or Omni

* Poly1, Poly2, Unison & Mono modes with note priority modes

* Polyphony variable from 2-8 notes, 1 note not required as you already have unison and mono.

* Transpose Mode +/- one Octave

* Octave shift 0 to -3 Octaves to give 0V on bottom C

* Oscillator 2 detune (CV outputs 9-16)

* Oscillator 2 interval settings 0-12 semitones (CV outputs 9-16)

* Up/Down/Select buttons for menu

* USB MIDI Host support as well as 5 pin DIN

# During the testing all the controls were done manually with pots and buttons, but to be integrated into a polysynth these functions need to be controlled over MIDI.

* MIDI CC numbers used to control functions

* CC 01  Modulation Wheel 0-12
* CC 14  VCO_B Interval. 0-127 (0-12 semitones)
* CC 15  VCO_B Detune. 0-12
* CC 16  PitchBend Depth. 0-127 (0-12 seimitones)

CC 17  FM Mod Wheel Depth. 0-127

CC 18  TM Mod Wheel Depth. 0-127

CC 19  FM Aftertouch Depth. 0-127

CC 20  TM Aftertouch Depth. 0-127

CC 21  VCO_A Octave switch. (0-127) -1, 0, +1

CC 22  VCO_B Octave switch. (0-127) -1, 0, +1

CC 64  Sustain. 0-127  off < 63, on > 63

CC 121 Start Autotune Routine.  start > 63
