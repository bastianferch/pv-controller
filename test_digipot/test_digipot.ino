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
const int slaveSelectPin=10;
const int channel=0;
// const int shutDownPin = 7;

void setup(){
//clock to 1MHz
  CLKPR=0x80,
  CLKPR=0x02; //baud rate = 1200??
 // CLKPR=0x04; baud rate = 600
  // set the slaveSelectPin as an output
  pinMode (slaveSelectPin, OUTPUT);
  // initialize SPI
  SPI.begin();
  //Set all pots to zero as a starting point
  digitalPotWrite(0,0);
}

void loop(){
  digitalPotWrite(0,5);
  delay(500);
  digitalPotWrite(0,10);
  delay(500);
  digitalPotWrite(0,15);
  delay(500);
  digitalPotWrite(0,20);
  delay(500);
  digitalPotWrite(0,40);
  delay(500);
  digitalPotWrite(0,60);
  delay(500);
  digitalPotWrite(0,80);
  delay(500);
  digitalPotWrite(0,110);
  delay(500);
  digitalPotWrite(0,140);
  delay(500);
}
 
void digitalPotWrite(int address, int value) {
  // take the SS pin low to select the chip:
  digitalWrite(slaveSelectPin, LOW);
  delay(100);
  //  send in the address and value via SPI:
  SPI.transfer(address);
  SPI.transfer(value);
  // take the SS pin high to de-select the chip:
  delay(100);
  digitalWrite(slaveSelectPin, HIGH); 
}

