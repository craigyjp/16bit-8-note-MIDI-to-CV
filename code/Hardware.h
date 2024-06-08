#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();

// Scale Factor will generate 0.25v/octave
// 10 octave keyboard on a 3.3v powered DAC
#define NOTE_SF 271.5
#define VEL_SF 256.0
#define ARRAY_SIZE 128
#define N_LAST_POINTS 10 

// Voices available
#define NO_OF_VOICES 8
#define trigTimeout 20

//Note DACS
#define DAC_NOTE1 7
#define DAC_NOTE2 8
#define DAC_NOTE3 5
#define DAC_NOTE4 6
#define DAC_NOTE5 10


//#define TUNE_INPUT 9


#define FM_INPUT A10

//Autotune MUX

#define MUX_S0 33
#define MUX_S1 34
#define MUX_S2 35
#define MUX_S3 36

#define ANA_MUX_S0 27
#define ANA_MUX_S1 28
#define ANA_MUX_S2 29

#define MUX_ENABLE 37

//Gate outputs
#define GATE_NOTE1 0
#define GATE_NOTE2 1
#define GATE_NOTE3 2
#define GATE_NOTE4 3
#define GATE_NOTE5 4
#define GATE_NOTE6 5
#define GATE_NOTE7 6
#define GATE_NOTE8 7

#define AUTOTUNE_LED 8

//Trig outputs
#define TRIG_NOTE1 16
#define TRIG_NOTE2 17
#define TRIG_NOTE3 18
#define TRIG_NOTE4 19
#define TRIG_NOTE5 20
#define TRIG_NOTE6 21
#define TRIG_NOTE7 22
#define TRIG_NOTE8 23

//Encoder or buttons
#define ENC_A 0
#define ENC_B 1
#define ENC_BTN 2
#define AUTOTUNE_BTN 3
#define OFFSET_RESET 4
#define OFFSET_DISPLAY 5
#define OSC1_THROUGH 6

#define MUX_IN A14

#define MUX1_FM_AT_DEPTH 0
#define MUX1_TM_MOD_DEPTH 1
#define MUX1_TM_AT_DEPTH 2
#define MUX1_FM_MOD_DEPTH 3
#define MUX1_spare4 4
#define MUX1_spare5 5
#define MUX1_spare6 6
#define MUX1_PB_DEPTH 7

#define QUANTISE_FACTOR 10


void setupHardware() {

    //Volume Pot is on ADC0
  adc->adc0->setAveraging(16);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(12);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  //MUXs on ADC1
  adc->adc1->setAveraging(16);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc1->setResolution(12);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  analogReadResolution(12);

  pinMode(DAC_NOTE1, OUTPUT);
  pinMode(DAC_NOTE2, OUTPUT);
  pinMode(DAC_NOTE3, OUTPUT);
  pinMode(DAC_NOTE4, OUTPUT);
  pinMode(DAC_NOTE5, OUTPUT);

  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);

  pinMode(ANA_MUX_S0, OUTPUT);
  pinMode(ANA_MUX_S1, OUTPUT);
  pinMode(ANA_MUX_S2, OUTPUT);

  pinMode(MUX_ENABLE, OUTPUT);
  //pinMode(TUNE_INPUT, INPUT);

  digitalWrite(DAC_NOTE1, HIGH);
  digitalWrite(DAC_NOTE2, HIGH);
  digitalWrite(DAC_NOTE3, HIGH);
  digitalWrite(DAC_NOTE4, HIGH);
  digitalWrite(DAC_NOTE5, HIGH);

  digitalWrite(MUX_S0, LOW);
  digitalWrite(MUX_S1, LOW);
  digitalWrite(MUX_S2, LOW);
  digitalWrite(MUX_S3, LOW);

  digitalWrite(ANA_MUX_S0, LOW);
  digitalWrite(ANA_MUX_S1, LOW);
  digitalWrite(ANA_MUX_S2, LOW);

  digitalWrite(MUX_ENABLE, LOW);

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE1, LOW);
  SPI.transfer32(int_ref_on_flexible_mode);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE1, HIGH);
  SPI.endTransaction();

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE2, LOW);
  SPI.transfer32(int_ref_on_flexible_mode);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE2, HIGH);
  SPI.endTransaction();

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE3, LOW);
  SPI.transfer32(int_ref_on_flexible_mode);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE3, HIGH);
  SPI.endTransaction();

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE4, LOW);
  SPI.transfer32(int_ref_on_flexible_mode);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE4, HIGH);
  SPI.endTransaction();

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE5, LOW);
  SPI.transfer32(int_ref_on_flexible_mode);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE5, HIGH);
  SPI.endTransaction();
  
  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE5, LOW);
  sample_data = ((channel_a & 0xFFF0000F) | (13180 & 0xFFFF) << 4);
  SPI.transfer32(sample_data);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE5, HIGH);
  SPI.endTransaction();

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE5, LOW);
  sample_data = ((channel_b & 0xFFF0000F) | (0 & 0xFFFF) << 4);
  SPI.transfer32(sample_data);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE5, HIGH);
  SPI.endTransaction();

  SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE1));
  digitalWrite(DAC_NOTE5, LOW);
  sample_data = ((channel_c & 0xFFF0000F) | (17018 & 0xFFFF) << 4);
  SPI.transfer32(sample_data);
  delayMicroseconds(1);
  digitalWrite(DAC_NOTE5, HIGH);
  SPI.endTransaction();

}