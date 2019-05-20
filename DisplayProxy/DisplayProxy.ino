/*

  Solar controller remote display.  Based on example "TFT_meters"
*/

uint32_t tftTimer = 0;       // time for next update
int backLight = 1023;
#define TFT_LOOP_PERIOD 50        // Display updates every 50ms
const int VERT_METER_COUNT = 3;
int value[VERT_METER_COUNT] = {0, 0, 0};    // MAIN, SOLAR and PANEL
int old_value[VERT_METER_COUNT] = { -1, -1, -1};
//=========================================================================
// WiFi
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "credentials.h"
//const char* ssid     = "xxxx";
//const char* password = "xxxx";
//=========================================================================
// OTA
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//=========================================================================
// Bluetooth / UNO controller
const int SERIAL_BUFF_SIZE = 256;
const uint32_t UNO_DATA_PERIOD  = 300000;     // Data updates from UNO every 5 minutes
uint32_t unoTimer = 0;
byte solarOn = 0;
byte wetbackOn = 0;
byte boostState = 0;
byte hw = 0;   // Estimated percentage of HW
byte unoError = 0;
const byte NEON_CHAN_LEN = 8;      // No channels should be > than this length 'xxx.xx\0'
const byte NEON_CHAN_COUNT = 8;
char mNeonData[NEON_CHAN_COUNT][NEON_CHAN_LEN];
//================================================================
// Neon REST Service
#include "RestClient.h"
#include <ArduinoJson.h>
#include <EEPROM.h>
const char* proxyAddr = "192.168.1.130";
int proxyPort = 9000;
const char* neonURL = "restservice-neon.niwa.co.nz";
//Included in "credentials.h" which is not included in the GIT repository
//const char* neonUser = "xxxxxx";
//const char* neonPassword = "xxxxx";
const char* contentType = "application/json";
const char* importDataPath = "/NeonRESTService.svc/ImportData/4063?LoggerType=1";
const char* getSessionPath = "/NeonRESTService.svc/PostSession";

//================================================================
// NTP time synch
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ESP8266WiFi.h>
static const char ntpServerName[] = "time.nist.gov";
const int timeZone = 0;     // UTC
const unsigned int localPort = 8888;  // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
time_t prevDisplay = 0; // when the digital clock was displayed

const uint32_t CLOCK_UPDATE_RATE = 1000;
uint32_t clockTimer = 0;       // time for next clock update
//================================================================
// Remote debugging
#include "RemoteDebug.h"
RemoteDebug Debug;
const uint32_t MIN_HEAP = 10000;    // Reboot if heap drops to less than 10k
//================================================================
// Push button interrupts
const byte boostInterruptPin = D1;
volatile byte boostCount = 0;
const byte pageInterruptPin = D2; //????
int page = 0;
bool refreshPage = true;
const int MAX_PAGES = 2;
//================================================================


void setup(void) {

  Serial.begin(9600); // Bluetooth module

  initTFT();
  analogWrite(D0, backLight);

  //================================================
  // Init WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  char constat[] = "Connecting1234567890";
  int pntr = 10;
  while (WiFi.status() != WL_CONNECTED) {
    constat[pntr] = '.';
    constat[pntr + 1] = '\0';
    pntr++;
    if (pntr >= 19)
    {
      pntr = 10;
    }
    showConnStatus(constat);
    delay(250);
  }
  showSSID(ssid);
  //================================================
  // Init remote debugging
  Debug.begin("Telnet_SolarDisplay");
  Debug.setResetCmdEnabled(true);

  //================================================
  // Init OTA
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("solar_display");

  ArduinoOTA.onStart([]() {
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    showReceiveProgram();
  });
  ArduinoOTA.onEnd([]() {
    //
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //
  });
  ArduinoOTA.onError([](ota_error_t error) {
    showOTAError(error);
  });
  ArduinoOTA.begin();
  //================================================
  // Init NTP and time
  setSyncProvider(getNtpTime);
  setSyncInterval(3600);
  //================================================
  // Push button interrupts
  pinMode(boostInterruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(boostInterruptPin), boostInterrupt, FALLING);
  pinMode(pageInterruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pageInterruptPin), pageInterrupt, FALLING);
}

void loop() {

  if (tftTimer <= millis()) {
    tftTimer = millis() + TFT_LOOP_PERIOD;

    // Update the clock display
    if (clockTimer < millis()) {
      clockTimer = millis() + CLOCK_UPDATE_RATE;
      setStatusLabel(getTime(12), false);
    }

    if (unoTimer < millis())
    {
      if (boostCount > 0)
      {
        // Refresh now
        drawStatusIcons();
      }

      uint32_t heapFree = ESP.getFreeHeap();
      itoa(heapFree, mNeonData[7], 10);
      if (heapFree < MIN_HEAP)
      {
        ESP.restart();
      }

      setStatusLabel("SYNC...", true);
      DEBUG("#Starting interrogation at: %s\n", getISO8601Time(false));
      char data[SERIAL_BUFF_SIZE];
      if (getSolarDataFromUNO(data))
      {
        if (checkJSON(data))
        {
          parseJSON(data);
          pushToNeon();
          unoError = 0;
        }
        else
        {
          unoError = 1;
        }
      }
      else
      {
        unoError = 1;
      }

      drawStatusIcons();
      unoTimer = getNextUNOUpdateTime();
      setStatusLabel("", true);
    }

    // Currently in update TFT fast loop
    plotPointer();
    plotNeedle(hw, 0);
    setBacklight();
  }
  Debug.handle();
  ArduinoOTA.handle();
  yield();
}



int getNextUNOUpdateTime()
{
  int secHr = minute() * 60000 + second() * 1000;     // milliseconds into the hour
  int rem = secHr % UNO_DATA_PERIOD;
  int offset = UNO_DATA_PERIOD - rem;
  DEBUG("\r#Sync in %d seconds\n", offset / 1000);
  return millis() + offset;
}

bool getSolarDataFromUNO(char* buff)
{
  for (int retry = 0; retry < 2; retry++)
  {
    if (startCommandMode())
    {
      if (connect())
      {
        if (boostCount > 0)
        {
          toggleBoost();
        }
        readUNO(buff);
        startCommandMode();
        hangUp();
        return true;
      }
      else
      {
        reboot();
      }
    }
  }
  return false;
}

boolean startCommandMode()
{
  char * response;
  response = sendCommand("\r", 0, 0, 1000);
  if (strstr(response, "?"))
  {
    DEBUG("#Already in command mode\n");
    return true;
  }
  for (int retry = 0; retry < 3; retry++)
  {
    delay(1000);
    response = sendCommand("$$$", 0, 0, 1000);
    if (strstr(response, "CMD"))
    {
      return true;
    }
    delay(1000);
  }
  DEBUG("#Unable to start command mode\n");
  return false;
}

boolean connect()
{
  char * response;
  for (int retry = 0; retry < 3; retry++)
  {
    response = sendCommand("C,201512083167\r", 2000, 15000, 1000);

    if (strstr(response, "%CONNECT"))
    {
      return true;
    }
    else if (strstr(response, "ERR-connected"))
    {
      delay(1000);
      response = sendCommand("---\r", 0, 0, 1000);
      if (strstr(response, "END"))
      {
        return true;
      }
    }
    else if (strstr(response, "CONNECT failed"))
    {
      return false;
    }
    else
    {
      DEBUG("#No answer - assume connected\n");
      return true;
    }
  }
  return false;
}

void reboot()
{
  sendCommand("R,1\r", 0, 0, 1000);
  delay(5000);
}

void startInquiry()
{
  sendCommand("IQ\r", 10000, 0, 5000);
}

boolean readUNO(char* buff)
{
  char * response;
  for (int retry = 0; retry < 3; retry++)
  {
    response = sendCommand("R", 0, 0, 1000);
    if (strlen(response) > 0)
    {
      // Make a copy of the response or it will be overwritten on
      // the next command
      strcpy(buff, response);
      return true;
    }
  }
  return false;
}

boolean hangUp()
{
  sendCommand("K,1\r", 0, 0, 1000);
}

void toggleBoost()
{
  sendCommand("B", 0, 0, 1000);
  boostCount = 0;
}

//=============================================================
// Send a command via Serial, wait for "pause" for
// intial response to come through, and then after pause,
// exit when there's been more than "timeout" with no data
//=============================================================
char* sendCommand(char* cmd, long gate, long wait, long timeout)
{
  static char buff[SERIAL_BUFF_SIZE];
  memset(buff, 0, SERIAL_BUFF_SIZE);
  int pnt = 0;;
  char c;
  // Flush the input buffer
  while (Serial.available() > 0) {
    c = Serial.read();
  }
  Serial.print(cmd);
  pnt = 0;
  DEBUG("SEND: %s\n", cmd);
  long endTime;
  // Wait "gate" ms before testing for timeout condition
  if (gate > 0)
  {
    endTime = millis() + gate;
    while (endTime > millis())
    {
      while (Serial.available() > 0)
      {
        buff[pnt] = Serial.read();
        if (Debug.isActive(Debug.ANY))
        {
          Debug.write(buff[pnt]);
        }
        pnt++;
        if (pnt > 255) {
          buff[255] = 0;
          return buff;
        }
      }
      Debug.handle();
      yield();
    }
  }
  if (wait == 0) {
    wait = timeout;
  }
  endTime = millis() + wait;
  while (endTime > millis())
  {
    while (Serial.available() > 0)
    {
      buff[pnt] = Serial.read();
      if (Debug.isActive(Debug.ANY))
      {
        Debug.write(buff[pnt]);
      }
      pnt++;
      if (pnt > 255) {
        buff[255] = 0;
        return buff;
      }
      endTime = millis() + timeout;
    }
    Debug.handle();
    yield();
  }
  buff[pnt] = 0;  // Terminate string
  return buff;
}

bool checkJSON(char* json)
{
  // Sanity check results.
  // - First char should be "{"
  // - Absolute min size = 176 bytes
  // - Max size = 252 bytes (5 char for 15 values plus 16 char for relay state)
  char* ch;
  ch = strchr(json, '\0');
  if (ch == json)
  {
    DEBUG("#Null response\n");
    return false;
  }

  ch = strchr(json, '{');
  if (ch != json)
  {
    DEBUG("#Response doesn't start with {\n");
    return false;
  }

  ch = strchr(json, '}');
  int idx = ch - json;
  if (idx < 176 || idx > 252)
  {
    DEBUG("#Response end not in expected range: end_index= %d\n", idx);
    return false;
  }
  // Ensure response is null terminated (in case of multiple responses)
  json[idx + 1] = 0;
  return true;
}

void parseJSON(char* json)
{
  //const size_t bufferSize = JSON_OBJECT_SIZE(16) + 1024;
  //const size_t bufferSize = JSON_OBJECT_SIZE(16) + 140;   // From ArduinoHelper
  //StaticJsonBuffer<bufferSize> jsonBuffer;
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(json);

  // Make a copy of these to push to Neon
  strcpy(mNeonData[0], root["PA"]);
  strcpy(mNeonData[1], root["HI"]);
  strcpy(mNeonData[2], root["CO"]);
  strcpy(mNeonData[3], root["WB"]);
  strcpy(mNeonData[4], root["MC"]);
  strcpy(mNeonData[5], root["SC"]);
  float heat = strtof(root["HA"], NULL) * 0.000177;
  char heatStr[10];
  dtostrf(heat , 1, 2, heatStr);
  DEBUG("#Heat accumulation %s\n", heatStr);
  strcpy(mNeonData[6], heatStr);

  value[0] = atoi(root["MC"]);
  value[1] = atoi(root["SC"]);
  value[2] = atoi(root["PA"]);

  solarOn = atoi(root["SO"]);
  wetbackOn = atoi(root["WO"]);
  boostState = atoi(root["BS"]);

  // Calculate an effective % of water available.
  // We should scale
  // <30degC = 0%
  // >70degC = 100%
  // main cylinder = 180L
  // solar cylinder = 135l
  int hwc_pct = ((value[0] - 30) * 100) / 40;
  if (hwc_pct < 0) {
    hwc_pct = 0;
  }

  int swc_pct = ((value[1] - 30) * 100) / 40;
  if (swc_pct < 0) {
    swc_pct = 0;
  }

  hw = (57 * hwc_pct + 43 * swc_pct) / 100;
}

//=============================================================
// Automatically set the backlight based on the current
// light level
//=============================================================
void setBacklight()
{
  // Fully dark A0 ~=  600
  // Light  A0 == 0

  int lightLevel = analogRead(A0);
  lightLevel = (lightLevel * -1.706) + 1024;
  int diff = (backLight - lightLevel) / 2;
  diff = abs(diff);
  if (diff == 0) {
    diff = 1;
  }
  if (backLight > lightLevel)
  {
    backLight -= diff;
  }
  else if (backLight < lightLevel)
  {
    backLight += diff;
  }
  if (backLight < 2) {
    backLight = 2;
  }
  if (backLight > 1023) {
    backLight = 1023;
  }
  analogWrite(D0, backLight);
}

//==============================================================================
// NTP Methods
//==============================================================================
time_t getNtpTime()
{
  WiFiUDP Udp;
  Udp.begin(localPort);
  IPAddress ntpServerIP; // NTP server's ip address
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP, Udp);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address, WiFiUDP &Udp )
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

char* getTime(int offset)
{
  static char timeString[9];

  char buff[3];
  int hr = hour() + offset;
  if (hr >= 24)
  {
    hr -= 24;
  }
  int mi = minute();
  int se = second();

  itoa(hr, buff, 10);
  if (hr < 10)
  {
    strcpy(timeString, "0");
    strcat(timeString, buff);
  }
  else
  {
    strcpy(timeString, buff);
  }
  strcat(timeString, ":");

  itoa(mi, buff, 10);
  if (mi < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, ":");

  itoa(se, buff, 10);
  if (se < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  return timeString;
}

char* getISO8601Time(boolean zeroSeconds)
{
  static char timeString[20];

  char buff[5];
  int yr = year();
  int mo = month();
  int da = day();
  int hr = hour();
  int mi = minute();
  int se;
  if (zeroSeconds)
  {
    se = 0;
  }
  else
  {
    se = second();
  }

  itoa(yr, buff, 10);
  strcpy(timeString, buff);
  strcat(timeString, "-");
  itoa(mo, buff, 10);
  if (mo < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, "-");
  itoa(da, buff, 10);
  if (da < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, "T");
  itoa(hr, buff, 10);
  if (hr < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, ":");
  itoa(mi, buff, 10);
  if (mi < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  strcat(timeString, ":");
  itoa(se, buff, 10);
  if (se < 10)
  {
    strcat(timeString, "0");
  }
  strcat(timeString, buff);
  return timeString;
}

//==============================================================================
// REST Methods
//==============================================================================
int pushToNeon()
{
  RestClient client = RestClient(neonURL, proxyAddr, proxyPort);  
  client.setContentType(contentType);
  char sessionHeader[70];
  int httpStatus = getSessionToken(client, sessionHeader);
  if (httpStatus == 200)
  {
    httpStatus = pushData(client, sessionHeader);
  }
}

int getSessionToken(RestClient &client, char* sessionHeader)
{
  DynamicJsonBuffer sendJsonBuffer;
  JsonObject& cred = sendJsonBuffer.createObject();
  cred["Username"] = neonUser;
  cred["Password"] = neonPassword;
  char json[100];
  cred.printTo(json);
  String response;
  int statusCode = client.post(getSessionPath, json, &response);
  if (statusCode == 200)
  {
    DynamicJsonBuffer recvJsonBuffer;
    JsonObject& root = recvJsonBuffer.parseObject(response);
    strcpy(sessionHeader, "X-Authentication-Token: ");
    strcat(sessionHeader, root.get<String>("Token").c_str());
    client.setHeader(sessionHeader);
  }
  DEBUG("#GetSessionToken status code = %d\n", statusCode);
  return statusCode;
}

int pushData(RestClient &client, char* sessionHeader)
{
  DynamicJsonBuffer jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  JsonArray& Data = root.createNestedArray("Data");

  for (int i = 0; i < NEON_CHAN_COUNT; i++)
  {
    JsonObject& item = Data.createNestedObject();
    item["SensorNumber"] = String(i);
    item["ImportType"] = "0";

    JsonArray& itemSamples = item.createNestedArray("Samples");

    JsonObject& itemSample = itemSamples.createNestedObject();
    itemSample["Time"] = getISO8601Time(true);
    itemSample["Value"] = mNeonData[i];
  }

  char jsonData[1024];
  root.printTo((char*)jsonData, root.measureLength() + 1);

  int statusCode = client.post(importDataPath, (char*)jsonData);
  DEBUG("#ImportData status code = %d\n", statusCode);
  return statusCode;
}

//==============================================================================
// Interrupt callbacks
//==============================================================================

void boostInterrupt() {
  boostCount++;     // Capture the switch
  unoTimer = 0;     // This will force a connection where we can toggle boost
}

void pageInterrupt() {
  page++;
  refreshPage = true;
  if (page >= MAX_PAGES)
  {
    page == 0;
  }
}
