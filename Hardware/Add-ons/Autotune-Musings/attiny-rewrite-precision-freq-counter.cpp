#include <Arduino.h>

volatile uint32_t input_counter_high;
volatile uint32_t val_input_counter_high;
volatile uint32_t val_input_counter_low;

volatile uint32_t reference_counter_high;
volatile uint32_t val_reference_counter_high;
volatile uint32_t val_reference_counter_low;

volatile uint8_t wait_a_second;
volatile uint16_t timeout_counter;
volatile uint8_t message;

IntervalTimer timer0;
IntervalTimer timer1;

void timer0Overflow();
void timer1Overflow();
void timer1Capture();

void setup()
{
  delay(500);  // Give some time for initialization
  
  pinMode(3, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(4, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);

  // Configure Timer0 as an interval timer with overflow interrupt
  timer0.begin(timer0Overflow, 1000000);  // 1 MHz timer frequency
  
  // Configure Timer1 as an interval timer with overflow interrupt
  timer1.begin(timer1Overflow, 10000);  // 10 kHz timer frequency
  
  attachInterrupt(digitalPinToInterrupt(6), timer1Capture, RISING);
  
  // Initialize variables
  input_counter_high = 0;
  val_input_counter_high = 0;
  val_input_counter_low = 0;
  reference_counter_high = 0;
  val_reference_counter_high = 0;
  val_reference_counter_low = 0;
  wait_a_second = 0;
  timeout_counter = 765;
  message = 0;

  // Enable interrupts
  interrupts();
}

void loop()
{
  if (timeout_counter == 0)  // No input signal
  {
    // Do something when there's no input signal
  }

  if (message == 1)
  {
    message = 0;
    
    timeout_counter = 765;  // Input signal detected, reset timeout_counter (5 second wait)

    wait_a_second = 152;  // Start countdown to 1 second
    
    // Perform calculations using the frequency values
    uint32_t input_freq = val_input_counter_low;
    uint32_t ref_freq = val_reference_counter_low;

    // Calculate the resulting frequency
    uint32_t frequency = (input_freq * 10000000) / ref_freq;

    // Do something with the resulting frequency

    val_input_counter_low = 0;
    val_reference_counter_low = 0;
  }

  // TODO: Add code here to use the calculated values

  delay(100);  // Temporary delay for demonstration purposes
}

void timer0Overflow()
{
  input_counter_high++;
}

void timer1Overflow()
{
  reference_counter_high++;

  if (wait_a_second > 0)
  {
    wait_a_second--;
  }
  else
  {
    digitalWrite(5, HIGH);  // Make D_FF high
  }

  if (timeout_counter > 0)
  {
    timeout_counter--;
  }
}

void timer1Capture()
{
  val_input_counter_high = input_counter_high;
  val_reference_counter_high = reference_counter_high;

  message = 1;  // Signal to main program
}
