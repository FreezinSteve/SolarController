/*
   Solar controller: MK-2

   Sketch to control pump and solenoids for solar heating panel

   Required Inputs:
   - Panel temperature
   - Hot inlet temperature
   - Cold outlet temperature

   Outputs:
   - Pump
   - Open drain
   - Bypass solenoid
   - Pump Supply (cold supply to pump)
   - Close drain
   - HWC Control
   -

   Operation:
   State machine
   STATE 1 = NORMAL, check temperatures
   STATE 2 = PUMP w/BYPASS
   STATE 3 = PUMP w/NO BYPASS
   STATE 4 = PUMPOFF, wait "n" cycles after pumping
   STATE 5 = UNUSED
   STATE 6 = UNUSED
   STATE 7 = UNUSED
   STATE 8 = UNUSED
   STATE 9 = UNUSED
   STATE 10 = FROST DRAIN
   STATE 11 = DEFROST WAIT
   STATE 12 = DEFROST REFILL
*/

#include <EEPROM.h>
#include <avr/wdt.h>

// Channel allocation
const int PANEL_CHAN = A0;
const int HOT_IN_CHAN = A1;
const int COLD_OUT_CHAN = A2;
const int WETBACK_CHAN = A3;
const int MAIN_CYL_CHAN = A4;
const int SOLAR_CYL_CHAN = A5;

// RX = 0
// TX = 1
const int BT_ENABLE = 2;
const int HSIO_SYNC = 3;
const int HSIO_CLCK = 4;
const int HSIO_DATA = 5;
const int SCAN_LED = 13;
const int DRAIN_SWITCH = 6;


// HSIO Relay module output BIT mapping
const unsigned int PUMP_ON = 32768;  // RELAY 1
<<<<<<< HEAD
const unsigned int DRAIN_OPEN_ON = 16384; // RELAY 2
=======
const unsigned int DRAIN_OPEN = 16384; // RELAY 2
>>>>>>> 09853d99304dc8c662e5ab8ac3431de3c8922653
const unsigned int BYPASS_ON = 8192; // RELAY 3
const unsigned int COLD_ON = 4096;   // RELAY 4
const unsigned int DRAIN_CLOSE_ON = 2048;   // RELAY 5
const unsigned int HWC_ON = 1024;    // RELAY 6
const unsigned int DRAIN_CLOSE = 512;  // RELAY 7

// HSIO timing
const int HSIO_SYNC_DELAY = 10;        // time (ms) from raising SYNC to setting DATA
const int HSIO_CLOCK_DELAY = 1;  // time (ms) for CLOCK to remain HIGH
const int HSIO_DATA_DELAY = 1;        // time (ms) from setting DATA to setting CLOCK

const int SCAN_RATE = 1;
const int EEPROM_ADDR = 0;

const float PANEL_DIFF = 5;            // If SOLAR_OUT + PANEL_DIFF > PANEL_TEMP then pump
const float PANEL_FROST = 5;
const float PANEL_DEFROST = 30;
const int DRAIN_TIMEOUT  = 180;        // Drain time in seconds
const int DRAIN_RETRY = 3600;         // Retry drain every hour
const int HOT_COLD_DIFF = 1;          // If HOT_IN < COLD_OUT + HOT_COLD_DIFF then stop pumping
const int MAX_PUMP_RUNTIME = 600;     // 600 cycles ~ 10 minutes
const int MAX_PUMP_WAITTIME = 60;     // 60 cycles ~ 1 minutes
const int MAX_BYPASS_WAITTIME = 60;
const int REFILL_DRAIN_CLOSE_TIME = 10;  // Time to close drain valve before opening COLDOUT
const int REFILL_WAITTIME = 60;
const int WETBACK_ON_DIFF = 10;       // Wetback ON if > main cylinder - 'x'
const int SOLAR_ON_DIFF = 5;          // Solar ON if > 'x' + solar cylinder
const int HWC_COLD = 40;              // Hot water cylinder completely cold
const int HWC_LOW = 50;               // Hot water cylinder low
const int HWC_BOOST_RESET = 60;       // Turn off HWC boost above this
const int HWC_LOWBOOST_RISE = 10;     // Auto boost, raise up by 'n' before clearing down boost
const int HWC_CIRC_START_DIFF = 10;   // Start HWC circulation pump if diff > 'x'
const int HWC_CIRC_STOP_DIFF = 5;     // Stop HWC circulation pump if diff < 'x'

const int SOLAR_ON_MAXTIMER = 3600;   // After "n" then assume that there's no solar available

// resistance at 25 degrees C
#define THERMISTORNOMINAL 10000
// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 25
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 5
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3950
// the value of the 'other' resistor
#define SERIESRESISTOR 10000

int state = 1;
int timer = 0;
unsigned int controlWord = 0;
int command = 0;
long nextScan = 0;
uint32_t heatAccumulator = 0;
float cycleHeatAccumulator = 0;
int boostState = 0;
unsigned long timeSinceLastPump = 0;
int solarOn = SOLAR_ON_MAXTIMER;
int wetbackOn = 0;
bool circulating = false;

float panel_temp = 0;
float hot_in_temp = 0;
float cold_out_temp = 0;
float wetback_temp = 0;
float main_cyl_temp = 0;
float solar_cyl_temp = 0;

float m[] {1, 1, 1, 1, 1, 1}; // Correction multiplier, A0, A1, A2, A3, A4, A5
float c[] {0, 0, 0, 0, 0, 0}; // Correction offset,     A0, A1, A2, A3, A4, A5


void setup() {
  Serial.begin(9600);
  //Serial.println("Initialisation...");

  // connect AREF to 3.3V and use that as VCC, less noisy!
  analogReference(EXTERNAL);

  pinMode(PANEL_CHAN, INPUT);
  pinMode(HOT_IN_CHAN, INPUT);
  pinMode(COLD_OUT_CHAN, INPUT);
  pinMode(WETBACK_CHAN, INPUT);
  pinMode(MAIN_CYL_CHAN, INPUT);
  pinMode(SOLAR_CYL_CHAN, INPUT);
  pinMode(DRAIN_SWITCH, INPUT_PULLUP);

  pinMode(BT_ENABLE, OUTPUT);
  pinMode(HSIO_SYNC, OUTPUT);
  pinMode(HSIO_CLCK, OUTPUT);
  pinMode(HSIO_DATA, OUTPUT);
  pinMode(SCAN_LED, OUTPUT);

  // Enable Bluetooth
  digitalWrite(BT_ENABLE, HIGH);
  state = 1;

  heatAccumulator = EEPROMReadlong(EEPROM_ADDR);

  wdt_enable(WDTO_8S);     // enable the watchdog for 8 seconds (max possible)
}

void loop() {

  wdt_reset();  // Kick the watchdog

  // Check for incoming commands
  if (Serial.available() > 0) {
    command = Serial.read();
    processCommand();
  }

  // Manual control switches can override behaviour
  checkDrainSwitch();

  long ms = millis();
  if (ms > nextScan)
  {
    digitalWrite(SCAN_LED, HIGH);
    scan();
    nextScan = ms + (SCAN_RATE * 1000);
    digitalWrite(SCAN_LED, LOW);
  }
}

//=======================================================================
// Run control program
//=======================================================================
void scan()
{
  readSensors();
  controlWord = 0;

  if (state > 0)    // If not disabled
  {
    if (checkSensors())
    {
      switch (state)
      {
        //==============================
        case 1:   // NORMAL
          state1();
          break;
        case 2:   // PUMP W/BYPASS
          state2();
          break;
        case 3:   // PUMP
          state3();
          break;
        case 4:   // POST PUMP WAIT
          state4();
          break;
        //==============================
        case 10:  // FROST
          state10();
          break;
        case 11:  // WAIT
          state11();
          break;
        case 12:  // REFILL
          state12();
          break;
        //==============================
        default:
          state = 1;
          break;
      }
    }
    else
    {
      sensorError();
    }
    timeSinceLastPump++;
    checkBoost();
  }
  setOutputs();
}

//=======================================================================
// Read and scale all of the sensors
//=======================================================================

void readSensors()
{
  panel_temp = readTemperature(PANEL_CHAN, 0);
  hot_in_temp = readTemperature(HOT_IN_CHAN, 1);
  cold_out_temp = readTemperature(COLD_OUT_CHAN, 2);
  wetback_temp = readTemperature(WETBACK_CHAN, 3);
  main_cyl_temp = readTemperature(MAIN_CYL_CHAN, 4);
  solar_cyl_temp = readTemperature(SOLAR_CYL_CHAN, 5);
}

//=======================================================================
// Check that all the solar monitoring sensors are working
//=======================================================================
bool checkSensors()
{
  bool ret = true;
  if (isnan(panel_temp) || panel_temp < -20 || panel_temp > 120)
  {
    ret = false;
  }
  if (isnan(hot_in_temp) || hot_in_temp < -20 || hot_in_temp > 120)
  {
    ret = false;
  }
  if (isnan(cold_out_temp) || cold_out_temp < -20 || cold_out_temp > 120)
  {
    ret = false;
  }
  return ret;
}

//=======================================================================
// Read the specified temperature, correct using the sensor index number
// for the mult/offset arrays
//=======================================================================
float readTemperature(int channel, int sensorNumber)
{
  int samples[NUMSAMPLES];

  uint8_t i;
  float average;

  // take N samples in a row, with a slight delay
  for (i = 0; i < NUMSAMPLES; i++) {
    samples[i] = analogRead(channel);
    delay(10);
  }

  // average all the samples out
  average = 0;
  for (i = 0; i < NUMSAMPLES; i++) {
    average += samples[i];
  }
  average /= NUMSAMPLES;

  // convert the value to resistance
  average = 1023 / average - 1;
  average = SERIESRESISTOR / average;


  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C

  // Apply correction
  steinhart = steinhart * m[sensorNumber];
  steinhart = steinhart + c[sensorNumber];

  return steinhart;
}

//=======================================================================
// Check temperatures
//=======================================================================
void state1()
{

  double maxTemp = cold_out_temp + PANEL_DIFF;
  if (maxTemp > 95)
  {
    maxTemp = 95;
  }
  if (panel_temp > maxTemp)
  {
    timer = 0;
    state = 2;
  }
  else if (panel_temp < PANEL_FROST)
  {
    timer = 0;
    state = 10;
  }
}

//=======================================================================
// Pumping w/bypass until warm water at cylinder
//=======================================================================
void state2()
{
  controlWord += PUMP_ON;
  controlWord += BYPASS_ON;
  controlWord += COLD_ON;   // Turn on cold to ensure that the pump stays primed if there's still air in the line
  if (timer < 10)
  {
    // Wait 10 seconds to equilibrate temps
    timer++;
  }
  else if (hot_in_temp > (cold_out_temp + HOT_COLD_DIFF))
  {
    state = 3;
    timer = 0;
  }
  else if (timer > MAX_BYPASS_WAITTIME)
  {
    state = 3;
    timer = 0;
  }
  else
  {
    timer++;
  }
}

//=======================================================================
// Warm water at cylinder, pump until water colder than cylinder cold
//=======================================================================
void state3()
{
  if (hot_in_temp < (cold_out_temp + HOT_COLD_DIFF))
  {
    state = 4;
    timer = 0;
    // Pump finished, save accumuation
    heatAccumulator += cycleHeatAccumulator;
    cycleHeatAccumulator = 0;
    EEPROMWritelong(EEPROM_ADDR, heatAccumulator);
  }
  else if (timer > MAX_PUMP_RUNTIME)
  {
    state = 4;
    timer = 0;
    // Pump finished, save accumuation
    heatAccumulator += cycleHeatAccumulator;
    cycleHeatAccumulator = 0;
    EEPROMWritelong(EEPROM_ADDR, heatAccumulator);
  }
  else
  {
    // Accumulate heat received (assume const. pump rate this is change in heat per second)
    // we'd need to know the pump rate to convert this to kWhr
    //heatAccumulator += (hot_in_temp - cold_out_temp);
    cycleHeatAccumulator += (hot_in_temp - cold_out_temp);
    controlWord += PUMP_ON;
    controlWord += COLD_ON;
    timer++;
  }
  timeSinceLastPump = 0;
}

//=======================================================================
// Post pump wait timer
//=======================================================================
void state4()
{
  timer++;
  if (timer > MAX_PUMP_WAITTIME)
  {
    timer = 0;
    state = 1;
  }
}

<<<<<<< HEAD
//=======================================================================
// Solar cylinder over temperature. Drain until below safe level
//=======================================================================
void state6()
{
  // Check for solar cylinder temperature dropped to safe level
  if (solar_cyl_temp < SOLAR_DUMP_STOP)
  {
    timer = 0;
    state = 1;
  }
  else
  {
    controlWord += DRAIN_OPEN_ON;
    controlWord += COLD_ON;
  }
}
=======
>>>>>>> 09853d99304dc8c662e5ab8ac3431de3c8922653

//=======================================================================
// Frost detected, drain panel
//=======================================================================
void state10()
{
  timer++;
<<<<<<< HEAD
  if (timer >= DRAIN_TIMEOUT)
=======
  if (timer < DRAIN_TIMEOUT)
  {
    // Drain solenoid is latching
    controlWord += DRAIN_OPEN;
    controlWord += VENT_ON;
  }
  else
>>>>>>> 09853d99304dc8c662e5ab8ac3431de3c8922653
  {
    state = 11;
    timer = 0;
  }
  controlWord += DRAIN_OPEN_ON;
}

//=======================================================================
// Panel drained, wait for warm temperatures
//=======================================================================
void state11()
{
  if (panel_temp > PANEL_DEFROST)
  {
    state = 12;
    timer = 0;
  }
  controlWord += DRAIN_OPEN_ON;
}

//=======================================================================
// Refill panel, open cold supply, close drain solenoid as it's latching
//=======================================================================
void state12()
{
  timer++;
  if (timer < REFILL_DRAIN_CLOSE_TIME)
  {
<<<<<<< HEAD
    // Drain for a few seconds with the cold solenoid open
    controlWord += DRAIN_OPEN_ON;
=======
    controlWord += DRAIN_CLOSE;
>>>>>>> 09853d99304dc8c662e5ab8ac3431de3c8922653
    controlWord += COLD_ON;
  }
  else if (timer < REFILL_WAITTIME)
  {
    controlWord += DRAIN_CLOSE;
    controlWord += COLD_ON;
    controlWord += PUMP_ON;
  }
  else
  {
    timer = 0;
    state = 1;
  }
}

//=======================================================================
// One or more vital sensors not working. Cycle the pump ON/OFF
// to stop panel getting too hot
//=======================================================================
void sensorError()
{
  timer++;
  if (timer < MAX_PUMP_RUNTIME)
  {
    controlWord += COLD_ON;
    controlWord += PUMP_ON;
  }
  else if (timer < (MAX_PUMP_RUNTIME + MAX_PUMP_WAITTIME))
  {
    // Do nothing, waiting
  }
  else
  {
    timer = 0;
  }
}

//=======================================================================
// Push the control word out via HSIO to control relays
//=======================================================================
void setOutputs()
{
  // DRAIN_CLOSE ON IS NEVER EXPLICITLY SET IN THE STATE MACHINE. WE JUST USE DRAIN_OPEN_ON AS THE FLAG FOR DRAIN OR NOT DRAIN
  if ((controlWord & DRAIN_OPEN_ON) == 0)
  {
    // DRAIN_OPEN_ON is not set, close the drain
    controlWord += DRAIN_CLOSE_ON;
  }

  // Bitbash controlWord out using HSIO
  // Raise SYNC

  digitalWrite(HSIO_SYNC, HIGH);
  delay(HSIO_SYNC_DELAY);

  for (int i = 0; i < 16; i++)
  {
    delay(HSIO_DATA_DELAY);
    if (controlWord & (1 << i))
    {
      // Current bit is set to 1
      digitalWrite(HSIO_DATA, HIGH);
    }
    delay(HSIO_DATA_DELAY);

    digitalWrite(HSIO_CLCK, HIGH);
    delay(HSIO_CLOCK_DELAY);
    digitalWrite(HSIO_CLCK, LOW);

    delay(HSIO_DATA_DELAY);
    digitalWrite(HSIO_DATA, LOW);
  }

  delay(HSIO_SYNC_DELAY);
  digitalWrite(HSIO_SYNC, LOW);
}

//=======================================================================
// Check wetback temperatures and cylinder temperatures to see if
// we should enable the main hot water cylinder element
//=======================================================================
void checkBoost()
{
  if (isnan(wetback_temp) || wetback_temp < -20 || wetback_temp > 120)
  {
    return;
  }
  if (isnan(main_cyl_temp) || main_cyl_temp < -20 || main_cyl_temp > 120)
  {
    return;
  }
  if (isnan(solar_cyl_temp) || solar_cyl_temp < -20 || solar_cyl_temp > 120)
  {
    return;
  }
  if (isnan(panel_temp) || panel_temp < -20 || panel_temp > 120)
  {
    return;
  }

  setWetbackOrSolarStates();

  if (main_cyl_temp > HWC_BOOST_RESET)
  {
    boostState = 0;    // Up to temperature, reset boost
  }
  else if (boostState <= 1)     // 0 == no boost, 1 == auto boost, 2 == user boost. User boost only resets when up to temperature
  {
    if (wetbackOn > 0 || solarOn > 0)
    {
      if (main_cyl_temp < HWC_COLD)
      {
        boostState = 1;    // Wetback or Solar on but cylinder really cold
      }
      else if (main_cyl_temp > HWC_COLD + HWC_LOWBOOST_RISE)
      {
        boostState = 0;
      }
    }
    else
    {
      // Wetback / solar not on
      if (main_cyl_temp < HWC_LOW)
      {
        boostState = 1;    // Wetback or solar off, and cylinder low
      }
      else if (main_cyl_temp > HWC_LOW + HWC_LOWBOOST_RISE)
      {
        boostState = 0;
      }
    }
  }

  // Note: HWC relay wired into N/C so that on downpowering controller, HWC is ON
  //if (boostState > 0)
  if (boostState == 0)
  {
    controlWord += HWC_ON;
  }

}

void setWetbackOrSolarStates()
{
  solarOn = 0;
  wetbackOn = 0;

  if (wetback_temp < HWC_COLD)
  {
    wetbackOn = 0;
  }
  else if (wetback_temp > main_cyl_temp)
  {
    wetbackOn = 1;
  }
  else if (wetback_temp > (main_cyl_temp - WETBACK_ON_DIFF))
  {
    wetbackOn = 1;
  }
  if (panel_temp > (solar_cyl_temp + SOLAR_ON_DIFF))
  {
    solarOn = 1;
  }
  else
  {
    // If the solar pump has pumped in the past "n" scans, then we
    // don't want the boost to start. This effectively disables boost
    // during the day if the sun is out
    if (timeSinceLastPump < SOLAR_ON_MAXTIMER)
    {
      solarOn = 1;
    }
  }
}


void outputReadingsJson()
{
  Serial.print("{\"PA\": \"");
  Serial.print(panel_temp);
  Serial.print("\", \"HI\": \"");
  Serial.print (hot_in_temp);
  Serial.print("\", \"CO\": \"");
  Serial.print (cold_out_temp);
  Serial.print("\", \"WB\": \"");
  Serial.print (wetback_temp);
  Serial.print("\", \"MC\": \"");
  Serial.print (main_cyl_temp);
  Serial.print("\", \"SC\": \"");
  Serial.print (solar_cyl_temp);
  Serial.print("\", \"ST\": \"");
  Serial.print (state);
  Serial.print("\", \"TM\": \"");
  Serial.print (timer);
  Serial.print("\", \"RL\": \"");
  // Output relay state
  for (int i = 0; i < 16; i++)
  {
    if (controlWord & (1 << i))
    {
      Serial.print("1");
    }
    else
    {
      Serial.print("0");
    }
  }
  Serial.print("\", \"DM\": \"");
  Serial.print("0");
  Serial.print("\", \"SM\": \"");
  Serial.print("0");
  Serial.print("\", \"HA\": \"");
  Serial.print(heatAccumulator);
  Serial.print("\", \"BS\": \"");
  Serial.print(boostState);
  Serial.print("\", \"SO\": \"");
  Serial.print(solarOn);
  Serial.print("\", \"WO\": \"");
  Serial.print(wetbackOn);
  Serial.print("\", \"LP\": \"");
  Serial.print(  timeSinceLastPump);
  Serial.println("\"}");
}

void processCommand()
{
  // Handle commands
  if (command == 'R' || command == 'r')       // 'R'ead
  {
    outputReadingsJson();
  }
  else if (command == '0')
  {
    state = 0;    // DISABLE
    Serial.println("#DISABLED");
  }
  else if (command == '1')
  {
    state = 1;    // RE-ENABLE
    Serial.println("#ENABLED");
  }
  else if (command == 'b' || command == 'B')
  {
    // Toggle HWC boost
    if (boostState > 0)
    {
      Serial.println("#BOOSTOFF");
      boostState = 0;
    }
    else
    {
      Serial.println("#BOOSTON");
      boostState = 2;
    }
  }
  else if (command == 'f' || command == 'F')
  {
    // Refill
    Serial.println("#FILL");
    state = 12;
    timer = 0;
  }
  else if (command == 'e' || command == 'E')
  {
    Serial.println("#EMPTY");
    state  = 10;
    timer = 0;

  }
}


void checkDrainSwitch()
{
  static bool lastStateDRAIN = false;
<<<<<<< HEAD

=======
  
>>>>>>> 09853d99304dc8c662e5ab8ac3431de3c8922653
  if (digitalRead(DRAIN_SWITCH) == LOW)
  {
    // Manual drain switch ON
    if (state != 10 && state != 11)
    {
      Serial.println("#DRAINON");
      Serial.println("#EMPTY");
      // force into DRAIN state.
      state = 10;
      timer = 0;
    }
    lastStateDRAIN = true;
  }
  else
  {
    if (lastStateDRAIN && state == 10)
    {
<<<<<<< HEAD
      // Switch has just been switched from drain, immediately try to
=======
      // Switch has just been switched from drain, immediately try to 
>>>>>>> 09853d99304dc8c662e5ab8ac3431de3c8922653
      // refill panel. This will happen if panel temp is high enough
      state = 11;
      timer = 0;
    }
    lastStateDRAIN = false;
  }
}

//===========================================================================
//This function will write a 4 byte (32bit) long to the eeprom at
//the specified address to address + 3.
void EEPROMWritelong(int address, uint32_t value)
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

uint32_t EEPROMReadlong(int address)
{
  //Read the 4 bytes from the eeprom memory.
  uint32_t four = EEPROM.read(address);
  uint32_t three = EEPROM.read(address + 1);
  uint32_t two = EEPROM.read(address + 2);
  uint32_t one = EEPROM.read(address + 3);

  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}
