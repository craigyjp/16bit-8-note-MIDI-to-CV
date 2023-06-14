/*
      8 note Poly MIDI to CV

      Version 6

      Copyright (C) 2020 Craig Barnes

      A big thankyou to Elkayem for his midi to cv code
      A big thankyou to ElectroTechnique for his polyphonic tsynth that I used for the poly notes routine

      This program is free software: you can redistribute it and/or modify
      it under the terms of the GNU General Public License as published by
      the Free Software Foundation, either version 3 of the License, or
      (at your option) any later version.

      This program is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
      GNU General Public License <http://www.gnu.org/licenses/> for more details.
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <Bounce2.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include <RoxMux.h>
#include <ShiftRegister74HC595.h>
#include "Parameters.h"
#include "Hardware.h"

// OLED I2C is used on pins 18 and 19 for Teensy 3.x

#define OLED_RESET 17
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int encoderPos, encoderPosPrev;

enum Menu {
  SETTINGS,
  KEYBOARD_MODE_SET_CH,
  MIDI_CHANNEL_SET_CH,
  TRANSPOSE_SET_CH,
  OCTAVE_SET_CH,
  POLYPHONY_COUNT,
  SCALE_FACTOR,
  SCALE_FACTOR_SET_CH
} menu;

char gateTrig[] = "TTTTTTT";

struct VoiceAndNote {
  int note;
  int velocity;
  long timeOn;
  bool sustained;  // Sustain flag
  bool keyDown;
  double noteFreq;  // Note frequency
  int position;
  bool noteOn;
};

struct VoiceAndNote voices[NO_OF_VOICES] = {
  { -1, -1, 0, false, false, 0, -1, false },
  { -1, -1, 0, false, false, 0, -1, false },
  { -1, -1, 0, false, false, 0, -1, false },
  { -1, -1, 0, false, false, 0, -1, false },
  { -1, -1, 0, false, false, 0, -1, false },
  { -1, -1, 0, false, false, 0, -1, false },
  { -1, -1, 0, false, false, 0, -1, false },
  { -1, -1, 0, false, false, 0, -1, false }
};

boolean voiceOn[NO_OF_VOICES] = { false, false, false, false, false, false, false, false };
int voiceToReturn = -1;        //Initialise to 'null'
long earliestTime = millis();  //For voice allocation - initialise to now
int prevNote = 0;              //Initialised to middle value
bool notes[128] = { 0 }, initial_loop = 1;
int8_t noteOrder[40] = { 0 }, orderIndx = { 0 };
bool S1, S2;

// MIDI setup

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
const int channel = 1;

#define OCTO_TOTAL 2
#define BTN_DEBOUNCE 50
RoxOctoswitch<OCTO_TOTAL, BTN_DEBOUNCE> octoswitch;
// pins for 74HC165
#define PIN_DATA 14  // pin 9 on 74HC165 (DATA)
#define PIN_LOAD 16  // pin 1 on 74HC165 (LOAD)
#define PIN_CLK 15   // pin 2 on 74HC165 (CLK))

ShiftRegister74HC595<3> sr(30, 31, 32);

void setup() {

  SPI.begin();

  setupHardware();

  octoswitch.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  octoswitch.setCallback(onButtonPress);



  //USB HOST MIDI Class Compliant
  delay(300);  //Wait to turn on USB Host
  myusb.begin();
  midi1.setHandleControlChange(myControlChange);
  midi1.setHandleNoteOff(myNoteOff);
  midi1.setHandleNoteOn(myNoteOn);
  midi1.setHandlePitchChange(myPitchBend);
  midi1.setHandleAfterTouchChannel(myAfterTouch);
  Serial.println("USB HOST MIDI Class Compliant Listening");

  //MIDI 5 Pin DIN
  MIDI.begin(masterChan);
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleControlChange(myControlChange);
  MIDI.setHandleAfterTouchChannel(myAfterTouch);
  Serial.println("MIDI In DIN Listening");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandlePitchChange(myPitchBend);
  usbMIDI.setHandleAfterTouchChannel(myAfterTouch);
  Serial.println("USB Client MIDI Listening");

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // OLED I2C Address, may need to change for different device,
  // Check with I2C_Scanner

  // Wire.setClock(100000L);  // Uncomment to slow down I2C speed

  // Read Settings from EEPROM
  for (int i = 0; i < 8; i++) {
    gateTrig[i] = (char)EEPROM.read(ADDR_GATE_TRIG + i);
    if (gateTrig[i] != 'G' || gateTrig[i] != 'T') EEPROM.write(ADDR_GATE_TRIG + i, 'T');
    gateTrig[i] = (char)EEPROM.read(ADDR_GATE_TRIG + i);
    EEPROM.get(ADDR_SF_ADJUST + i * sizeof(float), sfAdj[i]);
    if ((sfAdj[i] < 0.9f) || (sfAdj[i] > 1.1f) || isnan(sfAdj[i])) sfAdj[i] = 1.0f;
  }

  // Set defaults if EEPROM not initialized
  previousMode = EEPROM.read(ADDR_KEYBOARD_MODE);
  keyboardMode = EEPROM.read(ADDR_KEYBOARD_MODE);
  if (keyboardMode > 6 || keyboardMode < 0) {
    keyboardMode = 0;
    EEPROM.write(ADDR_KEYBOARD_MODE, keyboardMode);
  }

  transpose = EEPROM.read(ADDR_REAL_TRANSPOSE);
  masterTran = EEPROM.read(ADDR_TRANSPOSE);
  if (masterTran > 25 || masterTran < 0) {
    masterTran = 13;
    EEPROM.write(ADDR_TRANSPOSE, masterTran);
  }

  masterChan = EEPROM.read(ADDR_MASTER_CHAN);
  if (masterChan > 16 || masterChan < 0) {
    masterChan = 0;
    EEPROM.write(ADDR_MASTER_CHAN, masterChan);
  }

  octave = EEPROM.read(ADDR_OCTAVE);
  if (octave > 3 || octave < 0) {
    octave = 3;
    if (octave == 0) realoctave = -36;
    if (octave == 1) realoctave = -24;
    if (octave == 2) realoctave = -12;
    if (octave == 3) realoctave = 0;
    EEPROM.write(ADDR_OCTAVE, octave);
  }

  polyphony = (int)EEPROM.read(ADDR_NOTE_NUMBER);
  if (polyphony > 8 || polyphony < 1) {
    polyphony = 8;
    EEPROM.write(ADDR_NOTE_NUMBER, polyphony);
  }

  menu = SETTINGS;
  updateSelection();
}

void myPitchBend(byte channel, int bend) {
  if ((channel == masterChan) || (masterChan == 0)) {
    bend_data = int(bend * 0.395);
    sample_data = (channel_a & 0xFFF0000F) | (((int(bend * 0.395) + 13180) & 0xFFFF) << 4);
    outputDAC(DAC_NOTE5, sample_data);
  }
}

void myControlChange(byte channel, byte number, byte value) {
  if ((channel == masterChan) || (masterChan == 0)) {
    switch (number) {

      case 1:
        FM_RANGE_UPPER = int(value * FM_MOD_WHEEL);
        FM_RANGE_LOWER = (FM_RANGE_UPPER - FM_RANGE_UPPER - FM_RANGE_UPPER);
        TM_RANGE_UPPER = int(value * TM_MOD_WHEEL);
        TM_RANGE_LOWER = (TM_RANGE_UPPER - TM_RANGE_UPPER - TM_RANGE_UPPER);
        break;

      case 64:
        if (value > 63) {
          sustainOn = true;
          sustainNotes();
        } else {
          sustainOn = false;
          unsustainNotes();
        }
        break;
    }
  }
}

void myAfterTouch(byte channel, byte value) {
  if ((channel == masterChan) || (masterChan == 0)) {
    FM_AT_RANGE_UPPER = int(value * FM_AT_WHEEL);
    FM_AT_RANGE_LOWER = (FM_AT_RANGE_UPPER - FM_AT_RANGE_UPPER - FM_AT_RANGE_UPPER);
    TM_AT_RANGE_UPPER = int(value * TM_AT_WHEEL);
    TM_AT_RANGE_LOWER = (TM_AT_RANGE_UPPER - TM_AT_RANGE_UPPER - TM_AT_RANGE_UPPER);
  }
}

void mod_task() {

  MOD_VALUE = analogRead(FM_INPUT);
  FM_VALUE = map(MOD_VALUE, 0, 4095, FM_RANGE_LOWER, FM_RANGE_UPPER);
  FM_AT_VALUE = map(MOD_VALUE, 0, 4095, FM_AT_RANGE_LOWER, FM_AT_RANGE_UPPER);
  TM_VALUE = map(MOD_VALUE, 0, 4095, TM_RANGE_LOWER, TM_RANGE_UPPER);
  TM_AT_VALUE = map(MOD_VALUE, 0, 4095, TM_AT_RANGE_LOWER, TM_AT_RANGE_UPPER);
}

void mux_read() {
  mux1Read = adc->adc1->analogRead(MUX_IN);

  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR)) {
    mux1ValuesPrev[muxInput] = mux1Read;
    switch (muxInput) {
      case MUX1_FM_AT_DEPTH:  // AT depth to FM
        FM_AT_WHEEL = mux1Read;
        FM_AT_WHEEL = map(FM_AT_WHEEL, 0, 4095, 0, 16.12);
        break;
      case MUX1_TM_MOD_DEPTH:  // Modwheel Depth to TM
        TM_MOD_WHEEL = mux1Read;
        TM_MOD_WHEEL = map(TM_MOD_WHEEL, 0, 4095, 0, 16.12);
        break;
      case MUX1_TM_AT_DEPTH:  // AT depth to TM
        TM_AT_WHEEL = mux1Read;
        TM_AT_WHEEL = map(TM_AT_WHEEL, 0, 4095, 0, 16.12);
        break;
      case MUX1_FM_MOD_DEPTH:  // Modwheel depth to FM
        FM_MOD_WHEEL = mux1Read;
        FM_MOD_WHEEL = map(FM_MOD_WHEEL, 0, 4095, 0, 16.12);
        break;
      case MUX1_spare4:  // 4
        break;
      case MUX1_spare5:  // 5
        break;
      case MUX1_spare6:  // 6
        break;
      case MUX1_PB_DEPTH:  // 2
        BEND_WHEEL = mux1Read;
        BEND_WHEEL = map(BEND_WHEEL, 0, 4095, 0, 12);
        break;
    }
  }
  muxInput++;
  if (muxInput >= MUXCHANNELS)
    muxInput = 0;

  digitalWrite(ANA_MUX_S0, muxInput & B0001);
  digitalWrite(ANA_MUX_S1, muxInput & B0010);
  digitalWrite(ANA_MUX_S2, muxInput & B0100);
}

void unsustainNotes() {  // Unsustain notes
  for (int i = 0; i < (polyphony + 2); i++) {
    if (voices[i].keyDown) {
      voices[i].sustained = false;
      sendnote = voices[i].note;
      sendvelocity = voices[i].velocity;
      myNoteOff(masterChan, sendnote, sendvelocity);
    }
  }
}

void sustainNotes() {  // Sustain notes
  for (int i = 0; i < (polyphony + 2); i++) {
    if (voiceOn[i]) {
      voices[i].sustained = true;
    }
  }
}

void commandTopNote() {
  int topNote = 0;
  bool noteActive = false;

  for (int i = 0; i < 128; i++) {
    if (notes[i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNote(topNote);
  else  // All notes are off, turn off gate
    sr.set(GATE_NOTE1, LOW);
}

void commandBottomNote() {
  int bottomNote = 0;
  bool noteActive = false;

  for (int i = 87; i >= 0; i--) {
    if (notes[i]) {
      bottomNote = i;
      noteActive = true;
    }
  }

  if (noteActive)
    commandNote(bottomNote);
  else  // All notes are off, turn off gate
    sr.set(GATE_NOTE1, LOW);
}

void commandLastNote() {

  int8_t noteIndx;

  for (int i = 0; i < 40; i++) {
    noteIndx = noteOrder[mod(orderIndx - i, 40)];
    if (notes[noteIndx]) {
      commandNote(noteIndx);
      return;
    }
  }
  sr.set(GATE_NOTE1, LOW);  // All notes are off
}

void commandNote(int noteMsg) {
  note1 = noteMsg;
  sr.set(GATE_NOTE1, HIGH);
  sr.set(TRIG_NOTE1, HIGH);
  noteTrig[0] = millis();
}

void commandTopNoteUni() {
  int topNote = 0;
  bool noteActive = false;

  for (int i = 0; i < 128; i++) {
    if (notes[i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (noteActive) {
    commandNoteUni(topNote);
  } else {  // All notes are off, turn off gate
    sr.set(GATE_NOTE1, LOW);
    sr.set(GATE_NOTE2, LOW);
    sr.set(GATE_NOTE3, LOW);
    sr.set(GATE_NOTE4, LOW);
    sr.set(GATE_NOTE5, LOW);
    sr.set(GATE_NOTE6, LOW);
    sr.set(GATE_NOTE7, LOW);
    sr.set(GATE_NOTE8, LOW);
  }
}

void commandBottomNoteUni() {
  int bottomNote = 0;
  bool noteActive = false;

  for (int i = 87; i >= 0; i--) {
    if (notes[i]) {
      bottomNote = i;
      noteActive = true;
    }
  }

  if (noteActive) {
    commandNoteUni(bottomNote);
  } else {  // All notes are off, turn off gate
    sr.set(GATE_NOTE1, LOW);
    sr.set(GATE_NOTE2, LOW);
    sr.set(GATE_NOTE3, LOW);
    sr.set(GATE_NOTE4, LOW);
    sr.set(GATE_NOTE5, LOW);
    sr.set(GATE_NOTE6, LOW);
    sr.set(GATE_NOTE7, LOW);
    sr.set(GATE_NOTE8, LOW);
  }
}

void commandLastNoteUni() {

  int8_t noteIndx;

  for (int i = 0; i < 40; i++) {
    noteIndx = noteOrder[mod(orderIndx - i, 40)];
    if (notes[noteIndx]) {
      commandNoteUni(noteIndx);
      return;
    }
  }
  sr.set(GATE_NOTE1, LOW);
  sr.set(GATE_NOTE2, LOW);
  sr.set(GATE_NOTE3, LOW);
  sr.set(GATE_NOTE4, LOW);
  sr.set(GATE_NOTE5, LOW);
  sr.set(GATE_NOTE6, LOW);
  sr.set(GATE_NOTE7, LOW);
  sr.set(GATE_NOTE8, LOW);  // All notes are off
}

void commandNoteUni(int noteMsg) {

  note1 = noteMsg;
  note2 = noteMsg;
  note3 = noteMsg;
  note4 = noteMsg;
  note5 = noteMsg;
  note6 = noteMsg;
  note7 = noteMsg;
  note8 = noteMsg;

  sr.set(TRIG_NOTE1, HIGH);
  noteTrig[0] = millis();
  sr.set(GATE_NOTE1, HIGH);
  sr.set(TRIG_NOTE2, HIGH);
  noteTrig[1] = millis();
  sr.set(GATE_NOTE2, HIGH);
  sr.set(TRIG_NOTE3, HIGH);
  noteTrig[2] = millis();
  sr.set(GATE_NOTE3, HIGH);
  sr.set(TRIG_NOTE4, HIGH);
  noteTrig[3] = millis();
  sr.set(GATE_NOTE4, HIGH);
  sr.set(TRIG_NOTE5, HIGH);
  noteTrig[4] = millis();
  sr.set(GATE_NOTE5, HIGH);
  sr.set(TRIG_NOTE6, HIGH);
  noteTrig[5] = millis();
  sr.set(GATE_NOTE6, HIGH);
  sr.set(TRIG_NOTE7, HIGH);
  noteTrig[6] = millis();
  sr.set(GATE_NOTE7, HIGH);
  sr.set(TRIG_NOTE8, HIGH);
  noteTrig[7] = millis();
  sr.set(GATE_NOTE8, HIGH);
}

void myNoteOn(byte channel, byte note, byte velocity) {
  //Check for out of range notes
  if (note < 0 || note > 127) return;

  prevNote = note;
  switch (keyboardMode) {
    case 0:
      switch (getVoiceNo(-1)) {
        case 1:
          voices[0].note = note;
          note1 = note;
          voices[0].velocity = velocity;
          voices[0].timeOn = millis();
          voices[0].keyDown = true;
          sr.set(GATE_NOTE1, HIGH);
          sr.set(TRIG_NOTE1, HIGH);
          noteTrig[0] = millis();
          voiceOn[0] = true;
          break;

        case 2:
          voices[1].note = note;
          note2 = note;
          voices[1].velocity = velocity;
          voices[1].timeOn = millis();
          voices[1].keyDown = true;
          sr.set(GATE_NOTE2, HIGH);
          sr.set(TRIG_NOTE2, HIGH);
          noteTrig[1] = millis();
          voiceOn[1] = true;
          break;

        case 3:
          voices[2].note = note;
          note3 = note;
          voices[2].velocity = velocity;
          voices[2].timeOn = millis();
          voices[2].keyDown = true;
          sr.set(GATE_NOTE3, HIGH);
          sr.set(TRIG_NOTE3, HIGH);
          noteTrig[2] = millis();
          voiceOn[2] = true;
          break;

        case 4:
          voices[3].note = note;
          note4 = note;
          voices[3].velocity = velocity;
          voices[3].timeOn = millis();
          voices[3].keyDown = true;
          sr.set(GATE_NOTE4, HIGH);
          sr.set(TRIG_NOTE4, HIGH);
          noteTrig[3] = millis();
          voiceOn[3] = true;
          break;

        case 5:
          voices[4].note = note;
          note5 = note;
          voices[4].velocity = velocity;
          voices[4].timeOn = millis();
          voices[4].keyDown = true;
          sr.set(GATE_NOTE5, HIGH);
          sr.set(TRIG_NOTE5, HIGH);
          noteTrig[4] = millis();
          voiceOn[4] = true;
          break;

        case 6:
          voices[5].note = note;
          note6 = note;
          voices[5].velocity = velocity;
          voices[5].timeOn = millis();
          voices[5].keyDown = true;
          sr.set(GATE_NOTE6, HIGH);
          sr.set(TRIG_NOTE6, HIGH);
          noteTrig[5] = millis();
          voiceOn[5] = true;
          break;

        case 7:
          voices[6].note = note;
          note7 = note;
          voices[6].velocity = velocity;
          voices[6].timeOn = millis();
          voices[6].keyDown = true;
          sr.set(GATE_NOTE7, HIGH);
          sr.set(TRIG_NOTE7, HIGH);
          noteTrig[6] = millis();
          voiceOn[6] = true;
          break;

        case 8:
          voices[7].note = note;
          note8 = note;
          voices[7].velocity = velocity;
          voices[7].timeOn = millis();
          voices[7].keyDown = true;
          sr.set(GATE_NOTE8, HIGH);
          sr.set(TRIG_NOTE8, HIGH);
          noteTrig[7] = millis();
          voiceOn[7] = true;
          break;
      }
      break;

    case 1:
    case 2:
    case 3:
      if (keyboardMode == 1) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 2) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 3) {
        S1 = 0;
        S2 = 0;
      }
      noteMsg = note;

      if (velocity == 0) {
        notes[noteMsg] = false;
      } else {
        notes[noteMsg] = true;
      }

      voices[0].velocity = velocity;
      voices[1].velocity = velocity;
      voices[2].velocity = velocity;
      voices[3].velocity = velocity;
      voices[4].velocity = velocity;
      voices[5].velocity = velocity;
      voices[6].velocity = velocity;
      voices[7].velocity = velocity;

      if (S1 && S2) {  // Highest note priority
        commandTopNoteUni();
      } else if (!S1 && S2) {  // Lowest note priority
        commandBottomNoteUni();
      } else {                 // Last note priority
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNoteUni();
      }
      break;

    case 4:
    case 5:
    case 6:
      if (keyboardMode == 4) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 5) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 6) {
        S1 = 0;
        S2 = 0;
      }
      noteMsg = note;

      if (velocity == 0) {
        notes[noteMsg] = false;
      } else {
        notes[noteMsg] = true;
      }
      voices[0].velocity = velocity;

      if (S1 && S2) {  // Highest note priority
        commandTopNote();
      } else if (!S1 && S2) {  // Lowest note priority
        commandBottomNote();
      } else {                 // Last note priority
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNote();
      }
      break;
  }
}

void myNoteOff(byte channel, byte note, byte velocity) {
  switch (keyboardMode) {
    case 0:
      switch (getVoiceNo(note)) {
        case 1:
          if (!voices[0].sustained) {
            sr.set(GATE_NOTE1, LOW);
            voices[0].note = -1;
            voiceOn[0] = false;
            voices[0].keyDown = false;
          }
          break;
        case 2:
          if (!voices[1].sustained) {
            sr.set(GATE_NOTE2, LOW);
            voices[1].note = -1;
            voiceOn[1] = false;
            voices[1].keyDown = false;
          }
          break;
        case 3:
          if (!voices[2].sustained) {
            sr.set(GATE_NOTE3, LOW);
            voices[2].note = -1;
            voiceOn[2] = false;
            voices[2].keyDown = false;
          }
          break;
        case 4:
          if (!voices[3].sustained) {
            sr.set(GATE_NOTE4, LOW);
            voices[3].note = -1;
            voiceOn[3] = false;
            voices[3].keyDown = false;
          }
          break;
        case 5:
          if (!voices[4].sustained) {
            sr.set(GATE_NOTE5, LOW);
            voices[4].note = -1;
            voiceOn[4] = false;
            voices[4].keyDown = false;
          }
          break;
        case 6:
          if (!voices[5].sustained) {
            sr.set(GATE_NOTE6, LOW);
            voices[5].note = -1;
            voiceOn[5] = false;
            voices[5].keyDown = false;
          }
          break;
        case 7:
          if (!voices[6].sustained) {
            sr.set(GATE_NOTE7, LOW);
            voices[6].note = -1;
            voiceOn[6] = false;
            voices[6].keyDown = false;
          }
          break;
        case 8:
          if (!voices[7].sustained) {
            sr.set(GATE_NOTE8, LOW);
            voices[7].note = -1;
            voiceOn[7] = false;
            voices[7].keyDown = false;
          }
          break;
      }
      break;

    case 1:
    case 2:
    case 3:
      if (keyboardMode == 1) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 2) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 3) {
        S1 = 0;
        S2 = 0;
      }
      noteMsg = note;

      notes[noteMsg] = false;

      if (S1 && S2) {  // Highest note priority
        commandTopNoteUni();
      } else if (!S1 && S2) {  // Lowest note priority
        commandBottomNoteUni();
      } else {                 // Last note priority
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNoteUni();
      }
      break;

    case 4:
    case 5:
    case 6:
      if (keyboardMode == 4) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 5) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 6) {
        S1 = 0;
        S2 = 0;
      }
      noteMsg = note;

      notes[noteMsg] = false;

      if (S1 && S2) {  // Highest note priority
        commandTopNote();
      } else if (!S1 && S2) {  // Lowest note priority
        commandBottomNote();
      } else {                 // Last note priority
        if (notes[noteMsg]) {  // If note is on and using last note priority, add to ordered list
          orderIndx = (orderIndx + 1) % 40;
          noteOrder[orderIndx] = noteMsg;
        }
        commandLastNote();
      }
      break;
  }
}

int getVoiceNo(int note) {
  voiceToReturn = -1;       //Initialise to 'null'
  earliestTime = millis();  //Initialise to now
  if (note == -1) {
    //NoteOn() - Get the oldest free voice (recent voices may be still on release stage)
    for (int i = 0; i < (polyphony + 2); i++) {
      if (voices[i].note == -1) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    if (voiceToReturn == -1) {
      //No free voices, need to steal oldest sounding voice
      earliestTime = millis();  //Reinitialise
      for (int i = 0; i < (polyphony + 2); i++) {
        if (voices[i].timeOn < earliestTime) {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    return voiceToReturn + 1;
  } else {
    //NoteOff() - Get voice number from note
    for (int i = 0; i < (polyphony + 2); i++) {
      if (voices[i].note == note) {
        return i + 1;
      }
    }
  }
  //Shouldn't get here, return voice 1
  return 1;
}

void updateTimers() {

  if (millis() > noteTrig[0] + trigTimeout) {
    sr.set(TRIG_NOTE1, LOW);  // Set trigger low after 20 msec
  }

  if (millis() > noteTrig[1] + trigTimeout) {
    sr.set(TRIG_NOTE2, LOW);  // Set trigger low after 20 msec
  }

  if (millis() > noteTrig[2] + trigTimeout) {
    sr.set(TRIG_NOTE3, LOW);  // Set trigger low after 20 msec
  }

  if (millis() > noteTrig[3] + trigTimeout) {
    sr.set(TRIG_NOTE4, LOW);  // Set trigger low after 20 msec
  }

  if (millis() > noteTrig[4] + trigTimeout) {
    sr.set(TRIG_NOTE5, LOW);  // Set trigger low after 20 msec
  }

  if (millis() > noteTrig[5] + trigTimeout) {
    sr.set(TRIG_NOTE6, LOW);  // Set trigger low after 20 msec
  }

  if (millis() > noteTrig[6] + trigTimeout) {
    sr.set(TRIG_NOTE7, LOW);  // Set trigger low after 20 msec
  }

  if (millis() > noteTrig[7] + trigTimeout) {
    sr.set(TRIG_NOTE8, LOW);  // Set trigger low after 20 msec
  }
}

void updateVoice1() {
  unsigned int mV = (unsigned int)(((float)(note1 + transpose + realoctave) * NOTE_SF * sfAdj[0] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data1 = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(note1 + transpose + realoctave) * NOTE_SF * sfAdj[0] + 0.5) + (TM_VALUE));
  sample_data1 = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data1);
  unsigned int velmV = ((unsigned int)((float)voices[0].velocity) * VEL_SF);
  vel_data1 = (channel_a & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data1);
}

void updateVoice2() {
  unsigned int mV = (unsigned int)(((float)(note2 + transpose + realoctave) * NOTE_SF * sfAdj[1] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data2 = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data2);
  outputDAC(DAC_NOTE2, sample_data2);
  mV = (unsigned int)(((float)(note2 + transpose + realoctave) * NOTE_SF * sfAdj[1] + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data2 = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data2);
  unsigned int velmV = ((unsigned int)((float)voices[1].velocity) * VEL_SF);
  vel_data2 = (channel_b & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data2);
}

void updateVoice3() {
  unsigned int mV = (unsigned int)(((float)(note3 + transpose + realoctave) * NOTE_SF * sfAdj[2] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data3 = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data3);
  outputDAC(DAC_NOTE2, sample_data3);
  mV = (unsigned int)(((float)(note3 + transpose + realoctave) * NOTE_SF * sfAdj[2] + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data3 = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data3);
  unsigned int velmV = ((unsigned int)((float)voices[2].velocity) * VEL_SF);
  vel_data3 = (channel_c & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data3);
}

void updateVoice4() {
  unsigned int mV = (unsigned int)(((float)(note4 + transpose + realoctave) * NOTE_SF * sfAdj[3] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data4 = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data4);
  outputDAC(DAC_NOTE2, sample_data4);
  mV = (unsigned int)(((float)(note4 + transpose + realoctave) * NOTE_SF * sfAdj[3] + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data4 = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data4);
  unsigned int velmV = ((unsigned int)((float)voices[3].velocity) * VEL_SF);
  vel_data4 = (channel_d & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data4);
}

void updateVoice5() {
  unsigned int mV = (unsigned int)(((float)(note5 + transpose + realoctave) * NOTE_SF * sfAdj[4] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data5 = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data5);
  outputDAC(DAC_NOTE2, sample_data5);
  mV = (unsigned int)(((float)(note5 + transpose + realoctave) * NOTE_SF * sfAdj[4] + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data5 = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data5);
  unsigned int velmV = ((unsigned int)((float)voices[4].velocity) * VEL_SF);
  vel_data5 = (channel_e & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data5);
}

void updateVoice6() {
  unsigned int mV = (unsigned int)(((float)(note6 + transpose + realoctave) * NOTE_SF * sfAdj[5] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data6 = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data6);
  outputDAC(DAC_NOTE2, sample_data6);
  mV = (unsigned int)(((float)(note6 + transpose + realoctave) * NOTE_SF * sfAdj[5] + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data6 = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data6);
  unsigned int velmV = ((unsigned int)((float)voices[5].velocity) * VEL_SF);
  vel_data6 = (channel_f & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data6);
}

void updateVoice7() {
  unsigned int mV = (unsigned int)(((float)(note7 + transpose + realoctave) * NOTE_SF * sfAdj[6] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data7 = (channel_g & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data7);
  outputDAC(DAC_NOTE2, sample_data7);
  mV = (unsigned int)(((float)(note7 + transpose + realoctave) * NOTE_SF * sfAdj[6] + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data7 = (channel_g & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data7);
  unsigned int velmV = ((unsigned int)((float)voices[6].velocity) * VEL_SF);
  vel_data7 = (channel_g & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data7);
}

void updateVoice8() {
  unsigned int mV = (unsigned int)(((float)(note8 + transpose + realoctave) * NOTE_SF * sfAdj[7] + 0.5) + (bend_data + FM_VALUE + FM_AT_VALUE));
  sample_data8 = (channel_h & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data8);
  outputDAC(DAC_NOTE2, sample_data8);
  mV = (unsigned int)(((float)(note8 + transpose + realoctave) * NOTE_SF * sfAdj[7] + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data8 = (channel_h & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data8);
  unsigned int velmV = ((unsigned int)((float)voices[7].velocity) * VEL_SF);
  vel_data8 = (channel_h & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data8);
}

void allNotesOff() {
  sr.set(GATE_NOTE1, LOW);
  sr.set(GATE_NOTE2, LOW);
  sr.set(GATE_NOTE3, LOW);
  sr.set(GATE_NOTE4, LOW);
  sr.set(GATE_NOTE5, LOW);
  sr.set(GATE_NOTE6, LOW);
  sr.set(GATE_NOTE7, LOW);
  sr.set(GATE_NOTE7, LOW);

  voices[0].note = -1;
  voices[1].note = -1;
  voices[2].note = -1;
  voices[3].note = -1;
  voices[4].note = -1;
  voices[5].note = -1;
  voices[6].note = -1;
  voices[7].note = -1;

  voiceOn[0] = false;
  voiceOn[1] = false;
  voiceOn[2] = false;
  voiceOn[3] = false;
  voiceOn[4] = false;
  voiceOn[5] = false;
  voiceOn[6] = false;
  voiceOn[7] = false;
}

void onButtonPress(uint16_t btnIndex, uint8_t btnType) {

  if (btnIndex == ENC_A && btnType == ROX_PRESSED) {
    static int encoderA, encoderB, encoderA_prev;

    if (highlightEnabled) {  // Update encoder position
      encoderPosPrev = encoderPos;
      encoderB ? encoderPos++ : encoderPos--;
    } else {
      highlightEnabled = true;
      encoderPos = 0;  // Reset encoder position if highlight timed out
      encoderPosPrev = 0;
    }
    highlightTimer = millis();
    updateSelection();
    encoderA_prev = encoderA;
  }

  if (btnIndex == ENC_B && btnType == ROX_PRESSED) {
    static int encoderA, encoderB, encoderB_prev;

    if (highlightEnabled) {  // Update encoder position
      encoderPosPrev = encoderPos;
      encoderA ? encoderPos-- : encoderPos++;
    } else {
      highlightEnabled = true;
      encoderPos = 0;  // Reset encoder position if highlight timed out
      encoderPosPrev = 0;
    }
    highlightTimer = millis();
    updateSelection();
    encoderB_prev = encoderB;
  }

  if (btnIndex == ENC_BTN && btnType == ROX_PRESSED) {
    updateMenu();
  }
}

void menuTimeOut() {

  // Check if highlighting timer expired, and remove highlighting if so
  if (highlightEnabled && ((millis() - highlightTimer) > HIGHLIGHT_TIMEOUT)) {
    highlightEnabled = false;
    menu = SETTINGS;    // Return to top level menu
    updateSelection();  // Refresh screen without selection highlight
  }
}

void loop() {

  updateTimers();
  menuTimeOut();
  myusb.Task();
  midi1.read(masterChan);    //USB HOST MIDI Class Compliant
  MIDI.read(masterChan);     //MIDI 5 Pin DIN
  usbMIDI.read(masterChan);  //USB Client MIDI
  mod_task();
  mux_read();
  updateVoice1();
  updateVoice2();
  updateVoice3();
  updateVoice4();
  updateVoice5();
  updateVoice6();
  updateVoice7();
  updateVoice8();
  octoswitch.update();
}

void outputDAC(int CHIP_SELECT, uint32_t sample_data) {
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(CHIP_SELECT, LOW);
  SPI.transfer32(sample_data);
  delayMicroseconds(8);  // Settling time delay
  digitalWrite(CHIP_SELECT, HIGH);
  SPI.endTransaction();
}

int setCh;
char setMode[6];

void updateMenu() {  // Called whenever button is pushed

  if (highlightEnabled) {  // Highlight is active, choose selection
    switch (menu) {
      case SETTINGS:
        switch (mod(encoderPos, 6)) {
          case 0:
            menu = KEYBOARD_MODE_SET_CH;
            break;
          case 1:
            menu = MIDI_CHANNEL_SET_CH;
            break;
          case 2:
            menu = TRANSPOSE_SET_CH;
            break;
          case 3:
            menu = OCTAVE_SET_CH;
            break;
          case 4:
            menu = POLYPHONY_COUNT;
            break;
          case 5:
            menu = SCALE_FACTOR;
            break;
        }
        break;

      case KEYBOARD_MODE_SET_CH:  // Save keyboard mode setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_KEYBOARD_MODE, keyboardMode);
        break;

      case MIDI_CHANNEL_SET_CH:  // Save midi channel setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_MASTER_CHAN, masterChan);
        break;

      case TRANSPOSE_SET_CH:  // Save transpose setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_TRANSPOSE, masterTran);
        EEPROM.write(ADDR_REAL_TRANSPOSE, masterTran - 12);
        transpose = (masterTran - 12);
        break;

      case OCTAVE_SET_CH:  // Save octave adjust setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_OCTAVE, octave);
        if (octave == 0) realoctave = -36;
        if (octave == 1) realoctave = -24;
        if (octave == 2) realoctave = -12;
        if (octave == 3) realoctave = 0;
        EEPROM.write(ADDR_REALOCTAVE, realoctave);
        break;

      case POLYPHONY_COUNT:  // Save polyphony mode setting to EEPROM
        menu = SETTINGS;
        EEPROM.write(ADDR_NOTE_NUMBER, polyphony);
        break;

      case SCALE_FACTOR:
        setCh = mod(encoderPos, 7);
        switch (setCh) {
          case 0:
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
            menu = SCALE_FACTOR_SET_CH;
            break;
          case 6:
            menu = SETTINGS;
            break;
        }
        break;

      case SCALE_FACTOR_SET_CH:  // Save scale factor to EEPROM
        menu = SCALE_FACTOR;
        EEPROM.put(ADDR_SF_ADJUST + setCh * sizeof(float), sfAdj[setCh]);
        break;
    }
  } else {  // Highlight wasn't visible, reinitialize highlight timer
    highlightTimer = millis();
    highlightEnabled = true;
  }
  encoderPos = 0;  // Reset encoder position
  encoderPosPrev = 0;
  updateSelection();  // Refresh screen
}

void updateSelection() {  // Called whenever encoder is turned
  display.clearDisplay();
  switch (menu) {
    case KEYBOARD_MODE_SET_CH:
      if (menu == KEYBOARD_MODE_SET_CH) keyboardMode = mod(encoderPos, 7);

    case MIDI_CHANNEL_SET_CH:
      if (menu == MIDI_CHANNEL_SET_CH) masterChan = mod(encoderPos, 17);

    case TRANSPOSE_SET_CH:
      if (menu == TRANSPOSE_SET_CH) masterTran = mod(encoderPos, 25);

    case OCTAVE_SET_CH:
      if (menu == OCTAVE_SET_CH) octave = mod(encoderPos, 4);

    case POLYPHONY_COUNT:
      if (menu == POLYPHONY_COUNT) {
      polyphony = mod(encoderPos, 7);
      }

    case SETTINGS:
      display.setCursor(0, 0);
      display.setTextColor(WHITE, BLACK);
      display.print(F("SETTINGS"));
      display.setCursor(0, 16);

      if (menu == SETTINGS) setHighlight(0, 6);
      display.print(F("Keyboard Mode "));
      if (menu == KEYBOARD_MODE_SET_CH) display.setTextColor(BLACK, WHITE);
      if (keyboardMode == 0) display.print("Poly  ");
      if (keyboardMode == 1) display.print("Uni T");
      if (keyboardMode == 2) display.print("Uni B");
      if (keyboardMode == 3) display.print("Uni L");
      if (keyboardMode == 4) display.print("Mono T ");
      if (keyboardMode == 5) display.print("Mono B ");
      if (keyboardMode == 6) display.print("Mono L ");
      display.println(F(""));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(1, 6);
      display.print(F("Midi Channel  "));
      if (menu == MIDI_CHANNEL_SET_CH) display.setTextColor(BLACK, WHITE);
      if (masterChan == 0) display.print("Omni");
      else display.print(masterChan);
      display.println(F(" "));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(2, 6);
      display.print(F("Transpose     "));
      if (menu == TRANSPOSE_SET_CH) display.setTextColor(BLACK, WHITE);
      display.print(masterTran - 12);
      display.println(F(" "));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(3, 6);
      display.print(F("Octave Adjust "));
      if (menu == OCTAVE_SET_CH) display.setTextColor(BLACK, WHITE);
      if (octave == 0) display.print("-3 ");
      if (octave == 1) display.print("-2 ");
      if (octave == 2) display.print("-1 ");
      if (octave == 3) display.print(" 0 ");
      display.println(F(" "));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(4, 6);
      display.print(F("Polyphony Count  "));
      if (menu == POLYPHONY_COUNT) display.setTextColor(BLACK, WHITE);
      display.print(polyphony + 2);
      display.println(F(" "));
      display.setTextColor(WHITE, BLACK);

      if (menu == SETTINGS) setHighlight(5, 6);
      else display.setTextColor(WHITE, BLACK);
      display.println(F("Scale Factor     "));
      break;

    case SCALE_FACTOR_SET_CH:
      if ((encoderPos > encoderPosPrev) && (sfAdj[setCh] < 1.1))
        sfAdj[setCh] += 0.001f;
      else if ((encoderPos < encoderPosPrev) && (sfAdj[setCh] > 0.9))
        sfAdj[setCh] -= 0.001f;

    case SCALE_FACTOR:
      display.setCursor(0, 0);
      display.setTextColor(WHITE, BLACK);
      display.print(F("SET SCALE FACTOR"));
      display.setCursor(0, 8);

      if (menu == SCALE_FACTOR) setHighlight(0, 7);
      display.print(F("Note 1:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 0) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[0], 3);

      if (menu == SCALE_FACTOR) setHighlight(1, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 2:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 1) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[1], 3);

      if (menu == SCALE_FACTOR) setHighlight(2, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 3:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 2) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[2], 3);

      if (menu == SCALE_FACTOR) setHighlight(3, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 4:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 3) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[3], 3);

      if (menu == SCALE_FACTOR) setHighlight(4, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 5:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 4) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[4], 3);

      if (menu == SCALE_FACTOR) setHighlight(5, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Note 6:    "));
      if ((menu == SCALE_FACTOR_SET_CH) && setCh == 5) display.setTextColor(BLACK, WHITE);
      display.println(sfAdj[5], 3);

      if (menu == SCALE_FACTOR) setHighlight(6, 7);
      else display.setTextColor(WHITE, BLACK);
      display.print(F("Return      "));
  }
  display.display();
}

void setHighlight(int menuItem, int numMenuItems) {
  if ((mod(encoderPos, numMenuItems) == menuItem) && highlightEnabled) {
    display.setTextColor(BLACK, WHITE);
  } else {
    display.setTextColor(WHITE, BLACK);
  }
}

int mod(int a, int b) {
  int r = a % b;
  return r < 0 ? r + b : r;
}
