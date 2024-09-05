sony_slink
==========

These are Arduino sketches to interface with Sony equipment using the
S-Link/Control A1 protocols. There are two sketches here:
* Target Arduino, USB serial communication
* Target ESP8266, wifi + websocket communication

I'm using this to control and read information from a Sony STR-DB2000
receiver.

To physically connect an Arduino/ESP8266 device to the S-Link bus you need
some additional components. Here's a schematic for the Arduino (ESP8266 is similar, but uses different pins):

![circuit](circuit.png)

Use a 3.5 mm mono plug to connect the Arduino/ESP8266 to the S-Link/Control A1 port of the Sony equipment and connect the Arduino to a USB port in your computer for power and serial communication.
The ESP8266 only needs power since it uses wifi for communication instead of the serial connection.

Use the serial port or websocket interface to send commands to the Sony equipment. Commands are sent as lines of hexadecimal data. Here are some commands to try:

* c02e - power on amplifier
* c02f - power off amplifier
* c06a - query amplifier name

or, some newer amplifiers use a different prefix

* 702e - power on amplifier
* 702f - power off amplifier
* 706a - query amplifier name

ESP8266 accepts command "debug" to enable debugging.


Take a look [here](http://boehmel.de/slink.htm) for more commands.

----

Reference documents:
* http://web.archive.org/web/20070720171202/http://www.reza.net/slink/text.txt
* http://web.archive.org/web/20070705130320/http://www.undeadscientist.com/slink/
* http://web.archive.org/web/20180831072659/http://boehmel.de/slink.htm
