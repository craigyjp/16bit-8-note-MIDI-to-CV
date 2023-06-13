//
// Modulation
//
float FM_VALUE = 0.0f;
float TM_VALUE = 0.0f;
float AT_VALUE = 0.0f;
float MOD_VALUE = 0.0f;
int FM_RANGE_UPPER = 0;
int FM_RANGE_LOWER = 0;
int AT_RANGE_UPPER = 0;
int AT_RANGE_LOWER = 0;
float MOD_WHEEL = 0.00f;
float AT_WHEEL = 0.00f;
int BEND_WHEEL = 0;
int TM_RANGE = 0;

#define MUXCHANNELS 8
static byte muxInput = 0;
static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux1Read = 0;

float sfAdj[8];

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
int numPlayingVoices = 0;
bool sustainOn = false;
byte sendnote, sendvelocity;

unsigned int velmV;

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
#define ADDR_SF_ADJUST 10  // (10-17)
#define ADDR_MASTER_CHAN 18
#define ADDR_TRANSPOSE 19
#define ADDR_REAL_TRANSPOSE 20
#define ADDR_OCTAVE 21
#define ADDR_REALOCTAVE 22
#define ADDR_KEYBOARD_MODE 23

bool highlightEnabled = false;   // Flag indicating whether highighting should be enabled on menu
#define HIGHLIGHT_TIMEOUT 20000  // Highlight disappears after 20 seconds.  Timer resets whenever encoder turned or button pushed
unsigned long int highlightTimer = 0;

