# JebSolarManager
DIY Solar Net Metering Energy Storage System. Cheap, efficient, flexible.

This repo holds the code for the ESP32 Arduino control/monitoring system. I have documented the whole project here too.

![v3 monitoring showing calculated power, grid consumption/export, target power/current to achieve net zero export](/images/jsm_v3.JPEG)

## Overview
New project for the year of 2022 is to get some Solar Power generation for my home in Manchester, UK.

My requirements are:
* **Grid tied.** As opposed to 'Off Grid', grid-tied systems can feed power into your house whilst it is also connected to the national grid
* **Batteries.** Peak solar generation happens when you need it the least. Mornings/evenings are when you need it. Batteries can help with this.
* **Integration.** Possible to integrate with [Home Assistant](https://www.home-assistant.io/) and get some nice monitoring/control
* **Variable Scale.** I wanted to start off small (2 panels on my Shed roof), then add to the system as funds allowed.
* **No Vendor Lock-in.** I hate that.

## Design Considerations
The basic architecture of a Solar PV ESS (Energy Storage System) is panels -> hybrid inverter <-> batteries & grid. Hybrid inverters manage the complicated process of charging the batteries and/or turning it into AC to feed into the house.

Hybrid Inverters are only one solution (DC Coupling) - another would be AC Coupling, where you turn the Solar DC into AC straight away. You then have some other kit physically separated that charges/discharges the batteries by converting AC<->DC. Either of these solutions is fine - they are each suitible for slightly different situations.

The magic happens in the Hybrid Inverter. There are a few brands on the market, such as:
* Solis
* SolaX (some good cheap low power options)
* SolarEdge
* Victron (great flexibility with the MultiPlus II, but SO expensive)
* SMA
* GivEnergy (know someone who has one and likes it)
* Growatt (good cheap ones)
* Fronius
* And some others

Most inverters you find tend to be 'off grid' - they generate sine wave AC, but they can't be connected to any other generation source (like the national grid) because they would be out of phase. This would cause flames fairly quickly!

Due to lockdowns and the ensuing supply/demand imbalances, all the Commercial Off The Shelf hardware has suddenly become extremely hard to purchase - you'll be lucky to find anywhere with stock, let alone any bargains! For my requirements, I'd really need to spend about £1000 or more. Batteries are can be anything from £500-£1000 **per Kilowatt-hour!!**, and the batteries all tend to be vendor-locked. The pay-back period (when it breaks even and pays for the initial outlay+maintenance) of these systems (when professionally installed) can be around 15+ years, or even never!

So I decided to take a heavily DIY-oriented approach (much to my wife's despair!).

After much research, I bought two 300W microinverters. My intention was to do 'AC Coupling'. However, I later changed my mind to DC coupling and ordered a 1000W normal grid-tie inverter plus a 'charge controller'. The brand I chose for the Charge Controller was Victron. I really love their philosophy of openness with their hardware/software - the make it easy to integrate any kit. You can change almost all settings via bluetooth, which I eventually hope to interface with via an Arduino. I was aware of their reputation for reliability and robustness as I had used some of their Maritime kit through work.

Net Metering is the name given to the problem of ensuring that your batteries+inverters don't just immediately drain in order to feed the grid. This isn't a problem in off-grid systems, because inverters naturally limit their output (and therefore also their battery input) depending on the demand of the load attached. However, when you're tied to the grid, the grid can absorb an almost unlimited amount of your stored energy. In fact you'd never even get to store any in the batteries, because it would all get instantly exported! The solution is the monitor how much energy is leaving your house in the direction of the grid. A CT-clamp energy monitor can monitor not only power consumption, but the **direction** of the current - so it can differentiate the incoming energy into the house (consumption), and the outgoing energy exported to the grid (production).

## Architecture

* 2 panels for my shed roof (Perlite 295W - selected due to smallest size 54-cell panel)
* Victron charge controller 100/20 48V
* 3x12V LiFePO4 batteries (30Ah each), which gives 1,152Wh of storage in total. (cheapest on eBay, within my requirements)
* Y&H 1000W Grid Tie Inverter (cheap eBay)
* Arduino ESP32-based control and monitoring
* Fibaro plug socket (ZWave RF control) as a 'master switch'
* Aeotec Gen5 ZWave Energy Monitor
* Home Assistant running on server (ZWave hub)

![System Architecture](/images/Architecture.png)

## Initial Testing and Assembly

I fitted the energy meter first. It requires wiring in -ideally, straight to the Consumer Unit. This ensures the measured voltage matches the incoming meter tails as closely as possible. CT Clamps only measure current (A) and direction - it must be multiplied by voltage (V) to get power (W), then integrated over time to get energy (kWh):

![Aeotec Gen5 Energy Meter](/images/aeotech_meter.JPEG)


Here's the info on the back of the panels:

![Panel Info](/images/panel.JPEG)

To save on cost, I fabricated my own mounting system from some angle brackets and nuts/bolts/washers:

![Bracket Parts](/images/bracket_parts.JPEG)

I used some roof sealant underneath and around the brackets to ensure water tightness:

![Bracket Parts](/images/bracket_installed.JPEG)

After this, I had a long wait (weeks) for the inverters and batteries to be delivered. Every delivery was late!

I constructed a quick and dirty battery tray to slide in under my workbench:

![Battery Tray](/images/battery_tray1.JPEG)
![Battery Tray - Installed](/images/battery_tray2.JPEG)

The Victron Charge Controller worked great when connected up to the batteries. It was more efficient at extracting power than my cheap Chinese inverters, and the batteries charged in no time even with fairly weak sun:

![Victron stats](/images/victron_1.PNG) ![Victron stats](/images/victron_2.PNG)


Here's a photo of the initial assembly (which doesn't work yet!):

![Initial assembly](/images/assembled_1.JPEG)

My main issue here was that I have no Net Metering yet. This was fine until I switched on the batteries and a massive inrush current trips the breakers and fries the 12A DC fuses in the micro-inverters! This was a major mistake. I connected the 300W microinverters (designed to only be connected to a <300W panel) directly to a very powerful current source with extremely low internal resistance, and, of course, it fried the inverters. Luckily I could just solder a new fuse onto the microinverters:

![Fixed Microinverter Fuses](/images/microinverter_fuse.jpeg)

With the new fuses I tried it again. The inverters ran ok this time, but their combined output was 620W - more than their rated capacity. And the batteries were draining fast. The power was all escaping out of the house into the grid!

So reluctantly I had to disconnect the batteries and the Victron Charge Controller. I connected the each of the panels to a Microinverter, and combined the AC output, which was then fed into the house by simply plugging in a standard 13A plug via the ZWave relay plug. Simple, but it works!

## Control Circuitry
Now began the main task - designing and building a monitoring & control circuit for my Net Zero requirement.

I decided to approach Net Metering by limiting the current that goes in to the inverter. When the inverter senses a current limitation via its MPPT algorithm, it will begin scaling back the voltage until it finds the optimal voltage/current. I was a bit worried this will produce some kind of oscillations, as often is the case when you have two competing control loops. Time will tell if this is the case!

My previous 'Go-To' Internet Of Things devices were either a Raspberry Pi or some sort of Arduino with Bluetooth, then a bluetooth dongle to a Pi/Server. I discovered to my horror that (due to lockdown supply chain woes) Raspberry Pis are now impossible to get hold of! There were none to be bought anywhere! I played with an old 'RedBearLabs Blend Micro' board I had (basically an Arduino Nano + Bluetooth) for a while, which worked fine but was very limited...the company had ceased to exist since I bought it in 2016.

Then I discovered the wonders of ESP32s. These amazing little boards have a powerful Arduino AND WiFi+Bluetooth. You can prototype on them simply by plugging them into USB - no need for any external wires etc, they don't even need to sit in a breadboard. Obviously as soon as you want to connect any peripherals to them, a breadboard becomes a necessity.

I bought an ADS1115 to perform 16-bit analogue-to-digital conversions. This is an awesome little board - it can do **differential** comparison, i.e. outputting the difference in voltage between two input pins. This is so insanely accurate (way better than a millivolt) compared to the native Arduino ground-referenced ADC. Interface is I2C.

I also bought a little 1.3" 128x64 OLED I2C screen as an afterthought, and I was so glad I did - they were so easy to integrate.

The circuit would have two main functions - measurement/monitoring, and control (via current limiting)

### Measurements & Monitoring
Current measurement would be performed by a 50A 75mV shunt (using differential inputs A0+A1), connected to the 'low-side' (i.e. between the Load and Ground). Voltage was measured via differential pins A2+A3, connected to the +ve input, and Ground. A simple resistor network is needed to get the voltage into the correct range for the ADS1115.  

Here's the initial prototype:

![Prototype of power monitoring](/images/esp_1.JPEG)

And here it is in-situ (note that the Victron/batteries are now disconnected):

![Prototype of power monitoring - installed](/images/assembled_2.JPEG)


### Current Limiting
This circuit is not yet built. My intention is:
* Use one or more MOSFETs to vary the current that can pass. These will need to be beefy MOSFETS with nice chunky heatsinks, as they will be dissipating a lot of wasted energy (maybe 5-10 Watts).
* Drive the MOSFET gate using an Op Amp output (with feedback into the inverting input)
* Drive the Op Amp using a 0-3.3v analogue output from the ESP32 

## Software Overview

I set up an MQTT (Mosquitto) broker on my server, via Docker. This server also runs Home Assistant. I will use this nice lightweight publish/subscribe mechanism to get data to/from the ESP32.

### Algorithm
Here's a brief summary of the code contained in this repo.

#### Setup
* Initialise display
* Initialise ADS1115
* Connect to WiFi
* Initialise web server (only for debug purposes)
* Initialise MQTT + subscribe to inputs (grid consumption and previous cumulative energy)

#### Loop
* Perform measurements of volts + amps
* Calculate power and increment cumulative total of energy
* Calculate target power & target amps
* Apply an alpha smoothing filter (a.k.a exponential moving average, software low pass filter) to prevent sudden rapid changes
* Write target amps via DAC (Digital Analogue Conversion) to pin 25
* Every so often, update the OLED display 
* Every so often, publish readings/decisions to MQTT

#### Callback
* On MQTT topic update, read in new grid consumption

### Home Assistant Integration

Firstly, I created an Automation that triggers whenever the Aeotec Energy Meter updates the current power consumption to/from the grid. This simply forwards the value to an MQTT topic.

Then, I created a sensors.yaml file to map the MQTT topics output by the ESP32 into Entities. In HASS, every input is either an 'event' or an 'entity'. Entities can have states, which can be primitive values. (OK - that is a simplification and not quite correct, but it'll do for now). 

sensors.yaml:
```
- platform: mqtt
  name: "JSM Volts"
  state_topic: "jsm/volts"
  unit_of_measurement: 'V'
  device_class: voltage
  state_class: measurement

- platform: mqtt
  name: "JSM Amps"
  state_topic: "jsm/amps"
  unit_of_measurement: 'A'
  device_class: current
  state_class: measurement

- platform: mqtt
  name: "JSM Power"
  state_topic: "jsm/power"
  unit_of_measurement: 'W'
  device_class: power
  state_class: measurement

- platform: mqtt
  name: "JSM Energy"
  state_topic: "jsm/energy"
  unit_of_measurement: 'kWh'
  state_class: total_increasing
  device_class: energy
```


You can see here that at the time of screeshot, I was exporting a small amount of power to the grid (the negative sign on consumption shows this):

![HASS Dashboard showing readouts](/images/hass_export.PNG)

Then, I updated my energy summary dashboard to include these new entities. HASS has a great inbuilt Energy dashboard with some fancy animations for solar/grid usage. It even integrates with an online forecasting API that uses your lat/lon, the panel azimuth/elevation, etc to predict generation throughout the day.

This is a screenshot from the very first day I tried the monitoring software. As you can see, I underperformed the forecast quite significantly! The weather suddenly turned cloudy in the afternoon though.

![HASS Energy Dashboard](/images/hass_energy_dashboard.PNG)

## Current Situation
I've not yet done the current limiting circuit. Original architecture is not implemented - batteries/victron are disconnected.

v0.3 software currently deployed, and it does a great job of monitoring.

## Future goals

* Battery monitoring
* Interface to Victron, either by Bluetooth or VE Direct (2 or 4 pin serial-like interface, open interface)
* Get LOTS more solar panels
* Try the Victron MultiPlus II inverter - it does basically everything I want, but costs ~£1,000.
* Buy or Build a *large* battery pack. Depending on panels/inverters (and funds!), I'd go up to 10kWh of storage.


## Useful Links
[ESP-WROOM-32 Pinout](https://lastminuteengineers.com/esp32-pinout-reference/)
[Reading a Current Shunt with an Arduino](https://learnarduinonow.com/2015/05/11/reading-current-shunt-with-arduino.html)
[Victron SmartSolar 100/20 Manual](https://www.victronenergy.com/upload/documents/Manual_SmartSolar_MPPT_75-10_up_to_100-20/MPPT_solar_charger_manual-en.pdf)
[Victron VE Direct Protocol FAQ](https://www.victronenergy.com/live/vedirect_protocol:faq)