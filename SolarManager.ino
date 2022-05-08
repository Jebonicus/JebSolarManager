#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_ADS1X15.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <PubSubClient.h>

#define SIMULATION false

#define LED_BUILTIN 2
// Output voltage for MOSFET control
#define DAC1 25

// VOLTMETER_RESISTOR_FACTOR = (R1+R2)/R2
#define VOLTMETER_RESISTOR_FACTOR 395.90196078
#define VOLT_FACTOR ((0.000125 * VOLTMETER_RESISTOR_FACTOR) / 16.0)
#define PERIOD_MS 50
#define DISPLAY_RATE 4
#define OUTPUT_RATE 100
// How quickly to change power, between 0.0-1.0. Smaller = slower, larger=faster (careful!)
#define TARGET_POWER_ALPHA 0.02

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
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#include "secrets.h"
/* Put your SSID & Password */
//const char* ssid = "XXX";
//const char* password = "XXX";
//const char* mqtt_server = "XXX";
//const char* mqtt_user = "XXX";
//const char* mqtt_pass = "XXX";

Adafruit_ADS1115 ads;
WebServer server(80);
WiFiClient espClient;
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
  display.println("JebSolarManager v0.3");
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
  server.on("/", handle_OnConnect);
  server.onNotFound(handle_NotFound);
  
  server.begin();
  Serial.println("HTTP server started");

  mqtt.setServer(mqtt_server, 1883);
  mqtt.setCallback(mqtt_callback);
  mqtt_connect();
  display.print("Ready!");
  display.display();
  delay(1000);
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
    volts = 35.0;
  }
}

void updateDisplay(void)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("JebSolarManager v0.3");
  
  display.setCursor(0, 15);
  display.print(String(volts, 2)); display.print(" V    ");
  display.print(String(amps, 2)); display.println(" A");
  display.print("Power: "); display.print(String(power, 2)); display.println(" W");

  display.print("Total: "); display.print(String(energy, 4)); display.println(" kWh");

  if(mqtt_initialised)
  {
    display.print("Grid In: "); display.print(String(grid_consumption, 2)); display.println(" W");
    display.println("");
    display.print("Limit: "); display.print(String(target_power, 0)); display.print(" W "); display.print(String(target_amps, 2)); display.println(" A");
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
  target_power = (target_power * (1.0f - TARGET_POWER_ALPHA)) + (new_target_power * TARGET_POWER_ALPHA);
  
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
  server.handleClient();
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

void handle_OnConnect() {
  server.send(200, "text/html", SendHTML()); 
}

void handle_NotFound(){
  server.send(404, "text/plain", "Not found");
}

String SendHTML(void){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr +="<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr +="<title>JebSolarManager v0.3</title>\n";
  ptr +="<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr +="body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
/*  ptr +=".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr +=".button-on {background-color: #3498db;}\n";
  ptr +=".button-on:active {background-color: #2980b9;}\n";
  ptr +=".button-off {background-color: #34495e;}\n";
  ptr +=".button-off:active {background-color: #2c3e50;}\n";*/
  ptr +="p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr +="</style>\n";
  ptr +="</head>\n";
  ptr +="<body>\n";
  ptr +="<h1>JebSolarManager v0.3";
  if(SIMULATION) {
    ptr += " - SIMULATION";
  }
  ptr +="</h1>\n";

  ptr += "<p><b>Measured Current:</b>  " + String(amps, 3) + "A </p><br>\n";
  ptr += "<p><b>Measured Volts:</b>  " + String(volts, 3) + "V </p><br>\n";
  ptr += "<p><b>Measured Power:</b>  " + String(power, 3) + "W </p><br>\n";
  ptr += "<p><b>Total Energy Production:</b>  " + String(energy, 6) + "kWh </p><br>\n";
  ptr += "<br>\n";
  ptr += "<p><b>Grid Consumption:</b>" + String(grid_consumption, 2) + "W</p><br>\n";
  ptr += "<hr>\n";
  ptr += "<p><b>Target Power:</b>" + String(target_power, 2) + "W</p><br>\n";
  ptr += "<p><b>Target Amps:</b>" + String(target_amps, 2) + "A</p><br>\n";
  ptr += "<p><b>DAC pin:</b>" + String(dac_value) + "</p><br>\n";
  
  ptr +="</body>\n";
  ptr +="</html>\n";
  return ptr;
}
