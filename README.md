# BDM_Arduino
Limited implementation of Motorola BDM interface on Arduino Uno R3

Rudimentary Arduino Uno R3 based implementaton of Motorola BDM (Background Debug Mode) interface. Does not support breakpoints, only execution of BDM commands. For more context see the the post [here](https://chirpy8.github.io/software%20and%20firmware/post-Reflash1/). The Arduino program is configured to readout the contents of the EEPROM memory blocks of a Motorola 68HC16Y5 or Y6 processor, as used in a Jaguar AJ27 ECU. Also, a very basic PC program written in the Processing language is included, to receive downloaded EPROM data from the Arduino over serial port, and save as a Jaguar ".b68" format flash file.

