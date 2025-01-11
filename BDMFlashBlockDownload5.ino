/*
Implements a basic BDM interface using simple custom shield

Created to download firmware from a Jaguar AJ27 ECU
Places CPU in BDM mode and downloads a flash blocks and slim memory, sends over serial
Interrupt on Freeze detects loss of BDM and re-executes placing back into BDM mode
Timer anticpates loss of BDM every ~50 - 80 ms, (configure based on watchdog timeout),
and allows execution to freeze prior to loss,
so that loss of BDM does not interfere with commands
Needs to determine if target chip is a Y5 or Y6 before downloading

D13 - SCK
D12 - MISO
D11 - MOSI
D10 - not used

D7 - Output high to assert reset on MCU via NPN transistor, low off
D6 - Aux SCK - as input during SPI, as output to bit bang initial bit (rising edge)
D5 - Freeze (input) (high = asserted)
D4 - Aux MOSI - as input during SPI, used to force MOSI low to send initial bit as 0
D3 - Aux MISO - used to read initial bit from MCU

*/

#define DELAY_CYCLES(n) __builtin_avr_delay_cycles(n)

#include <SPI.h>

int freezeVal = LOW;

byte statusBit = 0;
//storage for a retrieved data word from SPI
byte highRX = 0;
byte lowRX = 0;
//storage for multiple retrieved data words from command
unsigned int word1 = 0;
unsigned int word2 = 0;
unsigned int word3 = 0;
unsigned int word4 = 0;
unsigned int word5 = 0;
unsigned int word6 = 0;
unsigned int word7 = 0;

int responseStatus = 0;
const int DATA_VALID = 0;
const int COMMAND_COMPLETE = 1;
const int NOT_READY = 2;
const int ILLEGAL_COMMAND = 3;

 int BDMResponse = 0;

 volatile boolean suspendBDMoperations = true;

 boolean isY6 = false;

//timer 1 output compare A interrupt
//set suspendBDMOperations flag
 ISR (TIMER1_COMPA_vect)
 {
   suspendBDMoperations = true;
 }

// pin change interrupt on pin D5 (freeze)
// detects loss of BDM, and re-initiates BDM
 ISR (PCINT2_vect)
 {
   boolean freeze;
   DELAY_CYCLES(5); //let settle
   freeze = (digitalRead(5) == HIGH);

   if (freeze)
   {
     // force MOSI low and clock out initial bit
    digitalWrite(4, LOW);
    pinMode(4, OUTPUT);
    DELAY_CYCLES(10); //let settle
    digitalWrite(6, HIGH); //rising clock pulse
    DELAY_CYCLES(10); //let settle
    pinMode(6, INPUT_PULLUP); //clock floats high
    pinMode(4, INPUT); //MOSI floats high (external pullup)

    // use SPI to clock out 16 bit message - send NOP
    pinMode(11, OUTPUT); //MOSI enabled
    pinMode(13, OUTPUT); //SCK enabled

    sendSPIbyte(0x17);
    sendSPIbyte(0x89);

    suspendBDMoperations = false;

    //start timer to anticipate next loss of BDM
    TCNT1 = 0;

    // add any operations required after reset here
    if (isY6)
    {
      writeDataWord(0xff780, 0x1ac0); //this flash block is not enabled after reset
    }
    else
    {
      writeDataWord(0xff7c0, 0x1ac0); //this flash block is not enabled after reset
    }
   }
   else
   {
    //re-initiate BDM  
    pinMode(11, INPUT);  //MOSI for SPI disabled
    pinMode(13, INPUT); //SCK for SPI disabled
    pinMode(6, OUTPUT); //force bkpt low
    digitalWrite(6, LOW);
    digitalWrite(7,HIGH);  //assert reset
    DELAY_CYCLES(10);
    digitalWrite(7, LOW);  //release reset 
  }
 }

//sends a byte over SPI, and waits until transmission complete before retruning
byte sendSPIbyte(byte sendByte)
{
  SPDR = sendByte;
  while ((SPSR & 0x80) == 0) //wait for transmission to complete
  {
  }
  byte inByte = SPDR;
  return(inByte);
}

// sends a BDM command to MCU, receives previous response
// command to send is passed in as 16 bit parameter
// response is stored in highRX and lowRX, and status is return value
int sendBDMCommand(unsigned int value)
{
    byte highVal = value >> 8;
    byte lowVal = value & 0xff;

    //prepare to send initial bit
    pinMode(11, INPUT);  //MOSI for SPI disabled
    pinMode(13, INPUT); //SCK for SPI disabled

    // force MOSI low and clock out initial bit
    digitalWrite(4, LOW);
    pinMode(4, OUTPUT);

    // create clock falling edge
    digitalWrite(6, HIGH);
    pinMode(6, OUTPUT);
    digitalWrite(6, LOW);
    //read status bit from MCU
    int statusBit = digitalRead(3);
    // clock rising edge
    digitalWrite(6, HIGH);

    //prepare for SPI
    pinMode(6, INPUT_PULLUP); //Aux clock floats high
    pinMode(4, INPUT); //Aux MOSI floats high (external pullup)
    pinMode(11, OUTPUT); //MOSI enabled
    pinMode(13, OUTPUT); //SCK enabled

    //send command
    highRX = sendSPIbyte(highVal);
    lowRX = sendSPIbyte(lowVal);

    //compute response message status
    if (statusBit == LOW)
    {
     if ((highRX == 0xff) && (lowRX == 0xff))
     {
       responseStatus = COMMAND_COMPLETE;
      }
     else
     {
       responseStatus = DATA_VALID;
     }
    }
    else
    {
     if ((highRX == 0x00) && (lowRX == 0x00))
     {
       responseStatus = NOT_READY;
     }
     else
     {
        responseStatus = ILLEGAL_COMMAND; // should really test if == 0000
      }
    }

  return responseStatus;
}

//function to execute no operation and check correct response
unsigned int NOP()
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

do
{
      //Send RDMEM command
    unsigned int BDMResponse = sendBDMCommand(0x1789); 

    BDMResponse = sendBDMCommand(0x1789);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 3));

  // if 3 failures, then show error condition and halt
  if (errorCount == 3)
  {
    showError();
  }

}

//function to read a data memory word and return its value
unsigned int readDataWord(unsigned long targetAddress)
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;

  unsigned int bankAddress = (targetAddress >> 16);
  unsigned int offsetAddress = targetAddress & 0xffff;
  unsigned int commandCode;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

  do
  {
      //Send RDMEM command
    unsigned int BDMResponse = sendBDMCommand(0x1784); 

   //bank address
    commandCode = bankAddress + 0x4000;
    BDMResponse = sendBDMCommand(commandCode);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 3));

  // if 3 failures, then show error condition and halt
  if (errorCount == 3)
  {
    showError();
  }

  //offset address
  commandCode = offsetAddress;
  BDMResponse = sendBDMCommand(commandCode);

  //receive the 2 bytes of data
  do
  {
    BDMResponse = sendBDMCommand(0x1789);
    notReady = (BDMResponse == NOT_READY);
  }
  while (notReady);

  unsigned int result = (highRX << 8) + lowRX;

  return result;
}

//function to read a data memory word and return its value
unsigned int readDataByte(unsigned long targetAddress)
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;

  unsigned int bankAddress = (targetAddress >> 16);
  unsigned int offsetAddress = targetAddress & 0xffff;
  unsigned int commandCode;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

do
{
      //Send RDMEM command
    unsigned int BDMResponse = sendBDMCommand(0x1784); 

   //bank address
    commandCode = bankAddress;
    BDMResponse = sendBDMCommand(commandCode);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 100));

  // if 3 failures, then show error condition and halt
  if (errorCount == 100)
  {
    showError();
  }

  //offset address
  commandCode = offsetAddress;
  BDMResponse = sendBDMCommand(commandCode);

  //receive the 2 bytes of data
  do
  {
    BDMResponse = sendBDMCommand(0x1789);
    notReady = (BDMResponse == NOT_READY);
  }
  while (notReady);

  unsigned int result = lowRX;

  return result;
}


//function to read a program word and return its value
unsigned int readProgramWord(unsigned long targetAddress)
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;

  unsigned int bankAddress = (targetAddress >> 16);
  unsigned int offsetAddress = targetAddress & 0xffff;
  unsigned int commandCode;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

do
{
      //Send RDMEM command
    unsigned int BDMResponse = sendBDMCommand(0x1786); 

   //bank address
    commandCode = bankAddress;
    BDMResponse = sendBDMCommand(commandCode);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 3));

  // if 3 failures, then show error condition and halt
  if (errorCount == 3)
  {
    showError();
  }

  //offset address
  commandCode = offsetAddress;
  BDMResponse = sendBDMCommand(commandCode);

  //receive the 2 bytes of data
  do
  {
    BDMResponse = sendBDMCommand(0x1789);
    notReady = (BDMResponse == NOT_READY);
  }
  while (notReady);

  unsigned int result = (highRX << 8) + lowRX;

  return result;
}


//function to write a data memory word
void writeDataWord(unsigned long targetAddress, unsigned int value)
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;
  unsigned int commandCode;

  unsigned int bankAddress = (targetAddress >> 16);
  unsigned int offsetAddress = targetAddress & 0xffff;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

do
{
      //Send WDMEM command
    unsigned int BDMResponse = sendBDMCommand(0x1785); 

   //bank address, writing word
    commandCode = bankAddress + 0x4000;
    BDMResponse = sendBDMCommand(commandCode);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 3));

  // if 3 failures, then show error condition and halt
  if (errorCount == 3)
  {
    showError();
  }

    //offset address
    commandCode = offsetAddress;
    BDMResponse = sendBDMCommand(commandCode);

    //data value to write
    commandCode = value;
    BDMResponse = sendBDMCommand(commandCode);

  do
  {
     BDMResponse = sendBDMCommand(0x1789);
     notReady = (BDMResponse == NOT_READY);
   }
   while (notReady);

}


//function to write a program word
void writeProgramWord(unsigned long targetAddress, unsigned int value)
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;

  unsigned int bankAddress = (targetAddress >> 16);
  unsigned int offsetAddress = targetAddress & 0xffff;
  unsigned int commandCode;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

do
{
      //Send WDMEM command
    unsigned int BDMResponse = sendBDMCommand(0x1787); 

   //bank address, writing word
    commandCode = bankAddress;
    BDMResponse = sendBDMCommand(commandCode);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 3));

  // if 3 failures, then show error condition and halt
  if (errorCount == 3)
  {
    showError();
  }

  //offset address
  commandCode = offsetAddress;
  BDMResponse = sendBDMCommand(commandCode);

  //data value to write
  commandCode = value;
  BDMResponse = sendBDMCommand(commandCode);

  //receive the command complete message
  do
  {
    BDMResponse = sendBDMCommand(0x1789);
    notReady = (BDMResponse == NOT_READY);
  }
  while (notReady);

}

//function to read 128 byte block of data memory and print output to Serial Port
void displayDataBlock(unsigned long startAddr)
{
  byte buffer[128];
  Serial.print("Start Address ");
  Serial.println(startAddr, HEX);
  unsigned long targetAddr = startAddr;

    for (int x=0;x<(4*16);x++)
    {
      unsigned int dataRead = readDataWord(targetAddr);
      byte byte1 = (dataRead & 0xff);
      byte byte2 = (dataRead >> 8);
      buffer[2*x] = byte2;
      buffer[2*x + 1] = byte1;
      targetAddr = targetAddr + 2;
    }

    for (int x=0;x<8;x++)
    {
      for (int y = 0;y<16;y++)
      {
        if ((buffer[16*x + y] & 0xf0) == 0)
        {
          Serial.print("0");
        }
        Serial.print(buffer[16*x + y], HEX);
        Serial.print(" ");
      }
      Serial.println();
    }
}

//function to read PC and SP
unsigned int readPCandSP()
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;
  unsigned int BDMResponse;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

  do
  {
      //Send RPCSP command
    BDMResponse = sendBDMCommand(0x1782); 

   //get response
    BDMResponse = sendBDMCommand(0);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 3));

  // if 3 failures, then show error condition and halt
  if (errorCount == 3)
  {
    showError();
  }

  word1 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word2 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word3 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word4 = (highRX << 8) + lowRX;; // PK

  unsigned int result = sendBDMCommand(0); //Complete

  Serial.print("Response ");
  Serial.println(result, HEX);
  Serial.print("PK ");
  Serial.println(word1, HEX);
  Serial.print("PC ");
  Serial.println(word2, HEX);
  Serial.print("SK ");
  Serial.println(word3, HEX);
  Serial.print("SP ");
  Serial.println(word4, HEX);

  return result;
}

//function to read registers
unsigned int readRegisters()
{
  boolean illegalResponse = false;
  int errorCount = 0;
  boolean notReady = false;
  unsigned int BDMResponse;

  // pause while suspend operations flag is set
  while (suspendBDMoperations)
  {
  };

  do
  {
      //Send RPCSP command
    BDMResponse = sendBDMCommand(0x1780); 

   //get response
    BDMResponse = sendBDMCommand(0x007f);
    illegalResponse = (BDMResponse == ILLEGAL_COMMAND);

    //if command received an illegal command response, then try again up to 3 times
    if (illegalResponse)
    {
      errorCount++;
    }
  }
  while (illegalResponse && (errorCount < 3));

  // if 3 failures, then show error condition and halt
  if (errorCount == 3)
  {
    showError();
  }

  word1 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word2 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word3 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word4 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word5 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word6 = (highRX << 8) + lowRX;; // PK
  sendBDMCommand(0);
  word7 = (highRX << 8) + lowRX;; // PK

  unsigned int result = sendBDMCommand(0); //Complete

  Serial.print("Response ");
  Serial.println(result, HEX);
  Serial.print("CCR ");
  Serial.println(word1, HEX);
  Serial.print("K ");
  Serial.println(word2, HEX);
  Serial.print("IZ ");
  Serial.println(word3, HEX);
  Serial.print("IY ");
  Serial.println(word4, HEX);
  Serial.print("IX ");
  Serial.println(word5, HEX);
  Serial.print("E ");
  Serial.println(word6, HEX);
  Serial.print("D ");
  Serial.println(word7, HEX);
  return result;
}

//show error stops execution
void showError()
{
  Serial.println("Error, execution stopped");

  while(true){};

}

// function to read  block of data and send as bytes, preceded by start marker
void sendMemBlock(unsigned long startAddr, long numBytes)
{
  byte dataByte;
  unsigned long targetAddr = startAddr;

  for (long x=0;x<numBytes/2;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }
}

// function to read  slim block and send as bytes, preceded by start marker
void sendSlimMemBlock()
{
  byte dataByte;
  unsigned long targetAddr;

  sendStartMarker();

  //null for ff000 - ff1ff
  for (int x=0;x<512;x++)
  {
    Serial.write(0xff);
  }

  //valid for ff200 - ff3ff
  targetAddr = 0xff200;
  for (int x=0;x<256;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }

  //null for ff400 - ff5ff
  for (int x=0;x<512;x++)
  {
    Serial.write(0xff);
  }

  //valid for ff600 - ff63f
  targetAddr = 0xff600;
  for (int x=0;x<32;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }

  //null for ff640 - ff77f
  for (int x=0;x<320;x++)
  {
    Serial.write(0xff);
  }

  if(isY6)
  {
    //valid for ff780 - ff83f
    targetAddr = 0xff780;
    for (int x=0;x<96;x++)
    {
      unsigned int dataRead = readDataWord(targetAddr);
      byte byte1 = (dataRead & 0xff);
      byte byte2 = (dataRead >> 8);
      Serial.write(byte2);
      Serial.write(byte1);
      targetAddr = targetAddr + 2;
    }

    //null for ff840 - ff860
    for (int x=0;x<32;x++)
    {
      Serial.write(0xff);
    }
  }
  else
  {
    //null for ff780 - ff7c0
    for (int x=0;x<64;x++)
    {
      Serial.write(0xff);
    }

    //valid for ff7c0 - ff860
    targetAddr = 0xff7c0;
    for (int x=0;x<80;x++)
    {
      unsigned int dataRead = readDataWord(targetAddr);
      byte byte1 = (dataRead & 0xff);
      byte byte2 = (dataRead >> 8);
      Serial.write(byte2);
      Serial.write(byte1);
      targetAddr = targetAddr + 2;
    }

  }

  //valid for ff860 - ff87f
  targetAddr = 0xff860;
  for (int x=0;x<16;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }

  //null for ff880 - ff900
  for (int x=0;x<128;x++)
  {
    Serial.write(0xff);
  }

  //valid for ff900 - ffabf
  targetAddr = 0xff900;
  for (int x=0;x<224;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }

  //null for ffac0 - ffb00
  for (int x=0;x<64;x++)
  {
    Serial.write(0xff);
  }

  //valid for ffb00 - ffb07
  targetAddr = 0xffb00;
  for (int x=0;x<4;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }

  //null for ffb08 - ffc00
  for (int x=0;x<248;x++)
  {
    Serial.write(0xff);
  }

  //valid for ffc00 - ffc3f
  targetAddr = 0xffc00;
  for (int x=0;x<32;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }

  //null for ffc40 - ffe00
  for (int x=0;x<448;x++)
  {
    Serial.write(0xff);
  }

    //valid for ffe00 - fffff
  targetAddr = 0xffe00;
  for (int x=0;x<256;x++)
  {
    unsigned int dataRead = readDataWord(targetAddr);
    byte byte1 = (dataRead & 0xff);
    byte byte2 = (dataRead >> 8);
    Serial.write(byte2);
    Serial.write(byte1);
    targetAddr = targetAddr + 2;
  }

  sendStartMarker();

}

// send a start marker over the serial interface
void sendStartMarker()
{
    byte startMarker[8];

  //write a start demarcation block to aid parsing the data file, 4 bytes of 0xff
    startMarker[0] = 0xff;
    startMarker[1] = 0x0;
    startMarker[2] = 0xff;
    startMarker[3] = 0x0;
    startMarker[4] = 0x55;
    startMarker[5] = 0xaa;
    startMarker[6] = 0x55;
    startMarker[7] = 0xaa;

  Serial.write(startMarker,8);
}

// *******************************************
//overall setup and one time routine
 void setup() {

  //enable pin change interrupt on freeze pin
  PCMSK2 |= bit(PCINT21);
  PCIFR |= bit(PCIF2);
  PCICR |= bit(PCIE2);


  Serial.begin(500000);

   //configure reset pin/state
  digitalWrite(7, LOW);
  pinMode(7, OUTPUT);
  //configure bkpt/dsclk
  pinMode(6, INPUT_PULLUP);
  //configure freeze
  pinMode(5, INPUT);
  //configure Aux MOSI
  pinMode(4, INPUT);
  //configure Aux MISO
  pinMode(3, INPUT);

  //configure arduino SPI interface
  // mode 3, CPOL=1, CPHA=1, DORD=0 for MSB first
  SPCR = 0x5d; //SPI no interrupt, mode 3, MSB first, 500 kbps clock (fosc/32)
  SPSR = 0x01; //SPI interface 500 kbps clock set SPI2X bit 0

  // SPDR is the SPI data register for byte input/output
  // SPSR bit 7 is the SPI flag which is set for completion of data read/write (collision flag is bit 6)
  digitalWrite(10, HIGH);
  pinMode(10, OUTPUT); //ensure slave select does not disable SPI operation
  pinMode(11, INPUT);  //MOSI for SPI disabled
  pinMode(13, INPUT); //SCK for SPI disabled
  
  //initiate BDM ****************************

    digitalWrite(7,HIGH);  //assert reset
    pinMode(6, OUTPUT); //force bkpt low
    digitalWrite(6, LOW);
    DELAY_CYCLES(10);
    digitalWrite(7, LOW);  //release reset 

//configure timer 1
 // Mode 4 CTC on OCRA
 // prescale 64 for 4 us tick
 // interrupt on OCA
 // timer set for 50 ms
  TCCR1A = 0;
  TCCR1B = 0xb;
  OCR1A = 12500;
  //start timer to anticipate next loss of BDM
  TCNT1 = 0;
  TIMSK1 = 2;

  // initialization is now complete

  //determine if the chip is a Y5 or a Y6
  unsigned int testVal = readDataByte(0xffa02);

  if (testVal == 0xd0)
  {
    isY6 = true;
  }

  if (isY6)
  {
    sendStartMarker();  

    //output block 0
    sendMemBlock(0, 65536);

    sendMemBlock(0x10000, 65536);

    sendStartMarker();  

    sendStartMarker();  

    //output block 1
    sendMemBlock(0xb8000, 32768);

    sendStartMarker();  

  }
  else
  {

    sendStartMarker();  

    //output block 0
    sendMemBlock(0, 32768);

    sendMemBlock(0x8000, 32768);

    sendMemBlock(0x10000, 32768);

    sendMemBlock(0x18000, 32768);

    sendStartMarker();  
    sendStartMarker();  

    //output block 1
    sendMemBlock(0xb8000, 32768);

    sendStartMarker();  
  }

  //output tpu block
  sendStartMarker();  
  sendMemBlock(0x30000, 4096);
  sendStartMarker();  

  //output slim block
  sendSlimMemBlock();

}

void loop() {
  // put your main code here, to run repeatedly:

}
