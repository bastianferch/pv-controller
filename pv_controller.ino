#define REL_IN 2 // Input relay 2, 18
#define REL_OUT 3 // Output relay 3, 19
#define MEAS_ON 4 // Measure on 4,20
#define BAT_U_PIN A6 //  input voltage measurement battery A6
#define PV_U_PIN A7 // input voltage measurement PV A7 

//print = Serial.println(val);

unsigned long night_start;
unsigned long night_length;
word bat_u=0, pv_u=0;
word last_bat_u, last_pv_u;

//clock to 1MHz
void setup(){
  CLKPR=0x80,
  CLKPR=0x04;
  pinMode(REL_IN, OUTPUT);
  pinMode(REL_OUT, OUTPUT);
  pinMode(MEAS_ON, OUTPUT);
  pinMode(BAT_U_PIN, INPUT);
  pinMode(PV_U_PIN, INPUT);
}

void loop(){
  last_bat_u = bat_u;
  last_pv_u = pv_u;
  start_measure_voltage();
  bat_u = measure_voltage(BAT_U_PIN);
  pv_u = measure_voltage(PV_U_PIN);
  stop_measure_voltage();
  check_night_start();
}


// Battery and PV voltage measurement function
void start_measure_voltage(){
  digitalWrite(MEAS_ON,1);
  delay(10);
}

word measure_voltage(int analog_pin){
  return analogRead(analog_pin) // TODO function for calculating the real value in millivolt
}

void stop_measure_voltage(){
  digitalWrite(MEAS_ON,0);
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
void set_power_of_discharge(){
  //TODO
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