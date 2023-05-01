/*
 The AD8400 is SPI-compatible. To command it you send two bytes. 
 The first byte is the channel number starting at zero: (0)
 The second byte is the resistance value for the channel: (0 - 255).
  * CS - to digital pin 10  (SS pin)
  * SDI - to digital pin 11 (MOSI pin)
  * CLK - to digital pin 13 (SCK pin)
*/

#include <SPI.h>                     // set pin 10 as the slave select for the digital pot:
const int SLAVE_SELECT_PIN = 10;
const int channel = 0;

// sleep definition
#include <util/atomic.h>
#define CriticalSection ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
extern unsigned long timer0_millis;


#define REL_IN 2                     // Input relay   2, 18
#define REL_OUT 3                    // Output relay  3, 19
#define MEAS_ON 4                    // Measure on    4, 20
#define BAT_U_PIN A6                 // Input voltage measurement battery A6
#define PV_U_PIN A7                  // Input voltage measurement PV A7 

#define PV_U_START_CHARGING 28300    // Voltage to start charinging battery
#define BAT_U_MAX 30800              // Voltage charging battery full
#define BAT_U_SOFT_CHARGE 30600      // Voltage to start soft charge near battery full
#define BAT_U_SOFT_CHARGE_H 30450    // Voltage to stop soft charge
#define START_SOFT_CHARGING_CURRENT 3000 // When Soft Charing Voltage and Soft Charging current acceed soft charging starts 
#define BAT_U_MAX_H 30200            // Voltage to restart charging battery (Hysteresis)
#define BAT_U_MIN 28300              // Voltage to stop discharging battery empty
#define BAT_U_MIN_H 29000            // Voltage to restart discharing (Hysteresis)
#define BAT_LIN 29700                // Battery linear voltage to power 28V until 29,7V
#define INVERTER_START_DIODE_U 210   // Voltage charging diode discharging starts
#define INVERTER_START_DIODE_U_H 250 // Voltage charging diode discharging stops (Hysteresis)
#define BATTERY_CAPACITY 45000       // 48.000mAH Battery Capacity
#define INVERTER_START_TIME 15000    // Inverter start producing power to grid after 4 min = 15000 * 16 millis /clock frequence 1 Mhz instead of 16 MHz
#define PEAK_TIME_PERC 0.45          // Percentage of Peak time, each night firt x% are peak time, last 100%-x& are base time  
#define MIN_DISCHARGE_CURRENT 900    // Minimum discharge current
#define MAX_DISCHARGE_CURRENT 7000   // Maximum discharge current
#define BASE_MAX_DISCHARGE_CURRENT 2500 // Maximum Power needed at base time in mA


volatile unsigned long night_start = 0;       // night_start stores time stamp in millis/16 from last day to night dedection (divisor 16 because of 1MHz clock frequency)
volatile unsigned long night_length = 36000000; // night_length calculated after fist day to nigth und nigth to day dedection in true millis, default 36.000.000 millis = 10h
volatile unsigned long time_power_check = 0;  // time stamp from last battery power incremental and decremental calculation
volatile word bat_u, pv_u;                    // measured voltage at battery and PV
volatile word battery_power;                  // calculated actual power in battery
volatile word power_peak = 3300, power_base = 1800; // base (off peak) and peak power from battery to inverter
volatile word milliamps_pv_in = 0;            // current in mA, milliamps PV in
volatile int discharge_current = 0, milliamps = 0;    // current in mA, milliamps to or from battery
volatile byte ser_mon_line_counter = 0;       // Serial monitor line counter
volatile byte deepsleep_counter;              // Counter for multiple deep sleeps
volatile boolean relay_on[] = {false, false, false, false}; // {NULL, NULL,REL_IN, REL_OUT}
char outstr[17]="";                           // convert binary variables to string for output
long outlong;                                  // long tpo use sprintf with long variable instead of word

void setup(){
  clockspeed();                      // reduce clockspeed to 1MHz
  SMCR |= (1 << 2);                  // power down mode
  SMCR |= 1;                         // enable sleep

  Serial.begin(9600);                // open the serial port at 9600 bps: Attention clock rate 1MHz lead to Baudrate at the Computer 600 Baud
  Serial.println("####### PROGRAM NEW START ########");
  pinmodes();
  initialize_SPI();
  delay(500);                        // delay 8 sec. before starting measuring voltage
  start_measure_voltage();
  bat_u = measure_voltage(BAT_U_PIN);
  pv_u = measure_voltage(PV_U_PIN);
  stop_measure_voltage();

  estimate_bat_power();              // estimates Power in battery when adruino starts according to measured voltage from battery

  if (pv_u > PV_U_START_CHARGING) {  // if Volatge at PV is higher than usual voltage to start charging then
    if (bat_u < BAT_U_MAX) {         // iF battery voltage is lower than maximum battery voltage then start charging
      relay_on[REL_IN] = true;       
      do_relay_on(REL_IN,true);
      Serial.print("Septup: Turn Relay In to on, start battery charging. PV Voltage: ");
      Serial.println(pv_u);
    }        
  } else if (bat_u > BAT_U_MIN_H){   // if Volatge at PV is not higher than usual voltage to start charging then do discharging at base_power
    discharge_current = power_base;
    Serial.print("Septup: Turn Relay Out to on, start battery discharging. Battery Voltage: ");
    Serial.println(bat_u);
    start_discharging(discharge_current);
  }  
}


// ******* Chapter SUBROUTINES SETUP
void clockspeed() {
  //clock to 1MHz
  CLKPR=0x80,
  CLKPR=0x04;
}

void pinmodes() {
  pinMode(REL_IN, OUTPUT);
  pinMode(REL_OUT, OUTPUT);
  pinMode(MEAS_ON, OUTPUT);
  pinMode(BAT_U_PIN, INPUT);
  pinMode(PV_U_PIN, INPUT);
}

void initialize_SPI(){
  // set the slaveSelectPin as an output
  pinMode (SLAVE_SELECT_PIN, OUTPUT);
  // initialize SPI
  SPI.begin();
  //Set all pots to zero as a starting point
  discharge_current = 0;
  set_power_of_discharge(discharge_current);
}

void estimate_bat_power(){
  if (bat_u < BAT_LIN) {
    battery_power = max(0, map(bat_u, BAT_U_MIN, BAT_LIN, 0, 25000)); 
  } else {
    battery_power = 30000;        
  }
}

void loop(){

  outstr[17]="";
  start_measure_voltage();
  bat_u = measure_voltage(BAT_U_PIN);
  pv_u = measure_voltage(PV_U_PIN);
  stop_measure_voltage();

  battery_power = calculate_power_in_battery(battery_power);

  //                                       start battery charging relay when PV has enough power
  if (!relay_on[REL_IN] && bat_u < BAT_U_MAX_H) {   
    if ((!relay_on[REL_OUT] && pv_u > PV_U_START_CHARGING) || (relay_on[REL_OUT] &&  pv_u > bat_u)) {
      relay_on[REL_IN] = true;
      do_relay_on(REL_IN, true);
      Serial.print("Loop: Turn Relay In to on, start battery charging. PV Voltage: ");
      Serial.println(pv_u);
      do_night_end();
    }
  } else {
//                                         stop battery charging relay when no power from pv or battery voltage greater than maximum
    if (relay_on[REL_IN] && (pv_u < bat_u || bat_u > BAT_U_MAX)) {
      relay_on[REL_IN] = false;
      do_relay_on(REL_IN, false);
      Serial.print("Loop: Turn Relay In to off, stop battery charging. PV Voltage: ");
      Serial.print(pv_u);
      Serial.print(" Battery Voltage: ");
      Serial.println(bat_u);
      if (bat_u > BAT_U_MAX) {
        battery_power = BATTERY_CAPACITY;
        relay_on[REL_OUT] = false;        // stop discharging for soft charging
        do_relay_on(REL_OUT, false);
        Serial.println("Loop: Turn Relay Out to off, stop soft charging.");
      }
    }
  }
  delay(500);
  start_measure_voltage();
  bat_u = measure_voltage(BAT_U_PIN);
  pv_u = measure_voltage(PV_U_PIN);
  stop_measure_voltage();
//                                         start discharging relay when pv Voltage under start charging voltage or do soft charing?
  if ( !relay_on[REL_OUT] && bat_u > BAT_U_MIN_H) {
    if ((relay_on[REL_IN] && ((pv_u - bat_u) < INVERTER_START_DIODE_U || (bat_u > BAT_U_SOFT_CHARGE && milliamps > START_SOFT_CHARGING_CURRENT))) || (!relay_on[REL_IN] &&  pv_u <=  PV_U_START_CHARGING)) {
      if ((pv_u - bat_u) < INVERTER_START_DIODE_U || pv_u <=  PV_U_START_CHARGING) {
        power_base_peak_calculation();
        Serial.print("Loop: Turn Relay Out to on, start battery discharging. Battery Voltage: ");
        do_night_start();
      } else {
        Serial.print("Loop: Turn Relay Out to on, start battery soft charging. Battery Voltage: ");
      }
      discharge_current = power_base;
      Serial.println(bat_u);
      start_discharging(discharge_current);
    }
  } else {
//                                         stop discharging relay when battery voltage less then minimum or when charging and Diode Voltage greater than fixed value so that normal pv is producing
    if (relay_on[REL_OUT]) { 
      if (bat_u < BAT_U_MIN || (relay_on[REL_IN] && (pv_u - bat_u) > INVERTER_START_DIODE_U_H && bat_u < BAT_U_SOFT_CHARGE_H)) {
        relay_on[REL_OUT] = false;
        do_relay_on(REL_OUT, false);
        discharge_current = 0;
        Serial.print("Loop: Turn Relay Out to off, stop battery discharging. PV Voltage: ");
        Serial.print(pv_u);
        Serial.print(" Battery Voltage: ");
        Serial.println(bat_u);
        if (bat_u < BAT_U_MIN) {
          battery_power = 0;
        }
      }
    }
  }
  peak_base_time_soft_charge_power_control();
  Parameterausgabe_Serieller_Monitor();
  for (deepsleep_counter = 0; deepsleep_counter < 30; deepsleep_counter++) {
    watchdog_deepsleep(33);         // deep_sleep 33 = 8 sec
  } 
}

// ******* Chapter SUBROUTINES Battery and PV voltage measurement
void start_measure_voltage(){
  digitalWrite(MEAS_ON,1);
}

word measure_voltage(byte analog_pin){
  delay(30);
  return map_word(analogRead(analog_pin),0,1023,23650, 38500); 
}

void stop_measure_voltage(){
  digitalWrite(MEAS_ON,0);
}

long map_word(long value, word from_low, word from_high, word to_low, word to_high){
  return ((value - from_low) * (to_high - to_low) / (from_high - from_low) + to_low);
}


// ******* Chapter SUBROUTINE Battery Power Calculation
long calculate_power_in_battery (long power_mah){
  word diode_u = 0;
  int divisor = 0;
  int change = 0;
  long time_delay;
  milliamps_pv_in = 0;
  if (pv_u > bat_u) {
    diode_u = pv_u - bat_u;
    if (relay_on[REL_IN]){  
      switch (diode_u) {
        case 0 ... 200: 
          milliamps_pv_in = map (diode_u, 0, 200, 10, 150); 
          break;
        case 201 ... 250:     
          milliamps_pv_in = map (diode_u, 201, 250, 152, 1000); 
          break;
        case 251 ... 270:           
          milliamps_pv_in = map (diode_u, 251, 270, 1000, 2000); 
          break;
        case 271 ... 340:           
          milliamps_pv_in = map (diode_u, 271, 340, 2000, 8000); 
          break;
        case 341 ... 360:           
          milliamps_pv_in = map (diode_u, 341, 360, 8000, 9000); 
          break;
        default:           
          milliamps_pv_in = map (diode_u, 361, 360, 9000, 11000); 
          break;
      }
    }
  }
  if (relay_on[REL_OUT]) {
    milliamps = milliamps_pv_in - discharge_current;
  }
  if (abs(milliamps) > 15) { 
    time_delay = (millis() - time_power_check);
    change =  time_delay / (225000 / milliamps);
    if(change < 0 && abs(change) > power_mah){
      power_mah = 0;      
    } else {
      power_mah = min(power_mah + change, BATTERY_CAPACITY);
    }
  }
  delay(50);
  Serial.print("Calculate power in battery: Diode_u: ");
  Serial.print(diode_u);
  Serial.print(" Milliamps PV in: ");
  Serial.print(milliamps_pv_in);
  Serial.print(" Discharge current: ");
  Serial.print(discharge_current);
  Serial.print(" Milliamps: ");
  Serial.print(milliamps);
  Serial.print(" Battery Power ");
  Serial.print(power_mah);
  Serial.print(" Pv U: ");
  Serial.print(pv_u);
  Serial.print(" Bat U: ");
  Serial.print(bat_u);
  Serial.print(" Divisor: ");
  Serial.print(divisor);
  Serial.print(" Timedelay: ");
  Serial.println(millis()-time_power_check);
  time_power_check = millis();
  return power_mah;
}


// ******* Chapter SUBROUTINES IO Management and SPI Management DigitalResistance
void do_relay_on(byte pin, boolean doit){
 if (doit) {
    digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, LOW);
  }     
}

void start_discharging(word current) { //Battery discharge power control function
  set_power_of_discharge(1500);
  relay_on[REL_OUT]=true;
  do_relay_on(REL_OUT,true);
  delay(15000);
  set_power_of_discharge(current);
}

void set_power_of_discharge(int value) { //milliampere
  Serial.print(" Discharge Current: ");
  Serial.println(value);
  value = map(value,0,MAX_DISCHARGE_CURRENT,4,48);
  digitalPotWrite(0, value);
}
 
void digitalPotWrite(int address, int value) {
  // take the SS pin low to select the chip:
  digitalWrite(SLAVE_SELECT_PIN, LOW);
  delay(50);
  //  send in the address and value via SPI:
  SPI.transfer(address);
  SPI.transfer(value);
  // take the SS pin high to de-select the chip:
  delay(50);
  digitalWrite(SLAVE_SELECT_PIN, HIGH); 
}


// ******* Chapter SUBROUTINES Night Calulations
void do_night_start(){
  night_start = millis();
  Serial.print("Loop: Night Start at (true min): ");
  Serial.println((night_start * 16 / 60000));
}
void do_night_end(){
  if((millis() - night_start) > 675000) {
    night_length = min( 64800000, 16 * (millis() - night_start));    
    Serial.print("Loop: Night End, night length (true min): ");
    Serial.println((night_length / 60000));
  }
}


// ******* Chapter SUBROUTINES Night Power Management
void power_base_peak_calculation () {
  if (night_start != 0) {
    power_base = max(MIN_DISCHARGE_CURRENT, (battery_power / (night_length / 3600000 + 1) * 0,6));
    power_peak = max(MIN_DISCHARGE_CURRENT, (battery_power / (night_length / 3600000 + 1) * 1,1));
    if (power_base > BASE_MAX_DISCHARGE_CURRENT) {
      power_peak = min(MAX_DISCHARGE_CURRENT, (power_peak + (power_base - BASE_MAX_DISCHARGE_CURRENT)));
      power_base = BASE_MAX_DISCHARGE_CURRENT;                  
    }
  } 
}

void peak_base_time_soft_charge_power_control () {
  if (relay_on[REL_OUT]) {
    long since_night_start = (millis() - night_start) * 16;
    if (relay_on[REL_IN] && bat_u > BAT_U_SOFT_CHARGE_H) {
      discharge_current = max(MIN_DISCHARGE_CURRENT, milliamps_pv_in / 2);
    } else if (bat_u < BAT_U_MIN_H) {
      discharge_current = MIN_DISCHARGE_CURRENT;
      } else if (since_night_start  < (night_length * PEAK_TIME_PERC) || since_night_start > night_length) { 
        discharge_current = power_peak;
      } 
      else {
        discharge_current = power_base;     
      }
  }
  set_power_of_discharge(discharge_current);   
}

// ******* Chapter SUBROUTINE Information on seriell Monitor
void Parameterausgabe_Serieller_Monitor() {
  outstr[17]="";
  if (0 == (ser_mon_line_counter % 32)) {
    Serial.println("Status of relays          Volt_PV  Volt_bat Disc_curr  Bat_Curr Bat_Power Night_start(min)  Night_length(min) System_time(min)");    
  }
  Serial.print(relay_on[REL_IN]  ? "In is on   " : "In is off  ");
  Serial.print(relay_on[REL_OUT]  ? "Out is on   " : "Out is off  ");
  outlong = pv_u;
  sprintf(outstr, "%10ld", outlong);
  Serial.print(outstr);
  outlong = bat_u;
  sprintf(outstr, "%10ld", outlong);
  Serial.print(outstr);
  sprintf(outstr, "%10d", discharge_current);
  Serial.print(outstr);
  sprintf(outstr, "%10d", milliamps);
  Serial.print(outstr);
  outlong = battery_power;
  sprintf(outstr, "%10ld", outlong);
  Serial.print(outstr);
  outlong =  night_start/3750;
  sprintf(outstr, "%17ld", outlong);
  Serial.print(outstr);
  outlong = night_length/60000;
  sprintf(outstr, "%17ld", outlong);
  Serial.print(outstr);
  outlong = millis()/3750;
  sprintf(outstr, "%17ld", outlong);
  Serial.println(outstr);
  ser_mon_line_counter++;
  Serial.flush();  
  delay(20);
}

ISR(WDT_vect)
{
}
/*
Prescaler Chart copied out of datasheet for reference:
 setup like this 32,4,2,1
 so for a delay of .125ms, you would set prescaler to 3, or 8 sec would be 32+1 = 33
 keep mind of clock speed reduction 8sec * 16 = 126 sec
  0 x x 0 0 0 = 0 = (prescaler) 2K (2048) cycles 16 ms
  0 x x 0 0 1 = 1 = (prescaler) 4K (4096) cycles 32 ms
  0 x x 0 1 0 = 2 = (prescaler) 8K (8192) cycles 64 ms
  0 x x 0 1 1 = 3 = (prescaler) 16K (16384) cycles 0.125 s
  0 x x 1 0 0 = 4 = (prescaler) 32K (32768) cycles 0.25 s
  0 x x 1 0 1 = 5 = (prescaler) 64K (65536) cycles 0.5 s
  0 x x 1 1 0 = 6 = (prescaler) 128K (131072) cycles 1.0 s
  0 x x 1 1 1 = 7 = (prescaler) 256K (262144) cycles 2.0 s
  1 x x 0 0 0 = 32 = (prescaler) 512K (524288) cycles 4.0 s
  1 x x 0 0 1 = 33 = (prescaler) 1024K (1048576) cycles 8.0 s
 */ 
void watchdog_deepsleep(byte prescaler)  
{
  CriticalSection
  {
    WDTCSR = (24);//change enable and WDE - also resets
    WDTCSR = (prescaler);//prescalers only - get rid of the WDE and WDCE bit
    WDTCSR |= (1 << 6); //enable interrupt mode
  }
  //SETUP WATCHDOG TIMER
  ADCSRA &= ~(1 << 7);//kill ADC
  //BOD DISABLE - this must be called right before the asm sleep instruction
  MCUCR |= (3 << 5); //set both BODS and BODSE at the same time
  MCUCR = (MCUCR & ~(1 << 5)) | (1 << 6); //then set the BODS bit and clear the BODSE bit at the same time
  asm volatile("sleep");//in line assembler to go to sleep
  ADCSRA |= (1 << 7);//woke back up, turn ON ADC
  if (prescaler <= 2) CriticalSection timer0_millis += 16*(prescaler+1)/16;
  else if (prescaler <= 7) CriticalSection timer0_millis += 125*(prescaler-2)/16;
  else CriticalSection timer0_millis += 4000/16*(prescaler-31);
}


