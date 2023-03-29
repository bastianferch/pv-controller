/*
 The AD8400 is SPI-compatible. To command it you send two bytes. 
 The first byte is the channel number starting at zero: (0)
 The second byte is the resistance value for the channel: (0 - 255).
  * CS - to digital pin 10  (SS pin)
  * SDI - to digital pin 11 (MOSI pin)
  * CLK - to digital pin 13 (SCK pin)
*/

#include <SPI.h>
// set pin 10 as the slave select for the digital pot:
const int slaveSelectPin = 10;
const int channel = 0;
// const int shutDownPin = 7;



#define REL_IN 2 // Input relay 2, 18
#define REL_OUT 3 // Output relay 3, 19
#define MEAS_ON 4 // Measure on 4,20
#define BAT_U_PIN A6 //  input voltage measurement battery A6
#define PV_U_PIN A7 // input voltage measurement PV A7 
unsigned long night_start;
unsigned long night_length;
word bat_u=0, pv_u=0;
word last_bat_u, last_pv_u;


void setup(){
//clock to 1MHz
  CLKPR=0x80,
  CLKPR=0x04;
  // open the serial port at 9600 bps: Achtung aufgrund der Reduktion der Taktfrequenz die Baudrate am Computer 600
  Serial.begin(9600);
  pinMode(REL_IN, OUTPUT);
  pinMode(REL_OUT, OUTPUT);
  pinMode(MEAS_ON, OUTPUT);
  pinMode(BAT_U_PIN, INPUT);
  pinMode(PV_U_PIN, INPUT);

  // set the slaveSelectPin as an output
  pinMode (slaveSelectPin, OUTPUT);
  // initialize SPI
  SPI.begin();
  //Set all pots to zero as a starting point
  set_power_of_discharge(0);
}

void loop(){
  last_bat_u = bat_u;
  last_pv_u = pv_u;
  start_measure_voltage();
  bat_u = map(measure_voltage(BAT_U_PIN),0,1023,23650, 33650);
  pv_u = map(measure_voltage(PV_U_PIN),0,1023,23650, 33650);
  stop_measure_voltage();
  Serial.println("beide Rel on");
  do_battery_discharging(true);
  set_power_of_discharge(1);
  do_battery_charging(true);
  //Serial.println("Spannung PV(mV):" + String(pv_u) + "Spannug Bat(mV):" + String(bat_u));
  delay(1000);
  set_power_of_discharge(2);
  delay(1000);
  set_power_of_discharge(3);
  delay(1000);
  set_power_of_discharge(4);
  delay(1000);
  Serial.println("beide Rel off");
  do_battery_discharging(false);
  do_battery_charging(false);
  delay(1000);
    //check_night_start();
}


// Battery and PV voltage measurement function
void start_measure_voltage(){
  digitalWrite(MEAS_ON,1);
  delay(10);
}

word measure_voltage(int analog_pin){
  return analogRead(analog_pin); // TODO function for calculating the real value in millivolt
}

void stop_measure_voltage(){
  digitalWrite(MEAS_ON,0);
}

void do_battery_charging(boolean doit){
 if (doit) {
    digitalWrite(REL_IN, HIGH);
  } else {
    digitalWrite(REL_IN, LOW);
  }     
}

void do_battery_discharging(boolean doit){
 if (doit) {
    digitalWrite(REL_OUT, HIGH);
  } else {
    digitalWrite(REL_OUT, LOW);
  }     
}

// Logic to measure day length
// the duration of the night results from the time the PV voltage falls below 8000mV (Caution: short-term exceeding possible)
/**
2 longints für Nachtbeginn
*/

void check_night_start(){
  if(last_pv_u > 8000 & pv_u < 8000){
    night_start = millis();
  }
}
void check_night_end(){
  if(last_pv_u < 8000 & pv_u > 8000 & (millis() - night_start) > 675000){
    night_length = millis() - night_start;    
  }
}


//Battery discharge power control function
void set_power_of_discharge(int value) {
  digitalPotWrite(0, value*15);
}
 
void digitalPotWrite(int address, int value) {
  // take the SS pin low to select the chip:
  digitalWrite(slaveSelectPin, LOW);
  //  send in the address and value via SPI:
  SPI.transfer(address);
  SPI.transfer(value);
  // take the SS pin high to de-select the chip:
  digitalWrite(slaveSelectPin, HIGH); 
}


//Logik zur Berechnung des Ladezustandes der Batterie
void calculate_power_in_battery(){
  
}

//Entscheidungslogik Programmkern
// alle zwei Minuten
// es gibt ein Eingangrelais mit zwei Zuständen, direkt (low) und Batterie laden (high)
// es gibt ein Ausgangsrelais mit zwei Zuständen, direkt (low) und Batterie entladen (hig) 

// Eingangsrelais schalten wie folgt:
// wenn aus und UPV > 28V und UBat < 30200 mV, dann ein


void sleep(){
  // TODO
}