#include <WiFi.h>
//#include <jebwebserver.h>
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"
#include <Adafruit_ADS1X15.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <PubSubClient.h>

const String versionStr = "v0.3";
#define SIMULATION true

#define LED_BUILTIN 2
// Output voltage for MOSFET control
#define DAC1 25

// VOLTMETER_RESISTOR_FACTOR = (R1+R2)/R2
#define VOLTMETER_RESISTOR_FACTOR 395.90196078
#define VOLT_FACTOR ((0.000125 * VOLTMETER_RESISTOR_FACTOR) / 16.0)
#define PERIOD_MS 50
#define DISPLAY_RATE 4
#define OUTPUT_RATE 100

#define MAX_POWER 300.0f
#define MAX_AMPS 10.0f
#define MAX_DAC_AMPS 10.0f
#define MIN_VOLTS 15.0f
#define STALE_TIMEOUT_MS 30000.0f

// Display
#define i2c_Address 0x3c //initialize with the I2C addr 0x3C Typically eBay OLED's
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1   //   QT-PY / XIAO


#include "secrets.h"
// Secrets.h contains the following:
/* Put your SSID & Password */
//const char* ssid = "XXX";
//const char* password = "XXX";
//const char* mqtt_server = "XXX";
//const char* mqtt_user = "XXX";
//const char* mqtt_pass = "XXX";


Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_ADS1115 ads;
WiFiClient espClient;
//JebWebServer server;
AsyncWebServer server(80);
PubSubClient mqtt(espClient);

float amps = 0.0f; // Unit: A, Topic: jsm/amps
float volts = 0.0f; // Unit: V, Topic: jsm/volts
float power = 0.0f; // Unit: W, Topic: jsm/power
double energy_ws = 0.0; // Total consumption in Watt*Seconds
double energy = 0.0; // Unit: kWh, Topic: jsm/energy
float grid_consumption = 0.0f; // Topic: jsm/hass/power
float target_power = 0.0f;
float target_amps = 0.0f;
int dac_value = 0;
int16_t resultsA; // raw amp ADC reading
int16_t resultsV; // raw volt ADC reading
uint16_t loop_counter = 0; // Used to slow update of display/mqtt
bool mqtt_initialised = false;
// How quickly to change power, between 0.0-1.0. Smaller = slower, larger=faster (careful!)
float target_power_alpha = 0.02f;

uint16_t grid_consumption_lastupdate = 0;

void setup() {
  Serial.begin(57600);
  
  Serial.print("Initialising Display...");
  delay(250); // wait for the OLED to power up
  display.begin(i2c_Address, true); // Address 0x3C default
  display.display();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print("JebSolarManager "); display.println(versionStr);
  display.print("Booting...");
  display.display();

  if(!SIMULATION)
  {
    Serial.print("Initialising ADS...");
    ads.setGain(GAIN_SIXTEEN);    // 16x gain +/- 0.256V 1 bit = 0.125mV 0.0078125mV
    ads.begin();
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to "); Serial.println(ssid);
  delay(100);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(500);
 
    if ((++i % 16) == 0)
    {
      Serial.println(F(" still trying to connect"));
    }
  }

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqtt_callback);
  mqtt_connect();
  
  
  if(!SPIFFS.begin(false)){
     Serial.println("An Error has occurred while mounting SPIFFS - web server not available");
  }

  server.on("/setAlpha", HTTP_ANY, [](AsyncWebServerRequest *request){
    bool ok = false;
    if(request->hasParam("alpha", true))
    {
      AsyncWebParameter* p = request->getParam("alpha", true);
      float newAlpha = p->value().toFloat();
      if(newAlpha>=0.0f && newAlpha<=1.0f)
      {
        target_power_alpha = newAlpha;
        Serial.print("Updated Alpha to: "); Serial.println(String(target_power_alpha, 3));
        ok = true;
      }
    }
    if(ok)
    {
      request->send(200, "application/json", "{\"response\":\"ok\"}");
    }
    else
    {
      request->send(400, "application/json", "{\"response\":\"ko\",\"reason\":\"invalid/missing alpha parameter - must be between 0.0-1.0\"}");
    }
  });
  server.serveStatic("/", SPIFFS, "/").setTemplateProcessor(getHtmlTemplateVar).setDefaultFile("index.html");
 
  server.begin();
  Serial.println("Web Server started!");
  listDir(SPIFFS, "/", 0);
  display.print("Ready!");
  display.display();
  delay(1000);
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
   Serial.printf("Listing directory: %s\r\n", dirname);

   File root = fs.open(dirname);
   if(!root){
      Serial.println("− failed to open directory");
      return;
   }
   if(!root.isDirectory()){
      Serial.println(" − not a directory");
      return;
   }

   File file = root.openNextFile();
   while(file){
      if(file.isDirectory()){
         Serial.print("  DIR : ");
         Serial.println(file.name());
         if(levels){
            listDir(fs, file.name(), levels -1);
         }
      } else {
         Serial.print("  FILE: ");
         Serial.print(file.name());
         Serial.print("\tSIZE: ");
         Serial.println(file.size());
      }
      file = root.openNextFile();
   }
}

String getHtmlTemplateVar(const String& var)
{
  if(var == "VERSION"){
    return versionStr;
  } else if(var == "CURRENT") {
    return String(amps, 2);
  } else if(var == "VOLTAGE") {
    return String(volts, 2);
  } else if(var == "POWER") {
    return String(power, 2);
  } else if(var == "ENERGY") {
    return String(energy, 4);
  } else if(var == "GRID_CONSUMPTION") {
    return String(grid_consumption, 2);
  } else if(var == "TARGET_POWER") {
    return String(target_power, 2);
  } else if(var == "TARGET_CURRENT") {
    return String(target_amps, 2);
  } else if(var == "DAC") {
    return String(dac_value);
  } else if(var == "TARGET_POWER_ALPHA") {
    return String(target_power_alpha, 3);
  } else if(var == "SIM") {
    return SIMULATION ? String("<br>Simulation"):String("");
  }
 
  return String();
}

void mqtt_callback(char* topic_b, byte* payload_b, unsigned int length)
{
  String topic = String(topic_b);
  String payload = String((char *)payload_b);

  if(topic == "jsm/hass/power")
  {
    grid_consumption = payload.toFloat();
    grid_consumption_lastupdate = loop_counter;
  }
  else if(topic == "jsm/energy")
  {
    energy = payload.toFloat();
    energy_ws += energy * 3600.0 * 1000.0;
    Serial.print("Got initial update for jsm/energy: "); Serial.print(String(energy, 6)); Serial.println(" kWh");
    mqtt_initialised = true;
    mqtt.unsubscribe("jsm/energy");
  }
}

bool mqtt_connect(void)
{
  if(!mqtt.connected())
  {
    const char * clientId;
    if(!SIMULATION)
    {
      clientId = "JSM";
    }
    else
    {
      clientId = "JSM-Sim";
    }
    if(mqtt.connect(clientId, mqtt_user, mqtt_pass))
    {
      Serial.println("Connected to MQTT, subscribing...");
      mqtt.subscribe("jsm/hass/power");
      if(!mqtt_initialised)
      {
        mqtt.subscribe("jsm/energy");
      }
    } else {
      Serial.println("Cannot connect to MQTT");
    }
  }
  return mqtt.connected();
}

void readAmps(void)
{
  int16_t results;
  
  if(!SIMULATION)
  {
    resultsA = ads.readADC_Differential_0_1();
    amps = ((float)resultsA * 128.0) / 32768.0;//100mv shunt
    amps = amps * 1.333; //uncomment for 75mv shunt
  //amps = amps * 2; //uncomment for 50mv shunt
  }
  else
  {
    amps = 6 + 2 * ((loop_counter%5000)/5000.0);
  }
}

void readVolts(void)
{
  if(!SIMULATION)
  {
    resultsV = ads.readADC_Differential_2_3();
    volts = resultsV * VOLT_FACTOR;
  }
  else
  {
    volts = 35.12;
  }
}

void updateDisplay(void)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print("JebSolarManager "); display.println(versionStr);
  
  display.setCursor(0, 15);
  display.print("      ");
  display.print(String(volts, 2)); display.print(" V ");
  display.print(String(amps, 2)); display.println(" A");
  display.print("Power "); display.print(String(power, 0)); display.println(" W");

  display.print("Total "); display.print(String(energy, 4)); display.println(" kWh");

  if(mqtt_initialised)
  {
    display.print("Grid  "); display.print(String(grid_consumption, 0)); display.println(" W");
    display.println("");
    display.print("Limit "); display.print(String(target_power, 0)); display.print(" W "); display.print(String(target_amps, 2)); display.println(" A");
  }
  else
  {
    display.println("MQTT Not Initialised!");
  }
  
  display.display();
}

void processMeasurements(void)
{
  power = volts * amps;
  power *= 2.0; // JEB: HACK to guess other panel
  energy_ws += power * (PERIOD_MS/1000.0);
  if(mqtt_initialised)
  {
    energy = energy_ws / (3600.0 * 1000.0);
  }

  if(SIMULATION)
  {
    grid_consumption_lastupdate = loop_counter;
  }
  float new_target_power = 0.0f;
  if(volts > MIN_VOLTS && (grid_consumption_lastupdate+(STALE_TIMEOUT_MS/PERIOD_MS) >= loop_counter))
  {
    new_target_power = min(MAX_POWER, max(0.0f, power + grid_consumption));
  }
  target_power = (target_power * (1.0f - target_power_alpha)) + (new_target_power * target_power_alpha);
  
  target_amps = min(MAX_AMPS, target_power / max(MIN_VOLTS, volts));

  dac_value = min(255, max(0, (int)round((target_amps/MAX_DAC_AMPS) * 255.0f)));
  
  dacWrite(DAC1, dac_value);
}

void publish_mqtt(void)
{
  if (mqtt_connect() && !SIMULATION)
  {
    mqtt.publish("jsm/amps", (char *)String(amps, 3).c_str());
    mqtt.publish("jsm/volts", (char *)String(volts, 3).c_str());
    mqtt.publish("jsm/power", (char *)String(power, 3).c_str());
    if(mqtt_initialised)
    {
      mqtt.publish("jsm/energy", (char *)String(energy, 4).c_str(), true);
    }
  }
}

void loop() {
  readAmps();
  readVolts();
  processMeasurements();

  if((loop_counter % DISPLAY_RATE) == 0)
  {
    updateDisplay();
  }
  
  loop_counter++;
  
  mqtt.loop();
  
  if((loop_counter % OUTPUT_RATE) == 0)
  {
    Serial.print(String(amps, 2));
    Serial.print("A [");
    Serial.print(String(resultsA)); 
    Serial.print("], ");
    Serial.print(String(volts, 3)); 
    Serial.print("V [");
    Serial.print(String(resultsV)); 
    Serial.print("], ");
    Serial.print(String(power, 3)); 
    Serial.print("W, ");
    Serial.print(String(energy_ws, 6)); 
    Serial.print("Ws, ");
    Serial.print(String(energy, 6)); 
    Serial.print("kWh, Net=");
    Serial.print(String(grid_consumption, 2)); 
    Serial.print("W, Targ=");
    Serial.print(String(target_power, 2)); 
    Serial.print("W, ");
    Serial.print(String(target_amps, 2)); 
    Serial.print("A, ");
    Serial.print(String(dac_value));
    Serial.println("");
    publish_mqtt();
  }
  delay(PERIOD_MS);
}
