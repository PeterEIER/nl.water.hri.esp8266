/*
Sensus 620 High Resolution Interface (HRI)

Count the number of pulses on D7, calculate actual water flow and sum pulses for total water usage per day. Dump data over TCP/IP to your host (domoticz, homey etc).

Factor: 1 l/pulse

Written by Peter EIER

History
12MAR17 - v1.0 - Intial release

*/
#include "ESP8266WiFi.h"
#include <NTPtimeESP.h>          // WeMos ESP8266 Clock

//how many clients should be able to telnet to this ESP8266
#define MAX_SRV_CLIENTS 1
const char* ssid = "ssid";
const char* password = "password";
const int port = 8088;

WiFiServer server(port);
WiFiClient serverClients[MAX_SRV_CLIENTS];

// Sensor data
const int Water_C = 1;            // Pulses per liter (one = 1 puls per liter)

unsigned long curL = 0;           // Calculate current water usage
unsigned long totalL = 0;         // Total amount of water used
unsigned long waterusedL = 0;     // Summed water

unsigned long pulseCountW = 0;    // Pulse counter - total since start
unsigned long pulseCountWD = 0;   // Pulse counter - daily
unsigned long prevMillisW = 0;    // Water start time
unsigned long pulseTimeW = 0;     // Time elapsed for 1 pulse
unsigned long prevMillis = 0;     // Data dump timer
unsigned long prevMillis24hr = 0; // 24hr timer
unsigned long prevMillis1st = 0;  // 24hr timer
unsigned long sec2midnight = 0;   // Used to setup the midnight trigger
int triggeronce = 1;              // Used to setup the midnight trigger

//NTP configuration
NTPtime NTPch("ch.pool.ntp.org");
strDateTime dateTime;
byte actualHour;
byte actualMinute;
byte actualSecond;

void setup() {
  //Send serial data over WiFi
  Serial.begin(115200);
  server.begin();
  server.setNoDelay(true);
  
  Serial.println("Sensus 620 HRI - pulse sensor"); 
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println("Booted");
  Serial.println("Connecting to Wi-Fi");

  WiFi.mode(WIFI_STA);
  WiFi.begin (ssid,password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("WiFi connected");

  //Attach interrupt to our handler
  attachInterrupt(digitalPinToInterrupt(13), checkPin, FALLING); // trigger on rising edge (pulse goes from low to high)

  //setup midnight trigger
  NTP();
  sec2midnight = (86400 - ((actualHour * 3600) + (actualMinute * 60) + actualSecond ));
  Serial.print("Current time: ");
  Serial.print(actualHour);
  Serial.print(":");
  Serial.print(actualMinute);
  Serial.print(":");
  Serial.println(actualSecond);
  Serial.print("Seconds to midnight after boot: ");
  Serial.println(sec2midnight);
}

//Interrupt routine
void checkPin() {
  //do water consumption per minute calculation
  pulseTimeW = (millis() - prevMillisW);
  prevMillisW = millis();
  curL = ( 60000 / pulseTimeW ) * Water_C;        // l/min (we need a second pulse to calculate flow...)
  if (pulseTimeW == (millis())) { (curL = 3); };  // so when their is a water pulse we assume a default flow (3 L/min)

  //consumption calculation
  pulseCountWD++;
  totalL = (pulseCountWD * Water_C);               // daily count
  
  pulseCountW++;
  waterusedL = (pulseCountW * Water_C);            // total count
  return;
}

//ESP8266 Clock
void NTP() {
  yield();
  dateTime = NTPch.getNTPtime(1, 1);
  actualHour   = dateTime.hour;
  actualMinute = dateTime.minute;
  actualSecond = dateTime.second;
  return;
}

void loop() {
  
  uint8_t i;
  //check if there are any new clients
  if (server.hasClient()){
    for(i = 0; i < MAX_SRV_CLIENTS; i++){
      //find free/disconnected spot
      if (!serverClients[i] || !serverClients[i].connected()){
        if(serverClients[i]) serverClients[i].stop();
        serverClients[i] = server.available();
        Serial.print("New client: "); Serial.println(i);
        continue;
      }
    }
    //no free/disconnected spot so reject
    WiFiClient serverClient = server.available();
    serverClient.stop();
  }

  //Synchronize midnight trigger ones (based on current time of the day)
  if (triggeronce == 1) {
    if (((millis() - prevMillis1st)) >= (sec2midnight*1000)) {
      triggeronce = 0;
      
      sec2midnight = 86400;          //seconds in one day
      prevMillis24hr = millis();     //midnight, we start counting once more
      totalL = 0 ;                   //reset daily water usage
      pulseCountWD = 0;              //restart daily pulse count
    }
  }

  //Note: water usage is calculated in interrupt routine checkPin.

  //We reset the water flow rate when no pulse were received for at least 30 seconds (flow equals 2L/min)
  if ((millis() - prevMillisW) >= 30000) {
    prevMillisW = millis(); 
    curL = 0;                        //water flow
  }

  //reset daily water usage counter at midnight every night...
  if (triggeronce == 0) {
    if (((millis() - prevMillis24hr)) >= (sec2midnight*1000)) {
    prevMillis24hr = millis();
    totalL = 0;                      //reset daily water usage
    pulseCountWD = 0;                //restart daily pulse count
    }
  }
  
  //Dump data every 5 seconds
  if ((millis() - prevMillis) >= 5000) {
    prevMillis = millis();
    
    Serial.print(curL);
    Serial.println(" L/m");
    Serial.print(totalL);
    Serial.println(" Liter");
    Serial.println();

    String line1;String line2;String line3;String line4;String line5;String line6;String line7;String line8;String line9;String line10;String line11;
    line1 = "/HRI6\\Sensus-620\r\n\r\n";
    line2 = "DATA:1(";
    line3 = curL;
    line4 = "*Liter)\r\n";
    line5 = "DATA:2(";
    line6 = totalL;
    line7 = "*Liter)\r\n";
    line8 = "DATA:3(";
    line9 = waterusedL;
    line10 = "*Liter)\r\n";   
    line11 = "!\r\n";

    String msg;
    msg = line1 + line2 + line3 + line4 + line5 + line6 + line7 + line8 + line9 + line10 + line11;
    //Serial.println(msg);
    
    //Push data to all connected telnet clients
    for(i = 0; i < MAX_SRV_CLIENTS; i++){
      if (serverClients[i] && serverClients[i].connected()){
        char msgBuf[msg.length()];
        msg.toCharArray(msgBuf,(msg.length()+1));
        serverClients[i].write(msgBuf, msg.length());
      }
    }
  }
}

