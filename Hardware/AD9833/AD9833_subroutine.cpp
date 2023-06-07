// ----------------------------- DCO vars
const int FSYNC_PINS[8] = {2, 3, 4, 5, 6, 7, 8, 9};
#define SPI_CLOCK_SPEED 25000000                     // 7.5 MHz SPI clock - this works ALMOST without clock ticks
unsigned long MCLK = 25000000;

void AD9833Reset(int AD_board) {
  SPI.beginTransaction(SPISettings(SPI_CLOCK_SPEED, MSBFIRST, SPI_MODE2));
  int FSYNC_RESET_PIN = FSYNC_PINS[AD_board];
  digitalWrite(FSYNC_RESET_PIN, LOW);
  SPI.transfer16(0x2100);
  digitalWrite(FSYNC_RESET_PIN, HIGH);
  delayMicroseconds(10); // Wait for 10 us after reset
}

void updateAD9833() {
  for (int i = 0; i < POLYPHONY; i++) {
    if (voices[i].noteOn == true) {
      long FreqReg0 = (voices[i].dcoFreq * pow(2, 28)) / MCLK;   // Data sheet Freq Calc formula
      int MSB0 = (int)((FreqReg0 & 0xFFFC000) >> 14);     // only lower 14 bits are used for data
      int LSB0 = (int)(FreqReg0 & 0x3FFF);
      int FSYNC_SET_PIN = FSYNC_PINS[i];
      SPI.beginTransaction(SPISettings(SPI_CLOCK_SPEED, MSBFIRST, SPI_MODE2));
      digitalWrite(FSYNC_SET_PIN, LOW);  // set FSYNC low before writing to AD9833 registers
      LSB0 |= 0x4000; // DB 15=0, DB14=1
      MSB0 |= 0x4000; // DB 15=0, DB14=1
      delayMicroseconds(1); // Wait for 1 us after FSYNC falling edge
      SPI.transfer16(LSB0); // write lower 16 bits to AD9833 registers
      delayMicroseconds(50); // Wait for 50 us between writes                             
      SPI.transfer16(MSB0);  // write upper 16 bits to AD9833 registers
      delayMicroseconds(50); // Wait for 50 us between writes                            
      SPI.transfer16(0xC000); // write phase register
      delayMicroseconds(50); // Wait for 50 us between writes                          
      SPI.transfer16(0x2002);  // take AD9833 out of reset and output triangle wave (DB8=0)
      delayMicroseconds(5); // Wait for 50 us between writes         
      digitalWrite(FSYNC_SET_PIN, HIGH);  // write done, set FSYNC high
      SPI.endTransaction();
    }
  }
}
void setup() {
  SPI.begin();
  for (int i = 0; i < 8; i++) {
    int FSYNC_PIN_INIT = FSYNC_PINS[i];
    pinMode(FSYNC_PIN_INIT, OUTPUT);                           
    digitalWrite(FSYNC_PIN_INIT, HIGH); 
    AD9833Reset(i);
  }
}
