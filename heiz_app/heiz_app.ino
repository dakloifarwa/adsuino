/*
 heiz_app.ino, to be compiled with Arduino 1.6.5
 */

//-------- includes ------------------------------------------------------------------------------->
#include <Time.h>
#include <Wire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <avr/pgmspace.h>
#include <Adafruit_ADS1015.h>
#include "eth_private.h"
#include "pushingbox_ids.h" // your secret DevIDs from PushingBox.com
#include "allnet_ids.h" // contains the Allnet IP socket mapping
//<-------- includes -------------------------------------------------------------------------------

//-------- global variables ----------------------------------------------------------------------->

#ifndef SQUARE
#define SQUARE(X) ((X)*(X))
#endif
#ifndef ABS
#define ABS(X) ((X) > 0 ? (X) : (-(X)))
#endif
#ifndef MAX
#define MAX(X,Y) ((X)>(Y) ? (X):(Y))
#endif
#ifndef MIN
#define MIN(X,Y) ((X)<(Y) ? (X):(Y))
#endif
#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))
#endif

byte mac[] = ETH_PRIVATE_MAC;   // your secret MAC address from eth_private.h

// Set the static IP address to use if the DHCP fails to assign
IPAddress ip      ETH_PRIVATE_IP; // your secret network settings from eth_private.h
IPAddress gateway ETH_PRIVATE_GATEWAY;
IPAddress subnet  ETH_PRIVATE_SUBNET;

#define MAX_ETH_DATA   56

typedef union
{
  byte bBytes[MAX_ETH_DATA];
  struct
  {
    union
    {
      byte bData[MAX_ETH_DATA - sizeof(word)];
      word wData[(MAX_ETH_DATA - sizeof(word)) / sizeof(word)];
      struct
      {
        int16_t swTemperaturWarmWasserLO;
        int16_t swTemperaturHeizungLM;   
        int16_t swTemperaturHeizungLU;   
        int16_t swTemperaturHeizungRO;   
        int16_t swTemperaturHeizungRU;   
        int16_t swTemperaturAussen;      
        int16_t swTemperaturHeizraum;    
        int16_t swTemperaturKesselwasser;
        int16_t swTemperaturAbgase;      
        int16_t swTemperaturAussenMittel;
        int16_t swTemperaturAussenMin;   
        int16_t swTemperaturAussenMax;   
        int16_t swUpTime;                
        int16_t swHolzmenge;             
        int16_t swEinheizZeitpunkt;
        /*word wAbsFeuchteZuluft;
        word wAbsFeuchteAbluft;
        byte bLuefterdrehzahl;
        bool bLuefterEnabled;
        bool bHausbeleuchtungEnabled;
        word wHausbeleuchtungOnTime;*/
        bool bReboot;
        // neue Daten hier einfügen
      };
    };
    word wCRC16; // muss unbedingt an letzter Stelle stehen!
  };
} EthernetDataStruct;
EthernetDataStruct EthernetData;

#define AREF_VOLTAGE 5000L // mV
#define AVR_ADC_RANGE 1024L // ADU
#define LM61_OFFSET 600L // mV
#define LM61_GAIN 10L // mV/K
#define ADC_AVERAGING 1
int sensorPin = A3;    // Heizraumtemperatur

Adafruit_ADS1115 ads;  /* Use this for the 16-bit version */
#define ADS_ADC_RANGE 32767L // ADU
#define PT1000_REFERENZ_WERT 1000000L // mOhm bei 0 Grad Celsius
#define PT1000_TEMP_KOEFFIZIENT 3851L // ppm/K
#define PT1000_SPANNUNGSTEILER_WIDERSTAND 4700L // Ohm fuer maximalen Hub
#define PT1000_SPANNUNGSTEILER_SPANNUNG 5000L //mV
#define ADS_REF_SPANNUNG 4096L // mV
#define ADS_GAIN_2X (2L)
int32_t dwSpannungsTeilerSpannung = PT1000_SPANNUNGSTEILER_SPANNUNG;
#define TEMPERATUR_OFFSET (16) // 1.6 K

// Mittelung der Aussentemperatur:
#define MESSINTERVALL   (15) // Minuten
#define AT_ZEITRAUM           (18) // Zeitraum in Stunden
#define AT_MESSUNG_PRO_H (60 / MESSINTERVALL) // Messungen pro Stunde
#define NR_AT_WERTE (AT_ZEITRAUM * AT_MESSUNG_PRO_H)

#define PUSH_PERIOD_WW  (((3 * 60) / MESSINTERVALL) - 1) // Intervall 3h, umgerechnet in Viertelstunden
#define PUSH_PERIOD_HZ  (((2 * 60) / MESSINTERVALL) - 1) // Intervall 2h, umgerechnet in Viertelstunden
#define PUSH_PERIOD_AT  (((6 * 60) / MESSINTERVALL) - 1) // Intervall 6h, umgerechnet in Viertelstunden

#define WARMWASSERLIMIT       (40*10) // [1/10 ° Celsius]
#define HEIZWASSERLIMIT       (29*10) // [1/10 ° Celsius]
#define HEIZPUMPENLIMIT       (12*10) // [1/10 ° Celsius], Aussentemperatur, unterhalb derer die Pumpe laufen muss

// Pushing Box Parameter
#define RECEIVER_WW           0x01
#define RECEIVER_HZ           0x02
#define RECEIVER_AT           0x03
#define RECEIVER_STATUS       0x04
#define RECEIVER_TM           0x05

#define PUSHINGBOX_SERVERPFAD	"/pushingbox"
#define PUSHINGBOX_SERVERNAME "api.pushingbox.com"

//Konstanten in Programmspeicher, die gebraucht werden um die HTTP-GET Anfrage zusammenzusetzen
const char PUSHINGBOX_WW_STRING[] = {PUSHINGBOX_DEVID_WW"&wwtemp="};
const char PUSHINGBOX_HZ_STRING[] = {PUSHINGBOX_DEVID_HZ"&hztemp="};
const char PUSHINGBOX_ATMEAN_STRING[] = {PUSHINGBOX_DEVID_AT"&atmean="};
const char PUSHINGBOX_HZTIME_STRING[] = {PUSHINGBOX_DEVID_TM"&hztime="};

const char PUSHINGBOX_HOLZMENGE_STRING[] = {"&holzm="};

const char PUSHINGBOX_STATUS_ID_AT_STRING[] = {PUSHINGBOX_DEVID_STATUS"&at="};
const char PUSHINGBOX_STATUS_ATMEAN_STRING[] = {"&atmean="};
const char PUSHINGBOX_STATUS_ATMIN_STRING[] = {"&atmin="};
const char PUSHINGBOX_STATUS_ATMAX_STRING[] = {"&atmax="};
const char PUSHINGBOX_STATUS_WW_STRING[] = {"&wwtemp="};
const char PUSHINGBOX_STATUS_HZ_STRING[] = {"&hztemp="};

char serverName[] = "api.pushingbox.com";
byte serverAllnet[] = {192, 168, 234, 70};
boolean lastConnected = false;                 // State of the connection last time through the main loop
boolean lastConnectedAllnet = false;                 // State of the connection last time through the main loop

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;
EthernetClient allnetclient;

// time.nist.gov NTP server
char timeServer[] = "ptbtime1.ptb.de";
const int timeZone = +1;     // Central European Time
EthernetUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
time_t prevDisplay = 0; // when the digital clock was displayed
bool bSekundenwechsel = false;

int32_t meanbuffer;
int16_t messreihe[NR_AT_WERTE];
unsigned char mrc = 0;
bool bStartup = true;
unsigned char ww_push_timer = 0;
unsigned char at_push_timer = 0;
unsigned char hz_push_timer = 0;
bool bUpdateStatus = false;
bool bWarmwasserWarnung = false;
bool bHeizwasserWarnung = false;
bool bAussentemperaturWarnung = false;
bool bUpdateHeizzeit = false;
bool bSchalteHeizpumpe = false;
bool bAktiviereHeizpumpe = false;

// telnet defaults to port 23
EthernetServer chatserver(23);
boolean alreadyConnected = false; // whether or not the client was connected previously

//<-------- global variables -----------------------------------------------------------------------

//-------- setup code ----------------------------------------------------------------------------->

void setup() {
  Serial.begin(9600);

  for (int i = 0; i < 10*60; i++) // startup delay of 10 minutes for router to come up
  {
    delay(1000);
  }

  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    // try to congifure using IP address instead of DHCP:
    Ethernet.begin(mac, ip, gateway, subnet);
  }
  else {
    Serial.println("Ethernet ready");
    // print the Ethernet board/shield's IP address:
    Serial.print("My IP address: ");
    Serial.println(Ethernet.localIP());
  }
  // give the Ethernet shield a second to initialize:
  delay(1000);
  for (int i = 0; i < MAX_ETH_DATA; i++)
  {
    EthernetData.bBytes[i] = 0;
  }
  // start listening for chat clients
  chatserver.begin();

  Udp.begin(localPort);
  Serial.println("waiting for sync");
  setSyncProvider(getNtpTime);

  // The ADC input range (or gain) can be changed via the following
  // functions, but be careful never to exceed VDD +0.3V max, or to
  // exceed the upper and lower limits if you adjust the input range!
  // Setting these values incorrectly may destroy your ADC!
  //                                                                 ADS1115
  //                                                                 -------
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit =  0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit =  0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit =  0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit =  0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit =  0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit =  0.0078125mV
  ads.begin();
}

//<-------- setup code -----------------------------------------------------------------------------

//-------- loop code ------------------------------------------------------------------------------>

void loop()
{
  if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) { //update the display only if time has changed
      prevDisplay = now();
      bSekundenwechsel = true;
    }
  }

  if (bSekundenwechsel) // Sekundenwechsel
  {
    bSekundenwechsel = false;

    if (second() % 5 == 0)
      readTemperaturWarmwasserLO();
    if (second() % 5 == 1)
      readTemperaturPufferLM();
    if (second() % 5 == 2)
      readSpannungsTeilerSpannung();
    if (second() % 5 == 3)
      readAussenTemperatur(); // Ausentemperatur
    if (second() % 5 == 4)
      read_AIN_CH3(); // Heizraumtemperatur

    if (second() == 0) // Minutenwechsel
     {
      if (minute() == 0) // nur bei Stundenwechsel
       {
        EthernetData.swUpTime++;
       }
      if (minute() == 25) // stuendlich twittern, um jeweils kurz vor halb
       {
        bUpdateStatus = true;
        //Serial.println("Update!");
       }
      if (bStartup) // zuegig mit Daten fuellen
       {
        bStartup = false;
        for (unsigned char i = 0; i < NR_AT_WERTE; i++)
        {
          messreihe[i] = EthernetData.swTemperaturAussen;
        }
        EthernetData.swTemperaturAussenMittel = EthernetData.swTemperaturAussen;
        EthernetData.swTemperaturAussenMin = EthernetData.swTemperaturAussen;
        EthernetData.swTemperaturAussenMax = EthernetData.swTemperaturAussen;
       }
      if (minute() % MESSINTERVALL == 0)
       {
        messreihe[mrc] = EthernetData.swTemperaturAussen;
        meanbuffer = 0;
        EthernetData.swTemperaturAussenMin = +10000;
        EthernetData.swTemperaturAussenMax = -10000;
        for (unsigned char i = 0; i < NR_AT_WERTE; i++)
        {
          meanbuffer += messreihe[i];
          //Serial.println("mr%i:%i", i, messreihe[i]);
          EthernetData.swTemperaturAussenMin = MIN(messreihe[i], EthernetData.swTemperaturAussenMin);
          EthernetData.swTemperaturAussenMax = MAX(messreihe[i], EthernetData.swTemperaturAussenMax);
        }
        meanbuffer /= NR_AT_WERTE;
        EthernetData.swTemperaturAussenMittel = meanbuffer;
        berechneHolzMenge();
        //Serial.println("ATM:%i.%i", EthernetData.swTemperaturAussenMittel/10, EthernetData.swTemperaturAussenMittel%10);
        //Serial.println("ATMin:%i.%i", EthernetData.swTemperaturAussenMin/10, EthernetData.swTemperaturAussenMin%10);
        //Serial.println("ATMax:%i.%i", EthernetData.swTemperaturAussenMax/10, EthernetData.swTemperaturAussenMax%10);
        if (++mrc == NR_AT_WERTE)
        {
          mrc = 0;
        }
        // Warmwasser ueberwachen:
        if (EthernetData.swTemperaturWarmWasserLO < WARMWASSERLIMIT)
        {
          if (ww_push_timer == 0)
          {
            ww_push_timer = PUSH_PERIOD_WW; // alle 3 Stunden benachrichtigen
            bWarmwasserWarnung = true;
            //Serial.println("Warmwasser!");
          }
          else
          {
            ww_push_timer--;
          }
        }
        else
        {
          ww_push_timer = 0;
        } // Warmwasser
#warning Warmwasser-Zeitpunkt auch noch extrapolieren        
        // Heizwasser ueberwachen:
        if ((EthernetData.swTemperaturHeizungLM < HEIZWASSERLIMIT) && (EthernetData.swTemperaturAussenMittel < HEIZPUMPENLIMIT))
         {
          if (hz_push_timer == 0)
          {
            hz_push_timer = PUSH_PERIOD_HZ; // alle 2 Stunden benachrichtigen
            bHeizwasserWarnung = true;
            //Serial.println("Heizwasser!");
          }
          else
          {
            hz_push_timer--;
          }
         }
        else
         {
          hz_push_timer = 0;
         } // Heizwasser
        // Aussentemperatur ueberwachen:
        if (at_push_timer == 0)
        {
         at_push_timer = PUSH_PERIOD_AT; // alle 6 Stunden kontrollieren
         bSchalteHeizpumpe = true;
         if (EthernetData.swTemperaturAussenMittel < HEIZPUMPENLIMIT)
          {
            bAussentemperaturWarnung = true;
            bAktiviereHeizpumpe = true;
            //Serial.println("Aussentemperatur!");
          }
          else
          {
            bAktiviereHeizpumpe = false;
          }
        }
       else
        {
          at_push_timer--;
        } // Aussentemperatur
       } // Messintervall
      if (minute() == 40) // stuendlich pruefen
       {
        if (EthernetData.swTemperaturAussenMittel < HEIZPUMPENLIMIT)
        {
         int16_t time_buffer;
         static int16_t extrapoldata[3];
         //alte Werte sichern:
         extrapoldata[2] = extrapoldata[1]; // 2h
         extrapoldata[1] = extrapoldata[0]; // 1h
         extrapoldata[0] = EthernetData.swTemperaturHeizungLM; // aktuelle Temperatur
        
        // Temperaturabfall ueber zwei Stunden ermitteln und extrapolieren bis zum Einheizen:
        time_buffer = (HEIZWASSERLIMIT - extrapoldata[0]);
        time_buffer /= ((extrapoldata[0] - extrapoldata[2]) / 2);
        
        if (extrapoldata[0] > HEIZWASSERLIMIT)
         {
          EthernetData.swEinheizZeitpunkt = (time_buffer > 0 ? time_buffer : 24);
         }
        else
         {
          EthernetData.swEinheizZeitpunkt = 0;
         }
        bUpdateHeizzeit = true;
       }
      } // Einheiz-Zeitpunkt
    } // Minutenwechsel
  } // Sekundenwechsel

  if (bUpdateStatus)
  {
    sendToPushingBox(RECEIVER_STATUS);
    digitalClockDisplay();
    bUpdateStatus = false;
  }
  else if (bWarmwasserWarnung)
  {
    sendToPushingBox(RECEIVER_WW);
    digitalClockDisplay();
    bWarmwasserWarnung = false;
  }
  else if (bHeizwasserWarnung)
  {
    sendToPushingBox(RECEIVER_HZ);
    digitalClockDisplay();
    bHeizwasserWarnung = false;
  }
  else if (bAussentemperaturWarnung)
  {
    sendToPushingBox(RECEIVER_AT);
    digitalClockDisplay();
    bAussentemperaturWarnung = false;
  }
  else if (bSchalteHeizpumpe)
  {
    activateHeatPump(bAktiviereHeizpumpe);
    digitalClockDisplay();
    bSchalteHeizpumpe = false;
  }
  else if (bUpdateHeizzeit)
  {
    sendToPushingBox(RECEIVER_TM);
    bUpdateHeizzeit = false;
  }

  // the response from PushingBox Server, you should see a "200 OK"
  if (client.available()) {
    char c = client.read();
    Serial.print(c);
  }
  // the response from PushingBox Server, you should see a "200 OK"
  if (allnetclient.available()) {
    char c = allnetclient.read();
    Serial.print(c);
  }

  // wait for a new chatclient:
  EthernetClient chatclient = chatserver.available();

  // when the chatclient sends the first byte, say hello:
  if (chatclient) {
    if (!alreadyConnected) {
      // clead out the input buffer:
      chatclient.flush();
      chatclient.println("Hello, chatclient!");
      alreadyConnected = true;
    }

    if (chatclient.available() > 0) {
      // read the bytes incoming from the chatclient:
      char telcommand = chatclient.read();
      chatclient.println(" ");
      switch (telcommand)
       {
        case 's': bUpdateStatus = true; break;
        case 't': chatclient.print(hour());
                  chatclient.print(":");
                  chatclient.print(minute());
                  chatclient.print(":");
                  chatclient.print(second());
                  chatclient.print(" ");
                  chatclient.print(day());
                  chatclient.print(".");
                  chatclient.print(month());
                  chatclient.print(".");
                  chatclient.print(year());
                  chatclient.println();
                  break;
        case 'v': chatclient.println(dwSpannungsTeilerSpannung);
                  break;
        case '?': chatclient.println("s: twitter status message");
                  chatclient.println("t: local time");
                  chatclient.println("v: Pt1000 voltage");
                  chatclient.println("?: print this help");
                  break;
        default:  break;
       }
    }
  }

  // if there's no net connection, but there was one last time
  // through the loop, then stop the client:
  if (!client.connected() && lastConnected) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
  }
  lastConnected = client.connected();

  if (!allnetclient.connected() && lastConnectedAllnet) {
    Serial.println();
    Serial.println("disconnecting.");
    allnetclient.stop();
  }
  lastConnectedAllnet = allnetclient.connected();
} // main loop

//<-------- loop code ------------------------------------------------------------------------------

//-------- analog code ---------------------------------------------------------------------------->

void readTemperaturWarmwasserLO(void)
{
  ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit =  0.0625mV
  int32_t buffer = ads.readADC_SingleEnded(0);
  if (buffer == ADS_ADC_RANGE)
  {
    Serial.println("WW: n.c.");
    return;
  }
  // Temperatur in 1/10K berechnen:
  int64_t Ua = (ADS_REF_SPANNUNG * buffer) / ADS_ADC_RANGE / ADS_GAIN_2X; // mV
  int64_t Rtemp = (Ua * PT1000_SPANNUNGSTEILER_WIDERSTAND * 1000L) / (dwSpannungsTeilerSpannung - Ua); // mOhm

  Rtemp -= PT1000_REFERENZ_WERT;
  Rtemp *= 10;
  Rtemp /= PT1000_TEMP_KOEFFIZIENT;
  EthernetData.swTemperaturWarmWasserLO = Rtemp;
  //Serial.println("WW:%i.%i", EthernetData.swTemperaturWarmWasserLO/10, EthernetData.swTemperaturWarmWasserLO%10);
}

void readTemperaturPufferLM(void)
{
  ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit =  0.0625mV
  int32_t buffer = ads.readADC_SingleEnded(1);
  if (buffer == ADS_ADC_RANGE)
  {
    Serial.println("HZ: n.c.");
    return;
  }
  // Temperatur in 1/10K berechnen:
  int64_t Ua = (ADS_REF_SPANNUNG * buffer) / ADS_ADC_RANGE / ADS_GAIN_2X; // mV
  int64_t Rtemp = (Ua * PT1000_SPANNUNGSTEILER_WIDERSTAND * 1000L) / (dwSpannungsTeilerSpannung - Ua); // mOhm

  Rtemp -= PT1000_REFERENZ_WERT;
  Rtemp *= 10;
  Rtemp /= PT1000_TEMP_KOEFFIZIENT;
  EthernetData.swTemperaturHeizungLM = Rtemp;
  //Serial.println("HZ:%i.%i", EthernetData.swTemperaturHeizungLM/10, EthernetData.swTemperaturHeizungLM%10);
}

void readSpannungsTeilerSpannung(void)
{
  ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit =  0.1875mV (default)
  int32_t buffer = ads.readADC_SingleEnded(2);
  if (buffer == ADS_ADC_RANGE)
  {
    Serial.println("Vdd: n.c.");
    dwSpannungsTeilerSpannung = PT1000_SPANNUNGSTEILER_SPANNUNG;
    return;
  }
  // Temperatur in 1/10K berechnen:
  dwSpannungsTeilerSpannung = (ADS_REF_SPANNUNG * buffer) / ADS_ADC_RANGE; // mV
  dwSpannungsTeilerSpannung *= 3; // PGA ist 2/3!
  dwSpannungsTeilerSpannung /= 2;
  //Serial.println("Vdd:%i mV", dwSpannungsTeilerSpannung);
  //Serial.println("HR:%i.%i", EthernetData.swTemperaturHeizraum/10, EthernetData.swTemperaturHeizraum%10);
}

void readAussenTemperatur(void)
{
  ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit =  0.0625mV
  int32_t buffer = ads.readADC_SingleEnded(3);
  if (buffer == ADS_ADC_RANGE)
  {
    Serial.println("AT: n.c.");
    return;
  }
  // Temperatur in 1/10K berechnen:
  int64_t Ua = (ADS_REF_SPANNUNG * buffer) / ADS_ADC_RANGE / ADS_GAIN_2X; // mV
  int64_t Rtemp = (Ua * PT1000_SPANNUNGSTEILER_WIDERSTAND * 1000L) / (dwSpannungsTeilerSpannung - Ua); // mOhm

  Rtemp -= PT1000_REFERENZ_WERT;
  Rtemp *= 10;
  Rtemp /= PT1000_TEMP_KOEFFIZIENT;
  EthernetData.swTemperaturAussen = Rtemp;
  EthernetData.swTemperaturAussen -= TEMPERATUR_OFFSET;
  //Serial.println("AT:%i.%i", EthernetData.swTemperaturAussen/10, EthernetData.swTemperaturAussen%10);
}

void read_AIN_CH3(void) // AIN3, LM61
{
 int32_t buffer = analogRead(sensorPin);
 EthernetData.swTemperaturHeizraum = ((((buffer * AREF_VOLTAGE) / (ADC_AVERAGING * AVR_ADC_RANGE)) - LM61_OFFSET) * 10) / LM61_GAIN;
 //Serial.println(EthernetData.swTemperaturHeizraum);
}

void berechneHolzMenge(void)
{
 int8_t aussentemp = EthernetData.swTemperaturAussenMittel / 10;
 if (aussentemp <= 5)
  {
   EthernetData.swHolzmenge = 18;
   return;
  }
 if (aussentemp <= 7 && aussentemp >= 6)
  {
   EthernetData.swHolzmenge = 16;
   return;
  }
 if (aussentemp = 8)
  {
   EthernetData.swHolzmenge = 14;
   return;
  }
 if (aussentemp <= 11 && aussentemp >= 9)
  {
   EthernetData.swHolzmenge = 18;
   return;
  }
 if (aussentemp = 12)
  {
   EthernetData.swHolzmenge = 16;
   return;
  }
 if (aussentemp = 13)
  {
   EthernetData.swHolzmenge = 18;
   return;
  }
 if (aussentemp = 14)
  {
   EthernetData.swHolzmenge = 16;
   return;
  }
 if (aussentemp >= 15)
  {
   EthernetData.swHolzmenge = 16;
   return;
  }
}
//<-------- analog code ----------------------------------------------------------------------------

void activateHeatPump(bool state_on)
{
   sendToAllnet(ALLNET_DEVID_FBH, state_on);
   sendToAllnet(ALLNET_DEVID_HK, state_on);
}


//-------- Allnet code -------------------------------------------------------------------------------

//Function for sending the request to PushingBox
void sendToAllnet(char switch_id, bool state) {
  allnetclient.stop();
  Serial.println("allnet: connecting...");

  if (allnetclient.connect(serverAllnet, 80)) {
    Serial.println("connected, sending request");

    allnetclient.print("GET /xml/jsonswitch.php?id=");

   //Kopiere HTTP-Get Anfrage in den TCP-Buffer
    allnetclient.print(switch_id, DEC);
    allnetclient.print("&set=");
    allnetclient.print(state, DEC);    

    allnetclient.println(" HTTP/1.1");
    allnetclient.print("Host: ");
    allnetclient.println("ALL4076"); // ???
    allnetclient.println("User-Agent: Heizarduino");
    allnetclient.println();
  }
  else {
    Serial.println("allnet: connection failed");
  }
}
//<-------- Allnet code ------------------------------------------------------------------------------

//-------- push code -------------------------------------------------------------------------------

//Function for sending the request to PushingBox
void sendToPushingBox(char push_receiver) {
  client.stop();
  Serial.println("connecting...");

  if (client.connect(serverName, 80)) {
    Serial.println("connected, sending request");

    client.print("GET /pushingbox?devid=");

    switch (push_receiver) //Kopiere HTTP-Get Anfrage in den TCP-Buffer
    {
      case RECEIVER_WW:
        client.print(PUSHINGBOX_WW_STRING);
        client.print(EthernetData.swTemperaturWarmWasserLO / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturWarmWasserLO) % 10, DEC);
        // Holzmenge:
        client.print(PUSHINGBOX_HOLZMENGE_STRING);
        client.print(EthernetData.swHolzmenge, DEC);
        break;
      case RECEIVER_HZ:
        client.print(PUSHINGBOX_HZ_STRING);
        client.print(EthernetData.swTemperaturHeizungLM / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturHeizungLM) % 10, DEC);
        // Holzmenge:
        client.print(PUSHINGBOX_HOLZMENGE_STRING);
        client.print(EthernetData.swHolzmenge, DEC);
        break;
      case RECEIVER_AT:
        client.print(PUSHINGBOX_ATMEAN_STRING);
        client.print(EthernetData.swTemperaturAussenMittel / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturAussenMittel) % 10, DEC);
        break;
      case RECEIVER_TM:
        client.print(PUSHINGBOX_HZTIME_STRING);
        client.print(EthernetData.swEinheizZeitpunkt, DEC);
        break;
      case RECEIVER_STATUS:
        // Aussentemperatur Aktuell:
        client.print(PUSHINGBOX_STATUS_ID_AT_STRING);
        client.print(EthernetData.swTemperaturAussen / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturAussen) % 10, DEC);
        // Aussentemperatur Mittel:
        client.print(PUSHINGBOX_STATUS_ATMEAN_STRING);
        client.print(EthernetData.swTemperaturAussenMittel / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturAussenMittel) % 10, DEC);
        // Aussentemperatur Min:
        client.print(PUSHINGBOX_STATUS_ATMIN_STRING);
        client.print(EthernetData.swTemperaturAussenMin / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturAussenMin) % 10, DEC);
        // Aussentemperatur Max:
        client.print(PUSHINGBOX_STATUS_ATMAX_STRING);
        client.print(EthernetData.swTemperaturAussenMax / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturAussenMax) % 10, DEC);
        // Warmwassertemperatur:
        client.print(PUSHINGBOX_STATUS_WW_STRING);
        client.print(EthernetData.swTemperaturWarmWasserLO / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturWarmWasserLO) % 10, DEC);
        // Heizwassertemperatur:
        client.print(PUSHINGBOX_STATUS_HZ_STRING);
        client.print(EthernetData.swTemperaturHeizungLM / 10, DEC);
        client.print('.');
        client.print(ABS(EthernetData.swTemperaturHeizungLM) % 10, DEC);
        break;
    }

    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(serverName);
    client.println("User-Agent: Heizarduino");
    client.println();
  }
  else {
    Serial.println("connection failed");
  }
}
//<-------- push code ------------------------------------------------------------------------------

//-------- NTP code ------------------------------------------------------------------------------->

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

void digitalClockDisplay() {
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(".");
  Serial.print(month());
  Serial.print(".");
  Serial.print(year());
  Serial.println();
}

void printDigits(int digits) {
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
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
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(char* address)
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
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//<-------- NTP code -------------------------------------------------------------------------------
