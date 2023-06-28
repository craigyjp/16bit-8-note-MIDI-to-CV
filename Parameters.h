//
// Autotune
//
float midi_to_freqs[128][2] = {
  { 0, 4.09 },
  { 1, 4.33 },
  { 2, 4.59 },
  { 3, 4.86 },
  { 4, 5.15 },
  { 5, 5.45 },
  { 6, 5.78 },
  { 7, 6.12 },
  { 8, 6.49 },
  { 9, 6.87 },
  { 10, 7.28 },
  { 11, 7.71 },
  { 12, 8.17 },
  { 13, 8.66 },
  { 14, 9.17 },
  { 15, 9.72 },
  { 16, 10.30 },
  { 17, 10.91 },
  { 18, 11.56 },
  { 19, 12.25 },
  { 20, 12.98 },
  { 21, 13.75 },
  { 22, 14.57 },
  { 23, 15.43 },
  { 24, 16.35 },
  { 25, 17.32 },
  { 26, 18.35 },
  { 27, 19.45 },
  { 28, 20.60 },
  { 29, 21.83 },
  { 30, 23.12 },
  { 31, 24.50 },
  { 32, 25.96 },
  { 33, 27.50 },
  { 34, 29.14 },
  { 35, 30.87 },
  { 36, 32.7 },
  { 37, 34.65 },
  { 38, 36.71 },
  { 39, 38.89 },
  { 40, 41.20 },
  { 41, 43.65 },
  { 42, 46.25 },
  { 43, 49.00 },
  { 44, 51.91 },
  { 45, 55.00 },
  { 46, 58.27 },
  { 47, 61.74 },
  { 48, 65.41 },
  { 49, 69.3 },
  { 50, 73.42 },
  { 51, 77.78 },
  { 52, 82.41 },
  { 53, 87.31 },
  { 54, 92.50 },
  { 55, 98.00 },
  { 56, 103.83 },
  { 57, 110.00 },
  { 58, 116.54 },
  { 59, 123.47 },
  { 60, 130.81 },
  { 61, 138.59 },
  { 62, 146.83 },
  { 63, 155.56 },
  { 64, 164.81 },
  { 65, 174.61 },
  { 66, 185.00 },
  { 67, 196.00 },
  { 68, 207.65 },
  { 69, 220.00 },
  { 70, 233.08 },
  { 71, 246.94 },
  { 72, 261.63 },
  { 73, 277.18 },
  { 74, 293.66 },
  { 75, 311.13 },
  { 76, 329.63 },
  { 77, 349.23 },
  { 78, 369.99 },
  { 79, 392.00 },
  { 80, 415.30 },
  { 81, 440.00 },
  { 82, 466.16 },
  { 83, 493.88 },
  { 84, 523.25 },
  { 85, 554.37 },
  { 86, 587.33 },
  { 87, 622.25 },
  { 88, 659.25 },
  { 89, 698.46 },
  { 90, 739.99 },
  { 91, 783.99 },
  { 92, 830.61 },
  { 93, 880.00 },
  { 94, 932.33 },
  { 95, 987.77 },
  { 96, 1046.5 },
  { 97, 1108.73 },
  { 98, 1174.66 },
  { 99, 1244.51 },
  { 100, 1318.51 },
  { 101, 1396.91 },
  { 102, 1479.98 },
  { 103, 1567.98 },
  { 104, 1661.22 },
  { 105, 1760.00 },
  { 106, 1864.66 },
  { 107, 1975.53 },
  { 108, 2093.00 },
  { 109, 2217.46 },
  { 110, 2349.32 },
  { 111, 2489.02 },
  { 112, 2637.02 },
  { 113, 2793.83 },
  { 114, 2959.96 },
  { 115, 3135.96 },
  { 116, 3322.44 },
  { 117, 3520.00 },
  { 118, 3729.31 },
  { 119, 3951.07 },
  { 120, 4186.01 },
  { 121, 4434.92 },
  { 122, 4698.63 },
  { 123, 4978.03 },
  { 124, 5274.04 },
  { 125, 5587.65 },
  { 126, 5919.91 },
  { 127, 6271.93 },
};

int8_t autotune_value[128][16];

boolean autotuneStart = false;
float sum1 = 0;
int count1 = 0;
elapsedMillis timeout;

const int numOscillators = 8;   // Number of oscillators
const int numNotes = 128;       // Number of MIDI notes

float targetFrequency = 0.00;
int tuneNote = 1;
int oscillator;
int8_t frequencyError;

float currentFrequency[128];
int EEPROM_OFFSET = 50;


//
// Modulation
//

float FM_VALUE = 0.0f;
float FM_AT_VALUE = 0.0f;
float TM_VALUE = 0.0f;
float TM_AT_VALUE = 0.0f;
float MOD_VALUE = 0.0f;

int FM_RANGE_UPPER = 0;
int FM_RANGE_LOWER = 0;
int FM_AT_RANGE_UPPER = 0;
int FM_AT_RANGE_LOWER = 0;

int TM_RANGE_UPPER = 0;
int TM_RANGE_LOWER = 0;
int TM_AT_RANGE_UPPER = 0;
int TM_AT_RANGE_LOWER = 0;

float FM_MOD_WHEEL = 0.00f;
float FM_AT_WHEEL = 0.00f;
float TM_AT_WHEEL = 0.00f;
float TM_MOD_WHEEL = 0.00f;

int BEND_WHEEL = 0;
int TM_RANGE = 0;
int DETUNE = 0;
int INTERVAL_POT = 0;
int INTERVAL = 0;

#define MUXCHANNELS 8
static byte muxInput = 0;
static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux1Read = 0;

int encoderPos, encoderPosPrev;

int polyphony;
//int polyphony;
int masterChan;
int masterTran;
int previousMode;
int transpose;
int8_t d2, i;
int noteMsg;
int keyboardMode;
int octave;
int realoctave;
int bend_data;
int note1, note2, note3, note4, note5, note6, note7, note8;
int oscbnote1, oscbnote2, oscbnote3, oscbnote4, oscbnote5, oscbnote6, oscbnote7, oscbnote8;
int numPlayingVoices = 0;
bool sustainOn = false;
bool reset = false;
byte sendnote, sendvelocity;

unsigned int velmV;
unsigned int mV;
int8_t retrievedNumber;

float noteTrig[8];
float monoTrig;
float unisonTrig;

uint32_t int_ref_on_flexible_mode = 0b00001001000010100000000000000000;  // { 0000 , 1001 , 0000 , 1010000000000000 , 0000 }

uint32_t sample_data = 0b00000000000000000000000000000000;

uint32_t sample_data1 = 0b00000000000000000000000000000000;
uint32_t sample_data2 = 0b00000000000000000000000000000000;
uint32_t sample_data3 = 0b00000000000000000000000000000000;
uint32_t sample_data4 = 0b00000000000000000000000000000000;
uint32_t sample_data5 = 0b00000000000000000000000000000000;
uint32_t sample_data6 = 0b00000000000000000000000000000000;
uint32_t sample_data7 = 0b00000000000000000000000000000000;
uint32_t sample_data8 = 0b00000000000000000000000000000000;

uint32_t vel_data1 = 0b00000000000000000000000000000000;
uint32_t vel_data2 = 0b00000000000000000000000000000000;
uint32_t vel_data3 = 0b00000000000000000000000000000000;
uint32_t vel_data4 = 0b00000000000000000000000000000000;
uint32_t vel_data5 = 0b00000000000000000000000000000000;
uint32_t vel_data6 = 0b00000000000000000000000000000000;
uint32_t vel_data7 = 0b00000000000000000000000000000000;
uint32_t vel_data8 = 0b00000000000000000000000000000000;

uint32_t channel_a = 0b00000010000000000000000000000000;
uint32_t channel_b = 0b00000010000100000000000000000000;
uint32_t channel_c = 0b00000010001000000000000000000000;
uint32_t channel_d = 0b00000010001100000000000000000000;
uint32_t channel_e = 0b00000010010000000000000000000000;
uint32_t channel_f = 0b00000010010100000000000000000000;
uint32_t channel_g = 0b00000010011000000000000000000000;
uint32_t channel_h = 0b00000010011100000000000000000000;

// EEPROM Addresses
#define ADDR_GATE_TRIG 0  // (0-7)
#define ADDR_PITCH_BEND 8
#define ADDR_CC 9

#define ADDR_MASTER_CHAN 18
#define ADDR_TRANSPOSE 19
#define ADDR_REAL_TRANSPOSE 20
#define ADDR_OCTAVE 21
#define ADDR_REALOCTAVE 22
#define ADDR_KEYBOARD_MODE 23
#define ADDR_NOTE_NUMBER 30

bool highlightEnabled = false;   // Flag indicating whether highighting should be enabled on menu
#define HIGHLIGHT_TIMEOUT 20000  // Highlight disappears after 20 seconds.  Timer resets whenever encoder turned or button pushed
unsigned long int highlightTimer = 0;
