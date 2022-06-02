/*
UCI CS147 - Professor Maity
Names: Michael Schuler and Kavan Dharia
Group: 3
Project: PlantIQ - A Smart Monitoring System
*/

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <cmath>

#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#include <Adafruit_AHTX0.h>
#include "ESP32Tone.h"

//all of our GPIO Pins
#define SOIL_MOIST_PIN 33
#define PHOTO_RST_PIN 32
#define BUZZER_PIN 15

//LED pins
#define SOILHUM_LIGHT_PIN 25
#define TEMP_LIGHT_PIN 26
#define SUNLIGHT_LIGHT_PIN 27

//stores our temperature sensor
Adafruit_AHTX0 sensor;

//stores the frequency of the buzzer
#define HUM_DCB 1048
#define TEMP_DCB 1548
#define SUNLIGHT_DCB 2048

//will store how long to wait in between readings (s, ms respectively)
#define DIALOGUE_DELAY_SC 2
#define CALIBRATION_DELAY_MS 500
#define READ_DELAY_SC 5
#define CALIBRATION_PERIOD_MS 10000
#define NOTIFICATION_DELAY 5000

//used for mapping light and soil sensor values
#define FLOOR_VAL 0
#define MAX_MAP_VAL 100

//define the threshholds of our plant to monitor and their boundary values
//soil humidity range
#define REL_HUM_FLOOR 15
#define REL_HUM_CEIL 85

//temperature range
#define REL_TEMP_FLOOR 61
#define REL_TEMP_CEIL 76

//sunlight % range
#define REL_SOLAR_FLOOR 17
#define REL_SOLAR_CEIL 82

//error checking boundary values
#define LOWER_BOUND 0
#define UPPER_BOUND_TMP 120
#define UPPER_BOUND_SOIL 100
#define UPPER_BOUND_SUNL 100

//weather api change in thresholds
#define LOWER_CHANGE_THRESH 4.0
#define MED_CHANGE_THRESH 8.0
#define HIGH_CHANGE_THRESH 12.0

//thresholds for air humidity
#define LOWER_AIR_HUM_THRESH 40
#define UPPER_AIR_HUM_THRESH 62

//string to make output easier to read
#define CLEAR_STRING "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"

//buzzer state machine
#define BUZZER_DURATION 50
int buzzer_state = LOW;

// calibration settings for our light readings
int lowPhotoRead;
int highPhotoRead;

// calibration settings for our soil moisture readings
int lowSoilRead;
int highSoilRead;

char ssid[] = "UCInet Mobile Access";    // your network SSID (name) 
char pass[] = ""; // your network password (use for WPA, or use as key for WEP)

//API Key for OpenWeatherMaps
String apiKey = "a185eb5a73d0a555a95d401c79d648b4";

//city and country code for Weather API (different info for different query types)
String city = "Irvine"; 
String countryCode = "US"; 
String cityCode = "5359777";
int jsonHourPos = 7;	//stores what 3 hour increment we want to gather data from (8th/24hrs)

//timing for weather server requests
unsigned long prev_time = 0;
unsigned long timer_delay = 10000;
unsigned long update_delay = 22000;

//server data values
double fhrServTemp, humServ;

//contains the offsets our server info will determine
double offset = 0.0;

//holds the json data in string
String jsonReq;

String getRequest(const char* serverName) {
	WiFiClient client;
	HTTPClient http;

	//query the API server
	http.begin(client, serverName);

	//Send HTTP get request 
	int respCode = http.GET();

	//contains the json returned
	String payload = "{}";

	//make sure response is valid
	if (respCode >= 200 || respCode <= 299) {
		Serial.print("OpenWeatherAPI Response Code: ");
		Serial.println(respCode);
		payload = http.getString();
	}
	else {
		Serial.print("Error code: ");
		Serial.println(respCode);
	}
	Serial.println("\n");
	http.end();

	return payload;
}

void lightCalibration() {
	//store the start and end time
	int timing = 0;

	//read in the lowest brightness values and store, wait 5s, and read the high brightness value
	lowPhotoRead = INT_MAX;
	highPhotoRead = INT_MIN;
	
	//store the low and high values 
	while(timing <= CALIBRATION_PERIOD_MS) {
		int readValue = analogRead(PHOTO_RST_PIN);

		lowPhotoRead = (readValue < lowPhotoRead) ? readValue : lowPhotoRead;
		highPhotoRead = (readValue > highPhotoRead) ? readValue : highPhotoRead;

		//print the calibration results to the serial monitor
		Serial.print("Progress: ");
		Serial.print(timing);
		Serial.println(" ms / 10000 ms ");
		Serial.print("Low photo reading: ");
		Serial.println(lowPhotoRead);
		Serial.print("High photo reading: ");
		Serial.println(highPhotoRead);
		Serial.println();

		delay(CALIBRATION_DELAY_MS);
		timing += CALIBRATION_DELAY_MS;
	}
}	

void soilCalibration() {
	//store the start and end time
	int timing = 0;
	
	//read in the lowest brightness values and store, wait 5s, and read the high brightness value
	lowSoilRead = INT_MAX;
	highSoilRead = INT_MIN;
	
	//store the low and high values 
	while(timing <= CALIBRATION_PERIOD_MS) {
		int readValue = analogRead(SOIL_MOIST_PIN);

		lowSoilRead = (readValue < lowSoilRead) ? readValue : lowSoilRead;
		highSoilRead = (readValue > highSoilRead) ? readValue : highSoilRead;

		//print the calibration results to the serial monitor
		Serial.print("Progress: ");
		Serial.print(timing);
		Serial.println(" ms / 10000 ms ");
		Serial.print("Low soil reading: ");
		Serial.println(lowSoilRead);
		Serial.print("High soil reading: ");
		Serial.println(highSoilRead);
		Serial.println();

		delay(CALIBRATION_DELAY_MS);
		timing += CALIBRATION_DELAY_MS;
	}
}

void setup() {
	//do all the pinmode initialization steps
	pinMode(PHOTO_RST_PIN, INPUT);
	pinMode(SOIL_MOIST_PIN, INPUT);
	pinMode(BUZZER_PIN, OUTPUT);

	//initialize all the LED pins
	pinMode(SOILHUM_LIGHT_PIN, OUTPUT);
	pinMode(SUNLIGHT_LIGHT_PIN, OUTPUT);
	pinMode(TEMP_LIGHT_PIN, OUTPUT);

	//ensure that sensor is properly detected
	Serial.begin(9600);

	// We start by connecting to a WiFi network
	delay(CALIBRATION_DELAY_MS);
	Serial.println();
	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, pass);

	while (WiFi.status() != WL_CONNECTED) {
		delay(CALIBRATION_DELAY_MS);
		Serial.print(".");
	}

	Serial.println("\n");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	Serial.println("MAC address: ");
	Serial.println(WiFi.macAddress());
	Serial.println(CLEAR_STRING);

	//calibrate the AHT sensor
  	if (!sensor.begin()) {
		Serial.println("\nAttempting to detect AHT sensor...");
		while (1) delay(10);
	}
	Serial.println("AHT sensor found...");
	sleep(DIALOGUE_DELAY_SC);

	Serial.println("Calibration will begin momentarily...");
	delay(NOTIFICATION_DELAY);

	//calibrate our sensors
	Serial.println("Calibrating our photo sensor...");
	lightCalibration();
	Serial.println("Photo sensor calibrated...");
	sleep(DIALOGUE_DELAY_SC);
	Serial.println(CLEAR_STRING);


	Serial.println("\nCalibrating our soil sensor...");
	soilCalibration();
	Serial.println("Soil sensor calibrated...");
	sleep(DIALOGUE_DELAY_SC);
	Serial.println(CLEAR_STRING);
}

void loop() {
	// Send an HTTP GET request
	if ((millis() - prev_time) > timer_delay) {
		// Check WiFi connection status
		if (WiFi.status() == WL_CONNECTED) {
			String serverPath = "http://api.openweathermap.org/data/2.5/forecast?q=" + city + "," + countryCode + "&appid=" + apiKey;

			//get the weather data 
			jsonReq = getRequest(serverPath.c_str());

			//parse it into understandable data
			JSONVar weatherInfo = JSON.parse(jsonReq);

			if (JSON.typeof(weatherInfo) == "undefined") {
				Serial.println("Parsing input failed!");
				return;
			}
			//retrieve the 8th timestamp (all timestamps are in 3 hour increments)
			double kelvin = weatherInfo["list"][jsonHourPos]["main"]["temp"];
			fhrServTemp = 1.8 * (kelvin - 273) + 32;
			humServ = weatherInfo["list"][jsonHourPos]["main"]["humidity"];
		}
		else {
		Serial.println("WiFi Disconnected");
		}
	}

	//begin outputting heuristic information
	Serial.println("\n\n_________________________");
  	Serial.println("Environmental sensor data:");
	Serial.println("_________________________");

	//store the value of the reading from the photoresistor
	int photo_reading = analogRead(PHOTO_RST_PIN);
	int soil_reading = analogRead(SOIL_MOIST_PIN);

	//map it to a valid photoreading degree value
	photo_reading = map(photo_reading, lowPhotoRead, highPhotoRead, FLOOR_VAL, MAX_MAP_VAL);
	soil_reading = map(soil_reading, lowSoilRead, highSoilRead, FLOOR_VAL, MAX_MAP_VAL);

	//constrain the values between 0 - 100
	photo_reading = constrain(photo_reading, FLOOR_VAL, MAX_MAP_VAL);
	soil_reading = constrain(soil_reading, FLOOR_VAL, MAX_MAP_VAL);

	sensors_event_t humidity, temp;		//stores our data values;
	sensor.getEvent(&humidity, &temp); // populate temp and humidity objects with fresh data

	//store the temperature and humidity
	float temp_cel = temp.temperature;
	float temp_fhr = temp.temperature * 1.8 + 32.0;

	//contains normalized soil humidity reading
	int new_soil_reading = std::abs(soil_reading - 100);

	//the offset to the threshold will be calculated by averaging the current temp and the one 24hours from now
	double tempDiff = fhrServTemp - temp_fhr;
	offset = (tempDiff) / 2.0;

	//output temperature readings
	Serial.print("Air Temperature: "); 
	Serial.print(temp_fhr);
	Serial.println("째 F");

	Serial.print("Adjusted Air Temperature (w/API info): ");
	Serial.print(temp_fhr + offset);
	Serial.println("째 F");

	Serial.print("Air Temperature: "); 
	Serial.print(temp_cel);
	Serial.println("째 C");

	Serial.print("Photoresistor Reading: ");
	Serial.print(photo_reading);
	Serial.println("% sunlight");

	Serial.print("Soil Moisture Reading: ");
	Serial.print(new_soil_reading);
	Serial.println("% soil humidity");

	Serial.print("OpenWeatherAPI ");
	Serial.print((jsonHourPos + 1) * 3);
	Serial.print("hr Temperature: ");
	Serial.print(fhrServTemp);
	Serial.println("째 F");

	Serial.print("OpenWeatherAPI ");
	Serial.print((jsonHourPos + 1) * 3);
	Serial.print("hr Humidity: ");
	Serial.print(humServ);
	Serial.println(" % air humidity");
	Serial.println("_________________________");

	Serial.println("Status Report: ");
	Serial.println("_ _ _ _ _ _ _ _ _ _ _ _ _");

	//use future data to affect the thresholds
	if(tempDiff < LOWER_CHANGE_THRESH && tempDiff > 0) {
		Serial.print("There will be a minor increase in temperature in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}
	else if(tempDiff < MED_CHANGE_THRESH && tempDiff > 0) {
		Serial.print("There will be a moderate increase in temperature in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}
	else if(tempDiff < HIGH_CHANGE_THRESH && tempDiff > 0) {
		Serial.print("There will be a substantial increase in temperature in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}
	else if(tempDiff > (-1 * LOWER_CHANGE_THRESH) && tempDiff < 0) {
		Serial.print("There will be a minor decrease in temperature in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}
	else if(tempDiff > (-1 * MED_CHANGE_THRESH) && tempDiff < 0) {
		Serial.print("There will be a moderate decrease in temperature in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}
	else if(tempDiff > (-1 * HIGH_CHANGE_THRESH) && tempDiff < 0) {
		Serial.print("There will be a substantial decrease in temperature in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}

	//use weather api humidity to output information on air humidity
	if(humServ < LOWER_AIR_HUM_THRESH) {
		Serial.print("The air will be very dry in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}
	else if(humServ > UPPER_AIR_HUM_THRESH) {
		Serial.print("The air will be very humid in the next ");
		Serial.print((jsonHourPos + 1) * 3);
		Serial.println(" hours...");
	}

	//temperature state machine
	if(temp_fhr + offset > REL_TEMP_CEIL) {
		Serial.println("The temperature is too warm...");
	
		//turn LED on
		digitalWrite(TEMP_LIGHT_PIN, HIGH);

		//rattle the buzzer    
		if(buzzer_state != LOW) {
			buzzer_state = LOW;
			noTone(BUZZER_PIN);        
		}
		else if(buzzer_state == LOW) {
			buzzer_state = TEMP_DCB;
			tone(BUZZER_PIN, TEMP_DCB, BUZZER_DURATION);
		}
	}
	else if(temp_fhr + offset < REL_TEMP_FLOOR) {
		Serial.println("The temperature is too cold...");
	
		//turn LED on
		digitalWrite(TEMP_LIGHT_PIN, HIGH);

		//rattle the buzzer    
		if(buzzer_state != LOW) {
			buzzer_state = LOW;
			noTone(BUZZER_PIN);        
		}
		else if(buzzer_state == LOW) {
			buzzer_state = TEMP_DCB;
			tone(BUZZER_PIN, TEMP_DCB, BUZZER_DURATION);
		}
	}
	else {
		digitalWrite(TEMP_LIGHT_PIN, LOW);
	}
	buzzer_state = LOW;
	noTone(BUZZER_PIN);

	//soil humidity state machine
	if(new_soil_reading > REL_HUM_CEIL) {
		Serial.println("The soil is too moist...");
	
		//turn LED on
		digitalWrite(SOILHUM_LIGHT_PIN, HIGH);

		//rattle the buzzer    
		if(buzzer_state != LOW) {
			buzzer_state = LOW;
			noTone(BUZZER_PIN);        
		}
		else if(buzzer_state == LOW) {
			buzzer_state = HUM_DCB;
			tone(BUZZER_PIN, HUM_DCB, BUZZER_DURATION);
		}
	}
	else if(new_soil_reading < REL_HUM_FLOOR) {
		Serial.println("The soil is too dry...");
	
		//turn LED on
		digitalWrite(SOILHUM_LIGHT_PIN, HIGH);

		//rattle the buzzer    
		if(buzzer_state != LOW) {
			buzzer_state = LOW;
			noTone(BUZZER_PIN);        
		}
		else if(buzzer_state == LOW) {
			buzzer_state = HUM_DCB;
			tone(BUZZER_PIN, HUM_DCB, BUZZER_DURATION);
		}
	}
	else {
		digitalWrite(SOILHUM_LIGHT_PIN, LOW);
	}
	buzzer_state = LOW;
	noTone(BUZZER_PIN);

	//light state machine
	if(photo_reading > REL_SOLAR_CEIL) {
		Serial.print("There is too much sunlight...");
	
		//turn LED on
		digitalWrite(SUNLIGHT_LIGHT_PIN, HIGH);

		//rattle the buzzer    
		if(buzzer_state != LOW) {
			buzzer_state = LOW;
			noTone(BUZZER_PIN);        
		}
		else if(buzzer_state == LOW) {
			buzzer_state = SUNLIGHT_DCB;
			tone(BUZZER_PIN, SUNLIGHT_DCB, BUZZER_DURATION);
		}
	}
	else if(photo_reading < REL_SOLAR_FLOOR) {
		Serial.print("There is too little sunlight...");
	
		//turn LED on
		digitalWrite(SUNLIGHT_LIGHT_PIN, HIGH);

		//rattle the buzzer    
		if(buzzer_state != LOW) {
			buzzer_state = LOW;
			noTone(BUZZER_PIN);        
		}
		else if(buzzer_state == LOW) {
			buzzer_state = SUNLIGHT_DCB;
			tone(BUZZER_PIN, SUNLIGHT_DCB, BUZZER_DURATION);
		}
	}
	else {
		digitalWrite(SUNLIGHT_LIGHT_PIN, LOW);
	}
	buzzer_state = LOW;
	noTone(BUZZER_PIN);

	//properly manage our tracking variables and clear the offset
	prev_time = millis();
	offset = 0.0;
	Serial.println(CLEAR_STRING);
	sleep(READ_DELAY_SC);
}

/*	Initialization File:
==============================================================
; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    arduino-libraries/Arduino_JSON@^0.1.0
	madhephaestus/ESP32Servo@^0.11.0
	adafruit/Adafruit AHTX0@^2.0.1
	adafruit/Adafruit BusIO@^1.11.6
*/
