/*
      8 note Poly MIDI to CV

      Version 1.8

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
#include <iostream>
#include <cfloat>  // Include for DBL_MAX
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <Bounce2.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include <RoxMux.h>
#include <ShiftRegister74HC595.h>
#include <FreqMeasureMulti.h>
#include "Parameters.h"
#include "Hardware.h"

// OLED I2C is used on pins 18 and 19 for Teensy 3.x

#define OLED_RESET 17
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum Menu {
  SETTINGS,
  KEYBOARD_MODE_SET_CH,
  MIDI_CHANNEL_SET_CH,
  TRANSPOSE_SET_CH,
  OCTAVE_SET_CH,
  POLYPHONY_COUNT,
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

// Struct to store note data with derivatives
struct NoteData {
  int value;
  double derivative;
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
FreqMeasureMulti autosignal;

boolean cardStatus = false;
File tuningFile;

void setup() {

  SPI.begin();


  delay(10);
  autosignal.begin(22, FREQMEASUREMULTI_INTERLEAVE);
  setupHardware();

  octoswitch.begin(PIN_DATA, PIN_LOAD, PIN_CLK);
  octoswitch.setCallback(onButtonPress);

  // Initialize SD card
  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("SD card initialization failed!");
    while (1)
      ;
  }
  Serial.println("SD card initialized.");

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

  // Read Settings from EEPROM
  for (int i = 0; i < 8; i++) {
    gateTrig[i] = (char)EEPROM.read(ADDR_GATE_TRIG + i);
    if (gateTrig[i] != 'G' || gateTrig[i] != 'T') EEPROM.write(ADDR_GATE_TRIG + i, 'T');
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

  polyphony = EEPROM.read(ADDR_NOTE_NUMBER);
  if (polyphony > 8 || polyphony < 1) {
    polyphony = 8;
    EEPROM.write(ADDR_NOTE_NUMBER, polyphony);
  }

  loadTuningCorrectionsFromSD();

  menu = SETTINGS;
  updateSelection();
}

void saveTuningCorrectionsToSD() {
  if (SD.exists("tuning.txt")) {
    SD.remove("tuning.txt");  // Remove any existing file
  }

  File file = SD.open("tuning.txt", FILE_WRITE);
  if (file) {
    for (int o = 0; o < (numOscillators + 1); o++) {
      for (int i = 0; i < 128; i++) {
        file.print(i);                       // Note
        file.print(",");                     // Delimiter
        file.print(o);                       // Oscillator
        file.print(",");                     // Delimiter
        file.println(autotune_value[i][o]);  // Value
      }
    }
    file.close();
    Serial.println("Autotune values saved as strings.");
  } else {
    Serial.println("Error opening file for writing.");
  }
}

void loadTuningCorrectionsFromSD() {
  File tuningFile = SD.open("tuning.txt", FILE_READ);
  if (tuningFile) {
    while (tuningFile.available()) {
      String line = tuningFile.readStringUntil('\n');  // Read a line
      int comma1 = line.indexOf(',');                  // Find the first comma
      int comma2 = line.lastIndexOf(',');              // Find the last comma

      // Extract and parse note, oscillator, and value
      int note = line.substring(0, comma1).toInt();
      int osc = line.substring(comma1 + 1, comma2).toInt();
      int value = line.substring(comma2 + 1).toInt();

      if (note >= 0 && note < 128 && osc >= 0 && osc < (numOscillators + 1)) {
        autotune_value[note][osc] = value;
      } else {
        Serial.println("Invalid data format in tuning file.");
      }
    }
    tuningFile.close();
    Serial.println("Tuning corrections loaded from strings.");
  } else {
    Serial.println("Failed to open tuning file for reading.");
  }
}

double measureAverageFrequency(int samples = 5) {
  double totalFreq = 0;
  for (int i = 0; i < samples; i++) {
    totalFreq += measureFrequency();
    delay(5);  // Short delay for stability
  }
  return totalFreq / samples;
}

void autotune() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextColor(WHITE, BLACK);
  display.println(F("Autotune in progress:"));
  display.println(F(""));
  display.print(F("Oscillator  :"));
  display.print(oscillator / 2 + 1);
  display.print(oscillator % 2 == 0 ? "A" : "B");
  display.print("  ");
  display.println(A_NOTES[tuneNote]);

  display.print(F("Tune value  :"));
  display.setCursor(78, 32);
  display.println("    ");
  display.display();

  int midiNote = A_NOTES[tuneNote];
  double targetFrequency = midi_to_freqs[midiNote][1];

  setOscillator(midiNote, oscillator);
  delay(200);  // Allow stabilization

  double initialFreq = measureAverageFrequency(5);  // Average initial measurement

  int minError = -255;
  int maxError = 255;
  int bestError = 0;
  double bestFreqDiff = DBL_MAX;

  while (minError <= maxError) {
    int midError = (minError + maxError) / 2;

    updateOscillator(midiNote, midError);
    delay(50);                                         // Allow stabilization
    double measuredFreq = measureAverageFrequency(5);  // Averaged measurement
    double freqDiff = abs(measuredFreq - targetFrequency);


    if (freqDiff < bestFreqDiff) {
      bestFreqDiff = freqDiff;
      bestError = midError;
    }

    if (measuredFreq < targetFrequency) {
      minError = midError + 1;  // Increase offset
    } else if (measuredFreq > targetFrequency) {
      maxError = midError - 1;  // Decrease offset
    } else {
      bestError = midError;  // Exact match found
      break;
    }
  }

  autotune_value[midiNote][oscillator] = bestError;

  tuneNote++;
  if (tuneNote >= A_NOTES_COUNT) {
    tuneNote = 0;
    oscillator++;
    if (oscillator > numOscillators) {
      autotuneStart = false;
      extrapolateNotes();
      Serial.println("Auto Tune Finished");
      digitalWriteFast(MUX_ENABLE, LOW);
      sr.set(AUTOTUNE_LED, LOW);
      updateSelection();
    } else {
      digitalWriteFast(MUX_ENABLE, LOW);
      selectMuxInput();
      delay(50);  // Stabilize MUX switching
      digitalWriteFast(MUX_ENABLE, HIGH);
    }
  }
}

void setOscillator(int midiNote, int oscillator) {
  switch (oscillator) {
    case 0:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator1a, channel_a);
      break;
    case 1:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator1b, channel_a);
      break;
    case 2:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator2a, channel_b);
      break;
    case 3:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator2b, channel_b);
      break;
    case 4:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator3a, channel_c);
      break;
    case 5:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator3b, channel_c);
      break;
    case 6:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator4a, channel_d);
      break;
    case 7:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator4b, channel_d);
      break;
    case 8:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator5a, channel_e);
      break;
    case 9:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator5b, channel_e);
      break;
    case 10:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator6a, channel_f);
      break;
    case 11:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator6b, channel_f);
      break;
    case 12:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator7a, channel_g);
      break;
    case 13:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator7b, channel_g);
      break;
    case 14:
      setDAC(DAC_NOTE1, midiNote, (autotune_value[midiNote][oscillator]), oscillator8a, channel_h);
      break;
    case 15:
      setDAC(DAC_NOTE2, midiNote, (autotune_value[midiNote][oscillator]), oscillator8b, channel_h);
      break;
  }
}

void updateOscillator(int note, int error) {
  switch (oscillator) {
    case 0:
      setDAC(DAC_NOTE1, note, error, oscillator1a, channel_a);
      break;
    case 1:
      setDAC(DAC_NOTE2, note, error, oscillator1b, channel_a);
      break;
    case 2:
      setDAC(DAC_NOTE1, note, error, oscillator2a, channel_b);
      break;
    case 3:
      setDAC(DAC_NOTE2, note, error, oscillator2b, channel_b);
      break;
    case 4:
      setDAC(DAC_NOTE1, note, error, oscillator3a, channel_c);
      break;
    case 5:
      setDAC(DAC_NOTE2, note, error, oscillator3b, channel_c);
      break;
    case 6:
      setDAC(DAC_NOTE1, note, error, oscillator4a, channel_d);
      break;
    case 7:
      setDAC(DAC_NOTE2, note, error, oscillator4b, channel_d);
      break;
    case 8:
      setDAC(DAC_NOTE1, note, error, oscillator5a, channel_e);
      break;
    case 9:
      setDAC(DAC_NOTE2, note, error, oscillator5b, channel_e);
      break;
    case 10:
      setDAC(DAC_NOTE1, note, error, oscillator6a, channel_f);
      break;
    case 11:
      setDAC(DAC_NOTE2, note, error, oscillator6b, channel_f);
      break;
    case 12:
      setDAC(DAC_NOTE1, note, error, oscillator7a, channel_g);
      break;
    case 13:
      setDAC(DAC_NOTE2, note, error, oscillator7b, channel_g);
      break;
    case 14:
      setDAC(DAC_NOTE1, note, error, oscillator8a, channel_h);
      break;
    case 15:
      setDAC(DAC_NOTE2, note, error, oscillator8b, channel_h);
      break;
  }
}

double measureFrequency() {
  int count = 0;
  double sum = 0;

  if (A_NOTES[tuneNote] == 33) {
    count = 0;
    sum = 0;
    while (count < 15) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  if (A_NOTES[tuneNote] == 45) {
    count = 0;
    sum = 0;
    while (count < 20) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  if (A_NOTES[tuneNote] == 57) {
    count = 0;
    sum = 0;
    while (count < 25) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  if (A_NOTES[tuneNote] == 69) {
    count = 0;
    sum = 0;
    while (count < 50) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  if (A_NOTES[tuneNote] == 81) {
    count = 0;
    sum = 0;
    while (count < 70) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  if (A_NOTES[tuneNote] == 93) {
    count = 0;
    sum = 0;
    while (count < 100) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  if (A_NOTES[tuneNote] == 105) {
    count = 0;
    sum = 0;
    while (count < 150) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  if (A_NOTES[tuneNote] == 117) {
    count = 0;
    sum = 0;
    while (count < 200) {
      if (autosignal.available()) {
        sum += autosignal.read();
        count++;
      }
    }
    measuredFrequency = autosignal.countToFrequency(sum / count);
  }

  return measuredFrequency;
}

void setDAC(int chipSelect, int note, int error, float oscillator, uint32_t channel) {
  int mV = (int)(((float)(note)*NOTE_SF * oscillator + 0.5) + (VOLTOFFSET + error));
  mV = constrain(mV, 0, 65535);  // Ensure mV is within DAC range (0 to 65535 for a 16-bit DAC)
  uint32_t sampleData = (channel & 0xFFF0000F) | (((mV)&0xFFFF) << 4);

  outputDAC(chipSelect, sampleData);
}

//////////////////////////////////////////////////////////////////////////////

void extrapolateNotes() {
  for (int o = 0; o <= numOscillators; o++) {
    // Extrapolate and interpolate across the full MIDI note range (0-127)
    for (int i = 0; i < 128; i++) {
      // Find the nearest lower and higher known notes in A_NOTES
      int lowerNote = -1;
      int higherNote = -1;

      for (int j = 0; j < A_NOTES_COUNT; j++) {
        if (A_NOTES[j] <= i) {
          lowerNote = A_NOTES[j];
        }
        if (A_NOTES[j] > i) {
          higherNote = A_NOTES[j];
          break;
        }
      }

      if (lowerNote != -1 && higherNote != -1) {
        // Interpolation between lowerNote and higherNote
        int lowerValue = autotune_value[lowerNote][o];
        int higherValue = autotune_value[higherNote][o];
        double t = static_cast<double>(i - lowerNote) / (higherNote - lowerNote);
        autotune_value[i][o] = constrain(
          static_cast<int>(lowerValue + t * (higherValue - lowerValue)),
          -32768, 32767);
      } else if (lowerNote != -1) {
        // Extrapolation below the first known note
        int lowerValue = autotune_value[lowerNote][o];
        autotune_value[i][o] = constrain(lowerValue, -32768, 32767);
      } else if (higherNote != -1) {
        // Extrapolation above the last known note
        int higherValue = autotune_value[higherNote][o];
        autotune_value[i][o] = constrain(higherValue, -32768, 32767);
      }
    }
  }

  saveTuningCorrectionsToSD();
}

//////////////////////////////////////////////////////////////////////

void loadSDCardNow() {
  loadTuningCorrectionsFromSD();
}

void loop() {

  if (autotuneStart) {
    autotune();
  } else {
    updateTimers();
    menuTimeOut();
    myusb.Task();
    midi1.read(masterChan);    //USB HOST MIDI Class Compliant
    MIDI.read(masterChan);     //MIDI 5 Pin DIN
    usbMIDI.read(masterChan);  //USB Client MIDI
    mod_task();
    adjustInterval();
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
}

void setVCOStolowestA() {
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator1a + 0.5) + (VOLTOFFSET + autotune_value[33][0]));
  sample_data1 = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator1b + 0.5) + (VOLTOFFSET + autotune_value[33][1]));
  sample_data1 = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator2a + 0.5) + (VOLTOFFSET + autotune_value[33][2]));
  sample_data1 = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator2b + 0.5) + (VOLTOFFSET + autotune_value[33][3]));
  sample_data1 = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator3a + 0.5) + (VOLTOFFSET + autotune_value[33][4]));
  sample_data1 = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator3b + 0.5) + (VOLTOFFSET + autotune_value[33][5]));
  sample_data1 = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator4a + 0.5) + (VOLTOFFSET + autotune_value[33][6]));
  sample_data1 = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator4b + 0.5) + (VOLTOFFSET + autotune_value[33][7]));
  sample_data1 = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator5a + 0.5) + (VOLTOFFSET + autotune_value[33][8]));
  sample_data1 = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator5b + 0.5) + (VOLTOFFSET + autotune_value[33][9]));
  sample_data1 = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator6a + 0.5) + (VOLTOFFSET + autotune_value[33][10]));
  sample_data1 = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator6b + 0.5) + (VOLTOFFSET + autotune_value[33][11]));
  sample_data1 = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator7a + 0.5) + (VOLTOFFSET + autotune_value[33][12]));
  sample_data1 = (channel_g & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator7b + 0.5) + (VOLTOFFSET + autotune_value[33][13]));
  sample_data1 = (channel_g & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator8a + 0.5) + (VOLTOFFSET + autotune_value[33][14]));
  sample_data1 = (channel_h & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  mV = (unsigned int)(((float)(33) * NOTE_SF * oscillator8b + 0.5) + (VOLTOFFSET + autotune_value[33][15]));
  sample_data1 = (channel_h & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
}

void startAutotune() {
  sr.set(AUTOTUNE_LED, HIGH);
  display.clearDisplay();
  autotuneStart = true;
  oscillator = 0;
  tuneNote = 0;
  setVCOStolowestA();
  delay(200);
  workingNote = A_NOTES[tuneNote];
  targetFrequency = midi_to_freqs[workingNote][1];
  digitalWrite(MUX_S0, LOW);
  digitalWrite(MUX_S1, LOW);
  digitalWrite(MUX_S2, LOW);
  digitalWrite(MUX_S3, LOW);
  delayMicroseconds(100);
  digitalWriteFast(MUX_ENABLE, HIGH);  // Enable the mux
  delayMicroseconds(100);
}

void ResetAutoTuneValues() {
  for (int o = 0; o < (numOscillators + 1); o++) {
    for (int i = 0; i < 128; i++) {
      (autotune_value[i][o]) = 0;
    }
  }
  Serial.print("All Autotune Values are 0");
  Serial.println();
  Serial.print("Writing all 0 to SD card");
  Serial.println();
  saveTuningCorrectionsToSD();
}

void DisplayAutoTuneValues() {

  Serial.println("=== Autotune Values ===");
  if (displayvalues) {
    for (int o = 0; o < (numOscillators + 1); o++) {
      Serial.print("Oscillator ");
      Serial.print(o);
      Serial.println(":");
      for (int i = 0; i < 128; i++) {
        Serial.print("  Note ");
        Serial.print(i);
        Serial.print(" ");
        Serial.println(autotune_value[i][o]);
      }
    }
  }
}

void allowOsc1Through() {

  if (osc1Through) {
    digitalWrite(MUX_S0, LOW);
    digitalWrite(MUX_S1, LOW);
    digitalWrite(MUX_S2, LOW);
    digitalWrite(MUX_S3, LOW);
    delayMicroseconds(100);
    digitalWrite(MUX_ENABLE, HIGH);
  } else {
    digitalWrite(MUX_S0, LOW);
    digitalWrite(MUX_S1, LOW);
    digitalWrite(MUX_S2, LOW);
    digitalWrite(MUX_S3, LOW);
    digitalWrite(MUX_ENABLE, LOW);
  }
}

void myPitchBend(byte channel, int bend) {
  if ((channel == masterChan) || (masterChan == 0)) {
    switch (BEND_WHEEL) {
      case 12:
        bend_data = map(bend, -8192, 8191, -3235, 3235);
        break;

      case 11:
        bend_data = map(bend, -8192, 8191, -2970, 2969);
        break;

      case 10:
        bend_data = map(bend, -8192, 8191, -2700, 2699);
        break;

      case 9:
        bend_data = map(bend, -8192, 8191, -2430, 2429);
        break;

      case 8:
        bend_data = map(bend, -8192, 8191, -2160, 2159);
        break;

      case 7:
        bend_data = map(bend, -8192, 8191, -1890, 1889);
        break;

      case 6:
        bend_data = map(bend, -8192, 8191, -1620, 1619);
        break;

      case 5:
        bend_data = map(bend, -8192, 8191, -1350, 1349);
        break;

      case 4:
        bend_data = map(bend, -8192, 8191, -1080, 1079);
        break;

      case 3:
        bend_data = map(bend, -8192, 8191, -810, 809);
        break;

      case 2:
        bend_data = map(bend, -8192, 8191, -540, 539);
        break;

      case 1:
        bend_data = map(bend, -8192, 8191, -270, 270);
        break;

      case 0:
        bend_data = 0;
        break;
    }
    // sample_data = (channel_a & 0xFFF0000F) | (((int(bend * 0.395) + 13180) & 0xFFFF) << 4);
    // outputDAC(DAC_NOTE5, sample_data);
  }
}

void adjustInterval() {
  if (INTERVAL_POT != INTERVAL) {
    Serial.print("Interval setting ");
    Serial.println(INTERVAL_POT);
  }
  switch (INTERVAL_POT) {
    case 12:
      INTERVAL = 12;
      break;

    case 11:
      INTERVAL = 11;
      break;

    case 10:
      INTERVAL = 10;
      break;

    case 9:
      INTERVAL = 9;
      break;

    case 8:
      INTERVAL = 8;
      break;

    case 7:
      INTERVAL = 7;
      break;

    case 6:
      INTERVAL = 6;
      break;

    case 5:
      INTERVAL = 5;
      break;

    case 4:
      INTERVAL = 4;
      break;

    case 3:
      INTERVAL = 3;
      break;

    case 2:
      INTERVAL = 2;
      break;

    case 1:
      INTERVAL = 1;
      break;

    case 0:
      INTERVAL = 0;
      break;
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

      case 14:
        INTERVAL_POT = map(value, 0, 127, 0, 12);
        break;

      case 15:
        DETUNE = value;
        break;

      case 16:
        BEND_WHEEL = map(value, 0, 127, 0, 12);
        break;

      case 17:
        FM_MOD_WHEEL = map(value, 0, 127, 0, 16.2);
        break;

      case 18:
        TM_MOD_WHEEL = map(value, 0, 127, 0, 16.2);
        break;

      case 19:
        FM_AT_WHEEL = map(value, 0, 127, 0, 16.2);
        break;

      case 20:
        TM_AT_WHEEL = map(value, 0, 127, 0, 16.2);
        break;

      case 21:
        value = map(value, 0, 127, 0, 2);
        switch (value) {
          case 0:
            OCTAVE_A = -12;
            break;
          case 1:
            OCTAVE_A = 0;
            break;
          case 2:
            OCTAVE_A = 12;
            break;
        }
        break;

      case 22:
        value = map(value, 0, 127, 0, 2);
        switch (value) {
          case 0:
            OCTAVE_B = -12;
            break;
          case 1:
            OCTAVE_B = 0;
            break;
          case 2:
            OCTAVE_B = 12;
            break;
        }
        break;

      // case 64:
      //   if (value > 63) {
      //     sustainOn = true;
      //     sustainNotes();
      //   } else {
      //     sustainOn = false;
      //     unsustainNotes();
      //   }
      //   break;

      case 121:
        if (value > 63) {
          startAutotune();
        }
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
      switch (getVoiceNoPoly2(-1)) {
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

    case 2:
    case 3:
    case 4:
      if (keyboardMode == 2) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 3) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 4) {
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

    case 5:
    case 6:
    case 7:
      if (keyboardMode == 5) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 6) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 7) {
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
      switch (getVoiceNoPoly2(note)) {
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

    case 2:
    case 3:
    case 4:
      if (keyboardMode == 2) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 3) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 4) {
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

    case 5:
    case 6:
    case 7:
      if (keyboardMode == 5) {
        S1 = 1;
        S2 = 1;
      }
      if (keyboardMode == 6) {
        S1 = 0;
        S2 = 1;
      }
      if (keyboardMode == 7) {
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

int getVoiceNoPoly2(int note) {
  voiceToReturn = -1;       // Initialize to 'null'
  earliestTime = millis();  // Initialize to now

  if (note == -1) {
    // NoteOn() - Get the oldest free voice (recent voices may still be on the release stage)
    if (voices[lastUsedVoice].note == -1) {
      return lastUsedVoice + 1;
    }

    // If the last used voice is not free or doesn't exist, check if the first voice is free
    if (voices[0].note == -1) {
      return 1;
    }

    // Find the lowest available voice for the new note
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == -1) {
        return i + 1;
      }
    }

    // If no voice is available, release the oldest note
    int oldestVoice = 0;
    for (int i = 1; i < NO_OF_VOICES; i++) {
      if (voices[i].timeOn < voices[oldestVoice].timeOn) {
        oldestVoice = i;
      }
    }
    return oldestVoice + 1;
  } else {
    // NoteOff() - Get the voice number from the note
    for (int i = 0; i < NO_OF_VOICES; i++) {
      if (voices[i].note == note) {
        return i + 1;
      }
    }
  }

  // Shouldn't get here, return voice 1
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
  mV = (unsigned int)(((float)(note1 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator1a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note1][0]));
  sample_data1 = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data1);
  oscbnote1 = note1 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote1 + transpose + realoctave) * NOTE_SF * oscillator1b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note1][1]));
  sample_data1 = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data1);
  mV = (unsigned int)(((float)(note1 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE));
  sample_data1 = (channel_a & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data1);
  velmV = ((unsigned int)((float)voices[0].velocity) * VEL_SF);
  vel_data1 = (channel_a & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data1);
}

void updateVoice2() {
  mV = (unsigned int)(((float)(note2 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator2a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note2][2]));
  sample_data2 = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data2);
  oscbnote2 = note2 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote2 + transpose + realoctave) * NOTE_SF * oscillator2b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note2][3]));
  sample_data2 = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data2);
  mV = (unsigned int)(((float)(note2 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data2 = (channel_b & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data2);
  velmV = ((unsigned int)((float)voices[1].velocity) * VEL_SF);
  vel_data2 = (channel_b & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data2);
}

void updateVoice3() {
  mV = (unsigned int)(((float)(note3 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator3a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note3][4]));
  sample_data3 = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data3);
  oscbnote3 = note3 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote3 + transpose + realoctave) * NOTE_SF * oscillator3b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note3][5]));
  sample_data3 = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data3);
  mV = (unsigned int)(((float)(note3 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data3 = (channel_c & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data3);
  velmV = ((unsigned int)((float)voices[2].velocity) * VEL_SF);
  vel_data3 = (channel_c & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data3);
}

void updateVoice4() {
  mV = (unsigned int)(((float)(note4 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator4a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note4][6]));
  sample_data4 = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data4);
  oscbnote4 = note4 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote4 + transpose + realoctave) * NOTE_SF * oscillator4b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note4][7]));
  sample_data4 = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data4);
  mV = (unsigned int)(((float)(note4 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data4 = (channel_d & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data4);
  velmV = ((unsigned int)((float)voices[3].velocity) * VEL_SF);
  vel_data4 = (channel_d & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data4);
}

void updateVoice5() {
  mV = (unsigned int)(((float)(note5 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator5a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note5][8]));
  sample_data5 = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data5);
  oscbnote5 = note5 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote5 + transpose + realoctave) * NOTE_SF * oscillator5b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note5][9]));
  sample_data5 = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data5);
  mV = (unsigned int)(((float)(note5 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data5 = (channel_e & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data5);
  velmV = ((unsigned int)((float)voices[4].velocity) * VEL_SF);
  vel_data5 = (channel_e & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data5);
}

void updateVoice6() {
  mV = (unsigned int)(((float)(note6 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator6a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note6][10]));
  sample_data6 = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data6);
  oscbnote6 = note6 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote6 + transpose + realoctave) * NOTE_SF * oscillator6b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note6][11]));
  sample_data6 = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data6);
  mV = (unsigned int)(((float)(note6 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data6 = (channel_f & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data6);
  velmV = ((unsigned int)((float)voices[5].velocity) * VEL_SF);
  vel_data6 = (channel_f & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data6);
}

void updateVoice7() {
  mV = (unsigned int)(((float)(note7 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator7a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note7][12]));
  sample_data7 = (channel_g & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data7);
  oscbnote7 = note7 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote7 + transpose + realoctave) * NOTE_SF * oscillator7b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note7][13]));
  sample_data7 = (channel_g & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data7);
  mV = (unsigned int)(((float)(note7 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data7 = (channel_g & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data7);
  velmV = ((unsigned int)((float)voices[6].velocity) * VEL_SF);
  vel_data7 = (channel_g & 0xFFF0000F) | (((int(velmV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE3, vel_data7);
}

void updateVoice8() {
  mV = (unsigned int)(((float)(note8 + transpose + realoctave + OCTAVE_A) * NOTE_SF * oscillator8a + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE) + (autotune_value[note8][14]));
  sample_data8 = (channel_h & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE1, sample_data8);
  oscbnote8 = note8 + INTERVAL + OCTAVE_B;
  mV = (unsigned int)(((float)(oscbnote8 + transpose + realoctave) * NOTE_SF * oscillator8b + 0.5) + (VOLTOFFSET + bend_data + FM_VALUE + FM_AT_VALUE + DETUNE) + (autotune_value[note8][15]));
  sample_data8 = (channel_h & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE2, sample_data8);
  mV = (unsigned int)(((float)(note8 + transpose + realoctave) * NOTE_SF * 1.00 + 0.5) + (TM_VALUE + TM_AT_VALUE));
  sample_data8 = (channel_h & 0xFFF0000F) | (((int(mV)) & 0xFFFF) << 4);
  outputDAC(DAC_NOTE4, sample_data8);
  velmV = ((unsigned int)((float)voices[7].velocity) * VEL_SF);
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
  sr.set(GATE_NOTE8, LOW);

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

    if (highlightEnabled) {  // Update encoder position
      encoderPosPrev = encoderPos;
      encoderPos--;
    } else {
      highlightEnabled = true;
      encoderPos = 0;  // Reset encoder position if highlight timed out
      encoderPosPrev = 0;
    }
    highlightTimer = millis();
    updateSelection();
  }

  if (btnIndex == ENC_B && btnType == ROX_PRESSED) {

    if (highlightEnabled) {  // Update encoder position
      encoderPosPrev = encoderPos;
      encoderPos++;
    } else {
      highlightEnabled = true;
      encoderPos = 0;  // Reset encoder position if highlight timed out
      encoderPosPrev = 0;
    }
    highlightTimer = millis();
    updateSelection();
  }

  if (btnIndex == ENC_BTN && btnType == ROX_PRESSED) {
    updateMenu();
  }

  if (btnIndex == AUTOTUNE_BTN && btnType == ROX_PRESSED) {
    startAutotune();
  }

  if (btnIndex == OFFSET_RESET && btnType == ROX_PRESSED) {
    reset = !reset;
    ResetAutoTuneValues();
  }

  if (btnIndex == OFFSET_DISPLAY && btnType == ROX_PRESSED) {
    displayvalues = true;
    DisplayAutoTuneValues();
  }

  if (btnIndex == OSC1_THROUGH && btnType == ROX_PRESSED) {
    osc1Through = !osc1Through;
    allowOsc1Through();
  }

  if (btnIndex == LOAD_SD && btnType == ROX_PRESSED) {
    loadSDCardNow();
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

void outputDAC(int CHIP_SELECT, uint32_t sample_data) {
  SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE1));
  digitalWrite(CHIP_SELECT, LOW);
  delayMicroseconds(1);
  SPI.transfer32(sample_data);
  delayMicroseconds(3);  // Settling time delay
  digitalWrite(CHIP_SELECT, HIGH);
  SPI.endTransaction();
}

int setCh;
char setMode[6];

void updateMenu() {  // Called whenever button is pushed

  if (highlightEnabled) {  // Highlight is active, choose selection
    switch (menu) {
      case SETTINGS:
        switch (mod(encoderPos, 5)) {
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
        setCh = mod(encoderPos, 7);
        menu = SETTINGS;
        EEPROM.write(ADDR_NOTE_NUMBER, polyphony);
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
      if (menu == KEYBOARD_MODE_SET_CH) keyboardMode = mod(encoderPos, 8);

    case MIDI_CHANNEL_SET_CH:
      if (menu == MIDI_CHANNEL_SET_CH) masterChan = mod(encoderPos, 17);

    case TRANSPOSE_SET_CH:
      if (menu == TRANSPOSE_SET_CH) masterTran = mod(encoderPos, 25);

    case OCTAVE_SET_CH:
      if (menu == OCTAVE_SET_CH) octave = mod(encoderPos, 4);

    case POLYPHONY_COUNT:
      if (menu == POLYPHONY_COUNT) polyphony = mod(encoderPos, 7);

    case SETTINGS:
      display.setCursor(0, 0);
      display.setTextColor(WHITE, BLACK);
      display.print(F("SETTINGS"));
      display.setCursor(0, 16);

      if (menu == SETTINGS) setHighlight(0, 6);
      display.print(F("Keyboard Mode "));
      if (menu == KEYBOARD_MODE_SET_CH) display.setTextColor(BLACK, WHITE);
      if (keyboardMode == 0) display.print("Poly 1");
      if (keyboardMode == 1) display.print("Poly 2");
      if (keyboardMode == 2) display.print("Uni T");
      if (keyboardMode == 3) display.print("Uni B");
      if (keyboardMode == 4) display.print("Uni L");
      if (keyboardMode == 5) display.print("Mono T ");
      if (keyboardMode == 6) display.print("Mono B ");
      if (keyboardMode == 7) display.print("Mono L ");
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

void selectMuxInput() {
  switch (oscillator) {

    // Board 1 A
    case 0:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 1 B
    case 1:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, HIGH);
      break;

    // Board 2 A
    case 2:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 2 B
    case 3:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, HIGH);
      break;

    // Board 3A
    case 4:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 3 B
    case 5:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, HIGH);
      break;

    // Board 4 A
    case 6:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 4 B
    case 7:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, LOW);
      digitalWrite(MUX_S3, HIGH);
      break;

    // Board 5 A
    case 8:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 5 B
    case 9:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, HIGH);
      break;

    // Board 6 A
    case 10:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 6 B
    case 11:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, LOW);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, HIGH);
      break;

    // Board 7 A
    case 12:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 7 B
    case 13:
      digitalWrite(MUX_S0, LOW);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, HIGH);
      break;

    // Board 8 A
    case 14:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, LOW);
      break;

    // Board 8 B
    case 15:
      digitalWrite(MUX_S0, HIGH);
      digitalWrite(MUX_S1, HIGH);
      digitalWrite(MUX_S2, HIGH);
      digitalWrite(MUX_S3, HIGH);
      break;
  }
}
