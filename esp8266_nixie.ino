#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <SPI.h>
#include <TimeLib.h>
#include <Wire.h>
#include <NtpClientLib.h>
#include <Timer.h>
Timer updateTime;
Timer syncFromRTC;
Timer doSlotMachine;
Timer showDate;
Timer queryNtp;

// tested on a WEMOS D1 (retired model)
//
// PIN Layout Nixie Shield <-----> Wemos D1 ESP 8266
//                     GND <-----> GND
//                     VIN <-----> VIN
//                     SCK <-----> D13/SCK/D5
//                     MOSI<-----> D11/MOSI/D7
//                     LE  <-----> D8
//                     SDA <-----> D14/SDA
//                     SCL <-----> D15/SCL
// 
// Not all functions of the shield (e.g, RBG LEDs, buzzer) are connected

char ssid[] = "YourSSID";  //  your network SSID (name)
char pass[] = "YourPassword";       // your network password
const char* ntpServerName = "pool.ntp.org"; // ntp server
int8_t timeZone = 1;  // GMT offset [h]
int8_t minutesTimeZone = 0; // GMT offset [min]

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


int updateTimerId;

#define UpperDotsMask 0x80000000
#define LowerDotsMask 0x40000000


boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpEvent; // Last triggered event

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

unsigned long epoch;

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
  if (minute() % 2) {
    stringToDisplay = PreZero(day()) + PreZero(month()) + (year()-2000);
  } else {
    stringToDisplay = PreZero(hour()) + PreZero(minute()) + PreZero(second());
  }
  doIndication();
}

void slotMachine()
{
  for (int c = 0; c < 3; c++) {
    for (int i = 0; i < 10; i++) {
      stringToDisplay = "";
      for (int j = 0; j < 6; j++) {
        stringToDisplay.concat(i);
      }
      doIndication();
      delay(30);
    }
  }
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

  slotMachine();

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

  NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
    ntpEvent = event;
    syncEventTriggered = true;

  });

  NTP.begin("pool.ntp.org", timeZone, true, minutesTimeZone);
  NTP.setInterval(10);  

  updateTimerId = updateTime.every(1000, funcUpdateTime);
  if (RTC_present == true)
    syncFromRTC.every(60000, funcSyncFromRTC);


  //  ntpRequestId =
  queryNtp.every(1000, queryNTPServer);


  doSlotMachine.every(1800000, slotMachine);
}

void processSyncEvent (NTPSyncEvent_t ntpEvent) {
  if (ntpEvent) {
    Serial.print ("Time Sync error: ");
    if (ntpEvent == noResponse)
      Serial.println ("NTP server not reachable");
    else if (ntpEvent == invalidAddress)
      Serial.println ("Invalid NTP server address");
  } else {
        funcNtpToRtc();

    Serial.print ("Got NTP time: ");
    Serial.println (NTP.getTimeDateString (NTP.getLastNTPSync ()));
  }
}

void queryNTPServer()
{

  if (syncEventTriggered) {
    processSyncEvent (ntpEvent);
    syncEventTriggered = false;
  }
}

void funcSyncFromRTC()
{
  getRTCTime();
  setTime(RTC_hours, RTC_minutes, RTC_seconds, RTC_day, RTC_month, RTC_year);
 /* Serial.println(F("RTC Sync"));
  Serial.println(RTC_hours);
  Serial.println(RTC_minutes);
  Serial.println(RTC_seconds);
  Serial.println(RTC_day);
  Serial.println(RTC_month);
  Serial.println(RTC_year);*/
}

void funcNtpToRtc()
{
  Serial.println(F("NTP SYNC"));

  Serial.print (NTP.getTimeDateString ()); Serial.print (" ");
  Serial.print (NTP.isSummerTime () ? "Summer Time. " : "Winter Time. ");
  Serial.print ("WiFi is ");
  Serial.print (WiFi.isConnected () ? "connected" : "not connected"); Serial.print (". ");
  Serial.print ("Uptime: ");
  Serial.print (NTP.getUptimeString ()); Serial.print (" since ");
  Serial.println (NTP.getTimeDateString (NTP.getFirstSync ()).c_str ());

  byte y = year()-2000;
  setRTCDateTime(hour(), minute(), second(), day(), month(), y, 1);

}




void loop()
{


  ArduinoOTA.handle();

  updateTime.update();
  syncFromRTC.update();
  doSlotMachine.update();
  queryNtp.update();



}


