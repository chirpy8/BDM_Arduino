// BDM Flash Block Downloader 3

// works with Arduino program BDMFlashBlockDownload5

import processing.serial.*; 

  String cpuFileName = "undefined.B68";
 
  Serial port;                             // Create object from Serial class 
  int blocksRead = 0;
  
  // total bytes for cpu is 4 + 2 + 160*1029
  // total bytes for tpu is 4*1029
  // total bytes for slim is 4*1029
  //total is 4 + 168*1029 = 172878
  byte[] dataLog = new byte[172878];

  byte[] startSequence = {-1, 0x0, -1, 0x0, 85, -86, 85, -86};
  // ff 00 ff 00 55 aa 55 aa
  int byteCounter = 6; //take account of preamble bytes
  long timeOffset = 0;
  boolean initialHeaderWritten = false;
  boolean downloaded = false;
  boolean validFileName = false;
  PFont f;
  boolean displayChoice= false;

  
void downloadBlock(int blockSize, int startAddress)
{
  byte startAddress0 = (byte) ((startAddress & 0xf0000) >> 16);
  byte startAddress1 = (byte) ((startAddress & 0xff00) >> 8);
  byte startAddress2 = (byte) (startAddress & 0xff);
  int incomingBytesRead = 0;
  boolean startFlagReceived = false;
  int startMarkerCount = 0;
  int endMarkerCount = 0;
  boolean finished = false;
  boolean dataRead = false;
  timeOffset = millis() + 45000;
  
  do
  {
    //read the data from the Arduino feed
    if (startFlagReceived == false)
    {
      if (port.available() > 0)
      {
        byte inByte = (byte) port.read();
        if (inByte == startSequence[startMarkerCount])
        {
          startMarkerCount++;
        }
        else
        {
          startMarkerCount = 0;
        }
      }
      else
      {
        println("no bytes available");
        delay(200);
      }
      if (startMarkerCount == 8)
      {
        startFlagReceived = true;
        println("start flag detected");
      }
    }
    else
    {
      if (port.available() > 0)
      {
        //if start of very first first 1k block, write address header only  
        //if start of other 1 k block, write delimiter and address header
        if (((incomingBytesRead % 1024) == 0) && (incomingBytesRead < blockSize))
        {
          if (initialHeaderWritten)
          {
            dataLog[byteCounter] = 0;
            byteCounter++;
            dataLog[byteCounter] = 0;
            byteCounter++;
          }
          initialHeaderWritten = true;
          dataLog[byteCounter] = startAddress0;
          byteCounter++;
          dataLog[byteCounter] = startAddress1;
          if (startAddress2 == -4)
          {
            startAddress1++; //increment if startAddress2 about to rollover
          }
          byteCounter++;
          println(startAddress2, " 1k block read, byte counter is ", byteCounter, " incoming bytes is ", incomingBytesRead);
          dataLog[byteCounter] = startAddress2;
          byteCounter++;
          startAddress2 += 4;
        }
        
        // store incoming data
        if (incomingBytesRead < blockSize)
        {
          byte inByte = (byte) port.read();
          dataLog[byteCounter] = inByte;
          byteCounter++;
          incomingBytesRead++;
        }
       
      }
    
      if (incomingBytesRead == blockSize)
      {
        //read next 8 bytes and check equal to marker sequence, showing all bytes received
        if (port.available() > 0)
        {
          byte inByte = (byte) port.read();
          if (inByte == startSequence[endMarkerCount])
          {
            endMarkerCount++;
          }
          else
          {
            endMarkerCount = 0;
          }
        }
        if (endMarkerCount == 8)
        {
          //received whole block
          println("Block at ",startAddress," with size ",blockSize, " received, byte counter is ", byteCounter);
          dataRead = true;
        }
      }

      if ((millis() > timeOffset) || dataRead)
      {
        if (finished == false)
        {
          finished = true;
          if (!dataRead)
          {
            println("timeout for data capture");
            println("Bytes Read = ",byteCounter);
          }
        }
      }
      
    }
  }
  while (!finished);
}

void setup()
{
  size(480,100);
  f = createFont("Arial", 16, true);
}


void draw()
{
  textFont(f);
  fill(0);
  text("Press 1 for CPU1/IC501 download, 2 for CPU2/IC601 download", 20, 20);
  text("CPU1 filename will be F27C.B68, CPU2 will be F27D.B68", 20, 40);
  
  if ((keyPressed == true) && (key == '1'))
  {
    validFileName = true;
    cpuFileName = "F27xC.B68";
  }
  
  if ((keyPressed == true) && (key == '2'))
  {
    validFileName = true;
    cpuFileName = "F27xD.B68";
  }

  if ((!downloaded) && validFileName)
  {
    //download data
    downloaded = true;
    
    println("Initializing Serial");
    printArray(Serial.list());
    port = new Serial(this, Serial.list()[0], 500000); //open serial port to receive data arduino
    //select appropriate port by set [x] to target
    port.clear();
    println("Executing");
    
    //download main cpu
  
    //write 6 byte preamble
    dataLog[0] = 0x4;
    dataLog[1] = 0x0;
    dataLog[2] = 0x0;
    dataLog[3] = (byte) (168 & 0xff);
    dataLog[4] = 0x54;
    dataLog[5] = (byte) (0xaa & 0xff);
    
    // download first 128k flash block
    downloadBlock(131072, 0x0);
        
    //download 32k flash block
    downloadBlock(32768, 0xb8000);
    
    //download 4k tpu block
    downloadBlock(4096, 0x30000);
     
    //download 4k slim block
    downloadBlock(4096, 0xff000);
    
    dataLog[172876] = 0x0;
    dataLog[172877] = 0x0;
    
    //save main cpu program
    saveBytes(cpuFileName, dataLog);
    println("cpu file created");
  }
  
 //<>//
}
