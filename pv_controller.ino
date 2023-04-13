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

#define REL_IN 2                     // Input relay   2, 18
#define REL_OUT 3                    // Output relay  3, 19
#define MEAS_ON 4                    // Measure on    4, 20
#define BAT_U_PIN A6                 // Input voltage measurement battery A6
#define PV_U_PIN A7                  // Input voltage measurement PV A7 

#define PV_U_START_CHARGING 28000    // Voltage to start charinging battery
#define BAT_U_MAX 30600              // Voltage charging battery full
#define BAT_U_SOFT_CHARGE 30450      // Voltage to start soft charge near battery full
#define BAT_U_MAX_H 30200            // Voltage to restart charging battery (Hysteresis)
#define BAT_U_MIN 28300              // Voltage to stop discharging battery empty
#define BAT_U_MIN_H 29000            // Voltage to restart discharing (Hysteresis)
#define BAT_LIN 29700                // Battery linear voltage to power 28V until 29,7V
#define INVERTER_START_DIODE_U 260   // Voltage charging diode discharging starts
#define INVERTER_START_DIODE_U_H 295 // Voltage charging diode discharging stops (Hysteresis)
#define BATTERY_CAPACITY 45000       // 48.000mAH Battery Capacity
#define INVERTER_START_TIME 15000    // Inverter start producing power to grid after 4 min = 15000 * 16 millis /clock frequence 1 Mhz instead of 16 MHz
#define POWER_BASE_MAX 2500          // Maximum Power needed at base time in mA
#define PEAK_TIME_PERS 45            // Percentage of Peak time, each night firt x% are peak time, last 100%-x& are base time  

unsigned long night_start = 0;       // night_start stores time stamp in millis from last day to night dedection (1MHz)
unsigned long night_length = 43200000; // night_length calculated after fist day to nigth und nigth to day dedection in true millis, default 43.200.000 millis = 12h
unsigned long time_power_check;      // time stamp from last battery power incremental and decremental calculation
word bat_u, pv_u;                    // measured voltage at battery and PV
word last_bat_u, last_pv_u;          // voltage in mV
word battery_power;                  // calculated actual power in battery
word power_peak = 4000, power_base = 2500; // base (off peak) and peak power from battery to inverter
int discharge_current = 0, milliamps = 0;    // current in mA, milliamps to or from battery
byte ser_mon_line_counter = 0;       // Serial monitor line counter
boolean relay_on[] = {false, false, false, false}; // {NULL, NULL,REL_IN, REL_OUT}

void setup(){
  clockspeed();                      // reduce clockspeed to 1MHz
  Serial.begin(9600);                // open the serial port at 9600 bps: Attention clock rate 1MHz lead to Baudrate at the Computer 600 Baud
  pinmodes();
  initialize_SPI();
  start_measure_voltage();
  bat_u = measure_voltage(BAT_U_PIN);
  pv_u = measure_voltage(PV_U_PIN);
  stop_measure_voltage();

  estimate_bat_power();              // estimates Power in battery when adruino starts according to measured voltage from battery

  if (pv_u > PV_U_START_CHARGING) {  // if Volatge at PV is higher than usual voltage to start charging then
    if (bat_u < BAT_U_MAX) {         // iF battery voltage is lower than maximum battery voltage then start charging
      relay_on[REL_IN] = true;       
      do_relay_on(REL_IN,true);
    }        
  } else if (bat_u > BAT_U_MIN_H){   // if Volatge at PV is not higher than usual voltage to start charging then do discharging at base_power
    discharge_current = power_base;
    start_discharging(discharge_current);
  }  
}


// ******* CHAPERT SUBROUTINES SETUP
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

  last_bat_u = bat_u;
  last_pv_u = pv_u;
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
    }
  }
//                                         stop battery charging relay when no power from pv or battery voltage greater than maximum
  if (relay_on[REL_IN] && (pv_u < bat_u || bat_u > BAT_U_MAX)) {
    relay_on[REL_IN] = false;
    do_relay_on(REL_IN, false);
    if (bat_u > BAT_U_MAX) {
      battery_power = BATTERY_CAPACITY;
      relay_on[REL_OUT] = false;        // stop discharging for soft charging
      do_relay_on(REL_OUT, false);
    }
  }
//                                         start discharging relay when pv Voltage under start charging voltage or do soft charing?
  if ( !relay_on[REL_OUT] && bat_u > BAT_U_MIN_H) {
    if ((relay_on[REL_IN] && ((pv_u - bat_u) < INVERTER_START_DIODE_U || bat_u > BAT_U_SOFT_CHARGE)) || (!relay_on[REL_IN] &&  pv_u <=  PV_U_START_CHARGING)) {
      power_base_peak_calculation();
      discharge_current = power_base;
      start_discharging(discharge_current);
    }
  }
//                                         stop discharging relay when battery voltage less then minimum or when charging and Diode Voltage greater than fixed value so that normal pv is producing
  if (relay_on[REL_OUT]) { 
    if (bat_u < BAT_U_MIN || (relay_on[REL_IN] && (pv_u - bat_u) > INVERTER_START_DIODE_U_H)) {
      relay_on[REL_OUT] = false;
      do_relay_on(REL_OUT, false);
      if (bat_u < BAT_U_MIN) {
        battery_power = 0;
      }
    }
  }
  peak_base_time_soft_charge_power_controll();
  check_night_start();
  check_night_end();
  Parameterausgabe_Serieller_Monitor();
  delay(80);
  //delay(15000); //4 minutes (Clock frequency redused to 1 Mhz)
}

// ******* CHAPERT SUBROUTINES Battery and PV voltage measurement
void start_measure_voltage(){
  digitalWrite(MEAS_ON,1);
  delay(10);
}

word measure_voltage(int analog_pin){
  return map(analogRead(analog_pin),0,1023,23650, 33650); 
}

void stop_measure_voltage(){
  digitalWrite(MEAS_ON,0);
}

// ******* CHAPERT SUBROUTINE Battery Power Calculation
long calculate_power_in_battery(long powermah){
  word diode_u = 0;
  milliamps = 0;
  diode_u = (pv_u - bat_u + last_pv_u - last_bat_u) / 2;
  if (relay_on[REL_IN] && (diode_u>0)){  
    switch (diode_u) {
      case 0 ... 250: 
        milliamps = map (diode_u, 0, 250, 100, 300); 
        break;
      case 251 ... 320:     
        milliamps = map (diode_u, 251, 320, 350, 1000); 
        break;
      case 321 ... 335:           
        milliamps = map (diode_u, 321, 335, 1000, 2000); 
        break;
      case 336 ... 390:           
        milliamps = map (diode_u, 336, 390, 2000, 8000); 
        break;
      case 391 ... 410:           
        milliamps = map (diode_u, 391, 410, 8000, 9000); 
        break;
      default:           
        milliamps = map (diode_u, 410, 430, 9000, 11000); 
        break;
    }
  }
  if (relay_on[REL_OUT]) {
    milliamps = milliamps - discharge_current;
  }
  powermah = min(max(0, powermah + (millis() - time_power_check) * milliamps / 225000), BATTERY_CAPACITY);
  time_power_check = millis();
}


// ******* CHAPERT SUBROUTINES IO Management and SPI Management DigitalResistance
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
  value = map(value,0,6500,0,58);
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


// ******* CHAPERT SUBROUTINES Night Calulations
void check_night_start(){
  if(last_pv_u > 8000 && pv_u < 8000){
    night_start = millis();
  }
}
void check_night_end(){
  if(last_pv_u < 8000 && pv_u > 8000 && night_start > 0 && (millis() - night_start) > 675000) {
    night_length = 16 * (millis() - night_start);    
  }
}


// ******* CHAPERT SUBROUTINES Night Power Management
void power_base_peak_calculation () {
  if (night_start != 0) {
    power_base = max(1000, (battery_power / (night_length / 3600000 + 1) * 0,6));
    power_peak = max(1000, (battery_power / (night_length / 3600000 + 1) * 1,1));
    if (power_base > POWER_BASE_MAX) {
      power_peak = min(6500, (power_peak + (power_base - POWER_BASE_MAX)));
      power_base = POWER_BASE_MAX;                  
    }
  } 
}

void peak_base_time_soft_charge_power_controll () {
  if (relay_on[REL_OUT]) {
    if (relay_on[REL_IN] && bat_u > BAT_U_SOFT_CHARGE) {
      set_power_of_discharge(max(500, min(7000, milliamps-2000)));      
    } else if ((millis() - night_start) * 16 < (night_length * PEAK_TIME_PERS) || ((millis() - night_start) * 16) > night_length) {
      set_power_of_discharge(power_peak);
    } 
    else {
      set_power_of_discharge(power_base);     
    }
  } 
}

// ******* CHAPERT SUBROUTINE Information on seriell Monitor
void Parameterausgabe_Serieller_Monitor() {
  char outstr[6]="";
  if (0 == (ser_mon_line_counter % 32)) {
    Serial.println("Status of relays          Volt_PV  Volt_bat Disc_curr  Bat_Curr Bat_Power Night_start(min)  Night_length(min)");    
  }
  Serial.print(relay_on[REL_IN]  ? "In is on   " : "In is off  ");
  Serial.print(relay_on[REL_OUT]  ? "Out is on   " : "Out is off  ");
  sprintf(outstr, "%10d", pv_u);
  Serial.print(outstr);
  sprintf(outstr, "%10d",bat_u);
  Serial.print(outstr);
  sprintf(outstr, "%10d", discharge_current);
  Serial.print(outstr);
  sprintf(outstr, "%10d", milliamps);
  Serial.print(outstr);
  sprintf(outstr, "%10d", battery_power);
  Serial.print(outstr);
  sprintf(outstr, "%17d", night_start/3750);
  Serial.print(outstr);
  sprintf(outstr, "%17d", night_length/60000);
  Serial.println(outstr);
  ser_mon_line_counter++;
}

void sleep(){
  // TODO
}