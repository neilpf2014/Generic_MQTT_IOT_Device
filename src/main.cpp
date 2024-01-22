/***   IOT Generic Demo
 *  Generic Demo for MicroController Monday
 *  Use JSON formated MQTT messages
 *  Based of of the light controller code
 *  Need ensure all of the Light stuff is commented out
 *  NF 20231223
*/

#include <stdlib.h>
#include <Arduino.h>
#include <PushButton.h>
#include <MQTThandler.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <ESPmDNS.h>

#define DEBUG_ON 1
byte debugMode = DEBUG_ON;
//#define Debug_LTS

#define DBG(...) debugMode == DEBUG_ON ? Serial.println(__VA_ARGS__) : NULL
//Fast led stuff

#define DATA_PIN 13
#define BTN_1_PIN 14
#define BTN_2_PIN 33

#define NUM_LEDS 150
#define LED_PERIOD 200
#define BTN_PERIOD 100 //ms

//
uint32_t effectCode;
uint32_t lastEffect;
uint32_t speed;
uint32_t ledBlockStart;
uint32_t ledBlocklen;

// Buttons
uint32_t btn1Pushes;
uint32_t btn2Pushes;
uint32_t btn1Total;
uint32_t btn2Total;
uint64_t Btn_timer;
PushButton PB1(BTN_1_PIN);
PushButton PB1(BTN_2_PIN);




// for dealing with data coming in from WS or other JSON source
uint32_t JSStatusCode;
uint32_t JSledIndex;
uint32_t JSDelayMs;
uint8_t JSredVal;
uint8_t JSGreenVal;
uint8_t JSBlueVal;

// last timer update
uint64_t LEDPastMils;
uint64_t PastMils;




#define PUBSUB_DELAY 200          // ms pubsub update rate
#define ADD_TIME_BTN_DELAY 2000   // ms delay to count add time btn as "down"

#define AP_DELAY 2000
#define HARD_CODE_BROKER "192.168.1.150"
#define CONFIG_FILE "/svr-config.json"
uint64_t PubSub_timer;


//********** Wifi and MQTT stuff below ******************************************************
//******* based on Moxie board project  *****************************************************
//** Update these with values suitable for the broker used. *********************************
//** should now save param's entered in the CP screen

IPAddress MQTTIp(192, 168, 1, 140); // IP oF the MQTT broker if not 192.168.1.183

WiFiClient espClient;
uint64_t lastMsg = 0;
unsigned long MessID;
String Msgcontents;

uint64_t msgPeriod = 10000; // Message check interval in ms (10 sec for testing)

String S_Stat_msg;
String S_Match;
String sBrokerIP;
int value = 0;
uint8_t GotMail;
uint8_t statusCode;
bool SaveConf_flag = false;
bool Use_def_IP_flag = false;

uint8_t ConnectedToAP = false;
MQTThandler MTQ(espClient, MQTTIp);
// set topics as neeed
const char *outTopic = "IOTDemoOut";
const char *inTopic = "IOTDemoIn";

// used to get JSON config
uint8_t GetConfData(void)
{
  uint8_t retVal = 1;
  if (SPIFFS.begin(false) || SPIFFS.begin(true))
  {
    if (SPIFFS.exists(CONFIG_FILE))
    {
      File CfgFile = SPIFFS.open(CONFIG_FILE, "r");
      if (CfgFile.available())
      {
        StaticJsonDocument<512> jsn;
        DeserializationError jsErr = deserializeJson(jsn, CfgFile);
        serializeJsonPretty(jsn, Serial); // think this is just going to print to serial
        if (!jsErr)
        {
          sBrokerIP = jsn["BrokerIP"].as<String>();
          retVal = 0;
        }
        else
          DBG("JSON error");
      }
    }
    else
      DBG("File_not_found");
  }
  return retVal;
}

// used to save config as JSON
uint8_t SaveConfData(String sIP)
{
  uint8_t retVal = 1;
  // SPIFFS.format();
  if (SPIFFS.begin(true))
  {

    StaticJsonDocument<512> jsn;
    jsn["BrokerIP"] = sIP;
    File CfgFile = SPIFFS.open(CONFIG_FILE, "w");
    if (CfgFile)
    {
      if (serializeJson(jsn, CfgFile) != 0)
      {
        retVal = 0;
        DBG("wrote something");
      }
      else
        DBG("failed to write file");
    }
    CfgFile.close();
  }
  else
  {
    retVal = 1;
    DBG("failed to open file");
  }

  return retVal;
}

// Wifi captive portal setup on ESP32
void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  // if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  digitalWrite(LED_BUILTIN, HIGH);
  Use_def_IP_flag = true;
}
void saveConfigCallback()
{
  // Set save config flag
  SaveConf_flag = true;
}

// Split wifi config into 2 procs to avoid code duplication
// this does most of the heavy lifting
// Saving of Params is working now !!
void WiFiCP(WiFiManager &WFM)
{
  bool validIP;
  uint8_t loadedFile;
  uint8_t savedFile;
  uint8_t isConnected;
  bool replaceHCIP;
  String sIPaddr;
  IPAddress MQTTeIP;

  WFM.setSaveConfigCallback(saveConfigCallback);
  WFM.setAPCallback(configModeCallback);
  replaceHCIP = false;
  WiFiManagerParameter TB_brokerIP("TBbroker", "MQTT broker IP", "192.168.1.150", 30);
  WFM.setHostname("IOT_Demo_Dev1");
  WFM.addParameter(&TB_brokerIP);
  isConnected = WFM.autoConnect("IOTdemo_ConfigAP");
  if (isConnected)
  {
    DBG("Connected");
    loadedFile = GetConfData();
    // load from file ignore TB
    if (!Use_def_IP_flag)
    {
      DBG("loaded IP from File");
      validIP = MQTTeIP.fromString(sBrokerIP);
      if (validIP)
        replaceHCIP = true;
    }
    else
    {
      sIPaddr = TB_brokerIP.getValue();
      DBG("Used IP from TB");
      if (!sIPaddr.isEmpty())
      {
        validIP = MQTTeIP.fromString(sIPaddr);
        if (validIP)
        {
          replaceHCIP = true;
          if (SaveConf_flag == true)
          {
            sBrokerIP = sIPaddr;
            SaveConf_flag = SaveConfData(sIPaddr);
          }
        }
      }
    }
    if (replaceHCIP == true)
    {
      MQTTIp = MQTTeIP;
      DBG("replaced default");
    }
  }
}

// called to set up wifi -- Part 1 of 2
void WiFiConf(uint8_t ResetAP)
{
  WiFiManager wifiManager;
  if (ResetAP)
  {
    wifiManager.resetSettings();
    WiFiCP(wifiManager);
  }
  else
    WiFiCP(wifiManager);
  // these are used for debug
  Serial.println("Print IP:");
  Serial.println(WiFi.localIP());
  // **************************
  GotMail = false;
  MTQ.setClientName("ESP32Client");
  MTQ.subscribeIncomming(inTopic);
  MTQ.subscribeOutgoing(outTopic);
}


// **********************************************************************************************
// ****************** End Wifi Config code ******************************************************


// separate the Wifi / MQTT init from other setup stuff
void IOTsetup()
{
  bool testIP;
  uint64_t APmodeCKtimer;
  uint8_t Btnstate;
  uint8_t tempint;
  APmodeCKtimer = millis();
  Btnstate = 0;

  // Will wait 2 sec and check for reset to be held down / pressed
  while ((APmodeCKtimer + AP_DELAY) > millis())
  {
    if (PB1.isCycled())
      Btnstate = 1;
    PB1.update();
  }
  tempint = PB1.cycleCount();
  String TempIP = MQTTIp.toString();
  // these lines set up the access point, mqtt & other internet stuff
  pinMode(LED_BUILTIN, OUTPUT); // Initialize the Green for Wifi
  WiFiConf(Btnstate);
  // comment this out so we can enter broker IP on setup
  /*
  testIP = mDNShelper();
  if (!testIP){
    MQTTIp.fromString(TempIP);
  }
  Serial.print("IP address of server: ");
  */
  Serial.print("IP address of broker: ");
  Serial.println(MQTTIp.toString());
  MTQ.setServerIP(MQTTIp);
  digitalWrite(LED_BUILTIN, LOW); // turn off the light if on from config
  // **********************************************************
}

// Added to output to MQTT as JSON
String MakeJson(uint32_t BtnNo, uint32_t Pushes, uint32_t Total)
{
  StaticJsonDocument<256> JM;
  String sJSoutput;
  JM["Mills"] = millis();
  JM["ButtonNo"] = BtnNo;
  JM["Pushes"] = Pushes;
  JM["TotalPushes"] = Total;
  serializeJson(JM, sJSoutput);
  return sJSoutput;
}

// Generic handle incoming
uint32_t MQTThandleIncoming(String MsgString)
{
  char CMD;
  String s_latter_message_contents;

  CMD = MsgString.charAt(0);
  if (MsgString.length() > 1)
  {
    
  }
  return 0;
}

// simple json deserialize here, this let us set on item on the string.
uint32_t DSIncomingJSON(String MsgString, uint32_t &ecode, uint32_t &startpos, uint32_t &blocklen, uint32_t &speed, uint8_t &redVal, uint8_t &GrnVal, uint8_t &BlueVal)
{
  StaticJsonDocument<512> IJSON;
  uint32_t RetVal = 0;
  //String s_latter_message_contents;

  uint32_t temp_ecode;
  uint32_t temp_speed;
  uint32_t temp_start;
  uint32_t temp_len;
  uint8_t temp_r;
  uint8_t temp_g;
  uint8_t temp_b;

  if (MsgString.length() > 1)
  {
    DeserializationError er = deserializeJson(IJSON, MsgString);
    if(er)
      RetVal = 2; // deserialize error
    else
    {
      temp_ecode = IJSON["EffectCode"].as<uint32_t>();
      temp_start = IJSON["StartPos"].as<uint32_t>();
      temp_len = IJSON["BlockLen"].as<uint32_t>();
      temp_speed = IJSON["DelayMs"].as<uint32_t>();
      //ledIndex = IJSON["LedIndex"].as<uint32_t>();
      temp_r = IJSON["r"].as<uint8_t>();
      temp_g = IJSON["g"].as<uint8_t>();
      temp_b = IJSON["b"].as<uint8_t>();
    } 
    if (temp_ecode > 0)
    {
      if (ecode != temp_ecode)
      { ecode = temp_ecode;
        RetVal = 1;}
      if(startpos != temp_start)
        {startpos = temp_start;
        RetVal = 1;}
      if(blocklen != temp_len)
        {blocklen = temp_len;
        RetVal = 1;}
      if(speed != temp_speed)
      { speed = temp_speed;
        RetVal = 1;}
      if(redVal != temp_r)
        {redVal = temp_r;
        RetVal = 1;}
      if(GrnVal != temp_g)
        {GrnVal = temp_g;
        RetVal = 1;}
      if(BlueVal != temp_b)
        {BlueVal = temp_b;
        RetVal = 1;}
    }
  }
  else
    RetVal = 2; // passed this an empty string
  return RetVal;
}

void setup()
{

  Serial.begin(115200);
  IOTsetup();
  
  
  Serial.println("program starting");
  delay(1000);
  LEDPastMils = millis();
  
  //gSDtimer = 0;
  JSDelayMs = LED_PERIOD;
  Btn_timer = millis();
  PubSub_timer = millis();
}

void loop() {
  
  // Add btn reading and other code here
  if ((millis() - Btn_timer) > BTN_PERIOD)
  {
    // read btns

  }
  

  // Deal with MQTT Pubsub
  if ((millis() - PubSub_timer) > PUBSUB_DELAY)
  {
    // Send as JSON now
    // This is just going to set the entire strip to be red

    GotMail = MTQ.update();
    if (GotMail == true)
    {
      //*********Incoming Msg *****************************
      //Serial.print("message is: ");
      Msgcontents = MTQ.GetMsg();
      Serial.println(Msgcontents);
      //JSStatusCode = DSIncomingJSON(Msgcontents,effectCode,ledBlockStart,ledBlocklen,JSDelayMs,JSredVal,JSGreenVal,JSBlueVal);
      if (JSStatusCode > 1)
      {
        // Set LED from returned contents
        Serial.println("JSON error");
        
      }
      //MQTThandleIncoming(Msgcontents);
      //********************************************************
      GotMail = false;
    }
    PubSub_timer = millis();
  }
}