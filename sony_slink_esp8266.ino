#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>

ESP8266WiFiMulti wifiMulti;
WebSocketsServer webSocket = WebSocketsServer(80);

const byte OUTPUT_PIN = 4;
const byte INPUT_PIN = 5;
const byte PULSE_BUFFER_SIZE = 100;

volatile unsigned long timeLowTransition = 0;
volatile byte bufferReadPosition = 0;
volatile byte bufferWritePosition = 0;
volatile byte pulseBuffer[PULSE_BUFFER_SIZE];

// debug data
uint8_t debugClient = WEBSOCKETS_SERVER_CLIENT_MAX;
String pulseLengths;
bool bufferOverflowDetected = false;

void setup()
{
  ArduinoOTA.begin();

  WiFi.hostname("sony_slink");
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP("Network_name", "Network_password");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);
  pinMode(INPUT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(INPUT_PIN), busChange, CHANGE);
}

void webSocketEvent(uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length) {
    switch(type) {
      case WStype_DISCONNECTED:
        if (clientNum == debugClient) {
          debugClient = WEBSOCKETS_SERVER_CLIENT_MAX;
        }
        break;
      case WStype_CONNECTED:
        break;
      case WStype_TEXT:
        if (strcmp((const char*)payload, "debug") == 0) {
          debugClient = clientNum;
          DEBUG("Debug output enabled");
          break;
        }

        // A hexadecimal command is expected
        if (length % 2 != 0) {
          DEBUG("Uneven length of S-link command");
          return;
        }

        byte commandBytes[length / 2];
        for (int i = 0; i < length; i += 2) {
          if (!isHexadecimalDigit(char(payload[i])) || !isHexadecimalDigit(char(payload[i + 1]))) {
            DEBUG("Non-hexadecimal S-link command");
            return;
          }
          String hexByte = String(char(payload[i])) + String(char(payload[i + 1]));
          commandBytes[i / 2] = strtol(hexByte.c_str(), NULL, 16);
        }

        sendCommand(commandBytes, sizeof(commandBytes));
        break;
    }
}

void DEBUG(String msg)
{
  if (webSocket.clientIsConnected(debugClient)) {
    webSocket.sendTXT(debugClient, msg);
  }
}

// This interrupt handler receives data from a remote slink device
IRAM_ATTR void busChange()
{
  unsigned long timeNow = micros();

  int busState = digitalRead(INPUT_PIN);
  if (busState == LOW) {
    timeLowTransition = timeNow;
    return;
  }

  // Bus is high. The time that the bus has been low determines what
  // has happened. Let's store this information for analysis outside
  // of the interrupt handler.
  int timeLow = timeNow - timeLowTransition;

  if ((bufferWritePosition + 1) % PULSE_BUFFER_SIZE == bufferReadPosition) {
    bufferOverflowDetected = true;
    return;
  }

  // Divide by 10 to make the pulse length fit in 8 bits
  pulseBuffer[bufferWritePosition] = min(255, timeLow / 10);
  bufferWritePosition = (bufferWritePosition + 1) % PULSE_BUFFER_SIZE;
}

void processSlinkInput()
{
  static byte currentByte = 0;
  static byte currentBit = 0;
  static String bytesReceivedHex;

  bool completeMessageReceived = false;  
  while (bufferReadPosition != bufferWritePosition) {
    int timeLow = pulseBuffer[bufferReadPosition] * 10;

    bufferReadPosition = (bufferReadPosition + 1) % PULSE_BUFFER_SIZE;

    if (timeLow > 2000) {
      // 2400 us -> new data sequence
      completeMessageReceived = true;
      break;
    }

    if (!pulseLengths.isEmpty()) {
      pulseLengths += " ";
    }
    pulseLengths += String(timeLow, DEC);

    currentBit += 1;
    if (timeLow > 900) {
      // 1200 us -> bit == 1
      bitSet(currentByte, 8 - currentBit);
    }
    else {
      // 600 us -> bit == 0
      bitClear(currentByte, 8 - currentBit);
    }

    if (currentBit == 8) {
      if (currentByte <= 0xF) {
        bytesReceivedHex += "0" + String(currentByte, HEX);
      }
      else {
        bytesReceivedHex += String(currentByte, HEX);
      }

      currentBit = 0;
    }
  }

  completeMessageReceived |= isBusIdle();
  if (completeMessageReceived && (!bytesReceivedHex.isEmpty() || currentBit != 0)) {
    if (!bytesReceivedHex.isEmpty()) {
      webSocket.broadcastTXT(bytesReceivedHex);
    }

    DEBUG("Pulses (us): " + pulseLengths);
    if (currentBit != 0) {
      DEBUG("Received " + String(currentBit) + " stray bits");
    }
    DEBUG("Buffer overflow detected: " + String(bufferOverflowDetected? "yes":"no"));

    bytesReceivedHex = String();
    currentByte = 0;
    currentBit = 0;
    pulseLengths = String();
  }
}

bool isBusIdle()
{
  noInterrupts();
  bool isBusIdle = micros() - timeLowTransition > 1200 + 600 + 20000;
  interrupts();
  return isBusIdle;
}

void sendPulseDelimiter()
{
  digitalWrite(OUTPUT_PIN, LOW);
  delayMicroseconds(600);
}

void sendSyncPulse()
{
  digitalWrite(OUTPUT_PIN, HIGH);
  delayMicroseconds(2400);  
  sendPulseDelimiter();
}

void sendBit(int bit)
{
  digitalWrite(OUTPUT_PIN, HIGH);
  if (bit) {
    delayMicroseconds(1200);
  }
  else {
    delayMicroseconds(600);
  }
  sendPulseDelimiter();
}

void sendByte(int value)
{
  for (int i = 7; i >= 0; --i) {
    sendBit(bitRead(value, i));
  }
}

void idleAfterCommand()
{
  delay(20); // will yield
}

void sendCommand(byte command[], int commandLength)
{
  do {
    yield(); // Can't yield after this point since precise timing is needed when sending data
  } while (!isBusIdle());

  noInterrupts();
  sendSyncPulse();
  for (int i = 0; i < commandLength; ++i) {
    sendByte(command[i]);
  }
  interrupts();
  idleAfterCommand();
}

void loop()
{
 if (wifiMulti.run() == WL_CONNECTED) {
    processSlinkInput();
    webSocket.loop();
    ArduinoOTA.handle();
  }
}
