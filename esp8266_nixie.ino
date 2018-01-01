#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <SPI.h>
#include <TimeLib.h>
#include <Wire.h>


char ssid[] = "YourSSID";  //  your network SSID (name)
char pass[] = "YourPassword";       // your network password


unsigned int localPort = 2390;      // local port to listen for UDP packets

/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

unsigned long epoch;

const byte LEpin = 0; //pin Latch Enabled data accepted while HI level
//const byte HIZpin = 0; //8 pin Z state in registers outputs (while LOW level)
const byte DHVpin = 14; // 5 off/on MAX1771 Driver  Hight Voltage(DHV) 110-220V
unsigned int SymbolArray[10] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
const unsigned int fpsLimit = 16666;
String stringToDisplay = "666666";
boolean UD, LD;
bool BlinkUp = false;
bool BlinkDown = false;
int blinkMask = B00000000; //bit mask for blinkin digits (1 - blink, 0 - constant light)
byte dotPattern = B00000000; //bit mask for separeting dots
//B00000000 - turn off up and down dots
//B1100000 - turn off all dots

#define DS1307_ADDRESS 0x68
byte zero = 0x00; //workaround for issue #527
int RTC_hours, RTC_minutes, RTC_seconds, RTC_day, RTC_month, RTC_year, RTC_day_of_week;
bool RTC_present;
bool waitForNTPReply = false;
uint32_t timeOffset = 1;
int ntpRequestId;

#define UpperDotsMask 0x80000000
#define LowerDotsMask 0x40000000

#include <Timer.h>
Timer updateTime;
Timer ntpRequest;
Timer ntpReceive;
Timer syncFromRTC;

void doIndication()
{

  static unsigned long lastTimeInterval1Started;
  if ((micros() - lastTimeInterval1Started) < fpsLimit) return;
  //if (menuPosition==TimeIndex) doDotBlink();
  lastTimeInterval1Started = micros();

  unsigned long Var32 = 0;

  long digits = stringToDisplay.toInt();
  //long digits=12345678;
  //Serial.print("strtoD=");
  //Serial.println(stringToDisplay);

  /**********************************************************
     Подготавливаем данные по 32бита 3 раза
     Формат данных [H1][H2}[M1][M2][S1][Y1][Y2]
   *********************************************************/

  digitalWrite(LEpin, LOW);
  //-------- REG 2 -----------------------------------------------
  /*Var32|=(unsigned long)(SymbolArray[digits%10]&doEditBlink(7))<<10; // Y2
    digits=digits/10;

    Var32 |= (unsigned long)SymbolArray[digits%10]&doEditBlink(6);//y1
    digits=digits/10;

    if (LD) Var32&=~LowerDotsMask;
    else  Var32|=LowerDotsMask;

    if (UD) Var32&=~UpperDotsMask;
    else  Var32|=UpperDotsMask;

    SPI.transfer(Var32>>24);
    SPI.transfer(Var32>>16);
    SPI.transfer(Var32>>8);
    SPI.transfer(Var32);
    //-------------------------------------------------------------------------
  */
  //-------- REG 1 -----------------------------------------------
  Var32 = 0;

  Var32 |= (unsigned long)(SymbolArray[digits % 10] & doEditBlink(5)) << 20; // s2
  digits = digits / 10;

  Var32 |= (unsigned long)(SymbolArray[digits % 10] & doEditBlink(4)) << 10; //s1
  digits = digits / 10;

  Var32 |= (unsigned long) (SymbolArray[digits % 10] & doEditBlink(3)); //m2
  digits = digits / 10;

  if (LD) Var32 &= ~LowerDotsMask;
  else  Var32 |= LowerDotsMask;

  if (UD) Var32 &= ~UpperDotsMask;
  else  Var32 |= UpperDotsMask;

  SPI.transfer(Var32 >> 24);
  SPI.transfer(Var32 >> 16);
  SPI.transfer(Var32 >> 8);
  SPI.transfer(Var32);
  //-------------------------------------------------------------------------

  //-------- REG 0 -----------------------------------------------
  Var32 = 0;

  Var32 |= (unsigned long)(SymbolArray[digits % 10] & doEditBlink(2)) << 20; // m1
  digits = digits / 10;

  Var32 |= (unsigned long)(SymbolArray[digits % 10] & doEditBlink(1)) << 10; //h2
  digits = digits / 10;

  Var32 |= (unsigned long)SymbolArray[digits % 10] & doEditBlink(0); //h1
  digits = digits / 10;

  if (LD) Var32 &= ~LowerDotsMask;
  else  Var32 |= LowerDotsMask;

  if (UD) Var32 &= ~UpperDotsMask;
  else  Var32 |= UpperDotsMask;

  SPI.transfer(Var32 >> 24);
  SPI.transfer(Var32 >> 16);
  SPI.transfer(Var32 >> 8);
  SPI.transfer(Var32);

  digitalWrite(LEpin, HIGH);
  //-------------------------------------------------------------------------
}

word doEditBlink(int pos)
{
  /*
    if (!BlinkUp) return 0;
    if (!BlinkDown) return 0;
  */

  if (!BlinkUp) return 0xFFFF;
  if (!BlinkDown) return 0xFFFF;
  //if (pos==5) return 0xFFFF; //need to be deleted for testing purpose only!
  int lowBit = blinkMask >> pos;
  lowBit = lowBit & B00000001;

  static unsigned long lastTimeEditBlink = millis();
  static bool blinkState = false;
  word mask = 0xFFFF;
  static int tmp = 0; //blinkMask;
  if ((millis() - lastTimeEditBlink) > 300)
  {
    lastTimeEditBlink = millis();
    blinkState = !blinkState;
    if (blinkState) tmp = 0;
    else tmp = blinkMask;
  }
  if (((dotPattern & ~tmp) >> 6) & 1 == 1) LD = true; //digitalWrite(pinLowerDots, HIGH);
  else LD = false; //digitalWrite(pinLowerDots, LOW);
  if (((dotPattern & ~tmp) >> 7) & 1 == 1) UD = true; //digitalWrite(pinUpperDots, HIGH);
  else UD = false; //digitalWrite(pinUpperDots, LOW);

  if ((blinkState == true) && (lowBit == 1)) mask = 0x3C00; //mask=B11111111;
  //Serial.print("doeditblinkMask=");
  //Serial.println(mask, BIN);
  return mask;
}


void setRTCDateTime(byte h, byte m, byte s, byte d, byte mon, byte y, byte w)
{
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(zero); //stop Oscillator

  Wire.write(decToBcd(s));
  Wire.write(decToBcd(m));
  Wire.write(decToBcd(h));
  Wire.write(decToBcd(w));
  Wire.write(decToBcd(d));
  Wire.write(decToBcd(mon));
  Wire.write(decToBcd(y));

  Wire.write(zero); //start

  Wire.endTransmission();

}

String PreZero(int digit)
{
  digit = abs(digit);
  if (digit < 10) return String("0") + String(digit);
  else return String(digit);
}

byte decToBcd(byte val) {
  // Convert normal decimal numbers to binary coded decimal
  return ( (val / 10 * 16) + (val % 10) );
}

byte bcdToDec(byte val)  {
  // Convert binary coded decimal to normal decimal numbers
  return ( (val / 16 * 10) + (val % 16) );
}

void getRTCTime()
{
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(zero);
  Wire.endTransmission();

  Wire.requestFrom(DS1307_ADDRESS, 7);

  RTC_seconds = bcdToDec(Wire.read());
  RTC_minutes = bcdToDec(Wire.read());
  RTC_hours = bcdToDec(Wire.read() & 0b111111); //24 hour time
  RTC_day_of_week = bcdToDec(Wire.read()); //0-6 -> sunday - Saturday
  RTC_day = bcdToDec(Wire.read());
  RTC_month = bcdToDec(Wire.read());
  RTC_year = bcdToDec(Wire.read());
}

void funcUpdateTime()
{
  stringToDisplay = PreZero(hour() + timeOffset) + PreZero(minute()) + PreZero(second());
  doIndication();
}

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("nixieHorst");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");


  //  ArduinoOTA.onStart([]() {
  //    Serial.println("Start");
  //  });
  //  ArduinoOTA.onEnd([]() {
  //    Serial.println("\nEnd");
  //  });
  //  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  //    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  //  });
  //  ArduinoOTA.onError([](ota_error_t error) {
  //    Serial.printf("Error[%u]: ", error);
  //    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
  //    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
  //    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
  //    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
  //    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  //  });
  ArduinoOTA.begin();

  SPI.begin(); //
  SPI.setDataMode (SPI_MODE3); // Mode 2 SPI but Mode 2 & 3 are Swapped on ESP8266
  SPI.setClockDivider(SPI_CLOCK_DIV8); // SCK = 16MHz/128= 125kHz

  Wire.begin();

  getRTCTime();
  byte prevSeconds = RTC_seconds;
  unsigned long RTC_ReadingStartTime = millis();
  RTC_present = true;
  while (prevSeconds == RTC_seconds)
  {
    getRTCTime();
    //Serial.println(RTC_seconds);
    if ((millis() - RTC_ReadingStartTime) > 3000)
    {
      Serial.println(F("Warning! RTC DON'T RESPOND!"));
      RTC_present = false;
      break;
    }
  }

  queryNTPServer();

  updateTime.every(1000, funcUpdateTime);
  if (RTC_present == true)
    syncFromRTC.every(60000, funcSyncFromRTC);


  ntpRequestId = ntpRequest.every(60000, queryNTPServer);
}

void queryNTPServer()
{
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  waitForNTPReply = true;
  ntpRequest.every(1000, getNTPAnswer, 10);
}

void funcSyncFromRTC()
{
  getRTCTime();
  setTime(RTC_hours, RTC_minutes, RTC_seconds, RTC_day, RTC_month, RTC_year);
  Serial.println(F("RTC Sync"));
}

void getNTPAnswer()
{
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    //    uint8_t hour = (epoch  % 86400L) / 3600;
    //    uint8_t minute = (epoch % 3600) / 60;
    //    uint8_t second = epoch % 60;
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second
    // wait ten seconds before asking for the time again
    uint8_t hour = (epoch  % 86400L) / 3600;
    uint8_t minute = (epoch % 3600) / 60;
    uint8_t second = epoch % 60;
    setRTCDateTime(hour, minute, second, RTC_day, RTC_month, RTC_year, 1);
    setTime(hour, minute, second, RTC_day, RTC_month, RTC_year);
    //? set to RTC?
    waitForNTPReply = false;
    
    Serial.println(F("NTP Sync"));

    ntpReceive.stop(ntpRequestId);
  }

}

void loop()
{
  ArduinoOTA.handle();

  updateTime.update();
  syncFromRTC.update();
  ntpRequest.update();
  if (waitForNTPReply == true)
    ntpReceive.update();

}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

