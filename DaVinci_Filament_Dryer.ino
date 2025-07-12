#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <Servo.h>

// --- Configuration ---
const char* ssid = "ITSME"; // <<-- REPLACE with your WiFi SSID
const char* password = "NOTYOU"; // <<-- REPLACE with your WiFi password

// Pin Definitions - NodeMCU D-series labels map to GPIO numbers
#define DHT_PIN D7 // GPIO13 for DHT11 Data Line
#define DHT_TYPE DHT11 // DHT Type
#define MQ2_DOUT_PIN D1 // GPIO5 for MQ2 Digital Output
#define MQ2_AOUT_PIN A0 // Analog Input for MQ2 Analog Output (internal divider helps, external divider reduces range for 0-3.3V)
#define HEATER_SERVO_PIN D4 // GPIO2 for Servo Signal Line

// Sensor Reading Intervals
const long READ_SENSOR_INTERVAL = 2000; // Read sensors every 2 seconds

// Heater Control Settings
float desiredTemperature = 22.0; // Initial desired temperature in Celsius
const float HYSTERESIS = 1.0; // 1.0°C hysteresis to prevent rapid switching
enum HeaterMode { OFF, ON, AUTO };
HeaterMode heaterMode = OFF; // Initial heater mode

// --- Global Variables ---
DHT dht(DHT_PIN, DHT_TYPE);
Servo heaterServo;
ESP8266WebServer server(80);

float currentTemperature = 0.0;
float currentHumidity = 0.0;
int mq2AnalogValue = 0;
bool mq2DigitalThreshold = false;
int currentServoPosition = -1; // -1 indicates unknown, for initial position

unsigned long lastSensorReadTime = 0;

// --- Function Prototypes ---
void setupWiFi();
void handleRoot();
void handleHeaterControl();
String getSensorDataJson();
void readSensors();
void controlHeaterServo();
String generateHtmlPage();
float celsiusToFahrenheit(float celsius); // Prototype for new helper function
float fahrenheitToCelsius(float fahrenheit); // Prototype for new helper function
void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("\nStarting Home Monitoring System...");

    // Initialize Pins
    pinMode(MQ2_DOUT_PIN, INPUT); // MQ2 Digital Output
    // DHT_PIN setup handled by dht.begin()
    // HEATER_SERVO_PIN setup handled by heaterServo.attach()

    // Attach Servo
    heaterServo.attach(HEATER_SERVO_PIN);
    heaterServo.write(0); // Initial servo position to OFF (0 degrees)
    currentServoPosition = 0;

    // Initialize DHT sensor
    dht.begin(); // Initializes DHT sensor on the defined pin

    setupWiFi(); // Connect to Wi-Fi

    // Web Server Routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/control", HTTP_GET, handleHeaterControl); // Handles heater mode and temp setting
    server.on("/sensordata", HTTP_GET, []() { // Route for dynamic data refresh
        server.send(200, "application/json", getSensorDataJson());
    });
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient(); // Handle incoming web requests

    unsigned long currentTime = millis();
    if (currentTime - lastSensorReadTime >= READ_SENSOR_INTERVAL) {
        readSensors();
        controlHeaterServo(); // Evaluate heater control logic in AUTO mode
        lastSensorReadTime = currentTime;
    }

    // Placeholder for MQ2 digital detection safety action
    if (mq2DigitalThreshold) {
        Serial.println("!!! Gas/Smoke Detected via Digital Output (Dout) !!!");
        // Implement safety action here (e.g., sound an alarm, send notification, trigger relay)
    }
}
// --- Function Implementations ---

void setupWiFi() {
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password); // Connects to the WiFi network

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP()); // Prints the assigned IP address
}

// Helper function to convert Celsius to Fahrenheit
float celsiusToFahrenheit(float celsius) {
    return (celsius * 1.8) + 32;
}

// Helper function to convert Fahrenheit to Celsius
float fahrenheitToCelsius(float fahrenheit) {
    return (fahrenheit - 32) / 1.8;
}

void readSensors() {
    // Read Temperature and Humidity
    currentTemperature = dht.readTemperature();
    currentHumidity = dht.readHumidity();

    if (isnan(currentTemperature) || isnan(currentHumidity)) {
        Serial.println("Failed to read from DHT sensor!"); // Error handling
    } else {
        Serial.print("Temperature (C): ");
        Serial.print(currentTemperature);
        Serial.print(" °C | Temperature (F): ");
        Serial.print(celsiusToFahrenheit(currentTemperature));
        Serial.print(" °F | Humidity: ");
        Serial.print(currentHumidity);
        Serial.println(" %");
    }

    // Read MQ2 Analog Value
    mq2AnalogValue = analogRead(MQ2_AOUT_PIN); // Reads the scaled value via voltage divider
    Serial.print("MQ2 Analog: ");
    Serial.println(mq2AnalogValue);

    // Read MQ2 Digital Value
    mq2DigitalThreshold = digitalRead(MQ2_DOUT_PIN);
    Serial.print("MQ2 Digital: ");
    Serial.println(mq2DigitalThreshold ? "Detected" : "Clear");
}

void controlHeaterServo() {
    int targetPosition = currentServoPosition; // Default to current position to prevent unnecessary writes

    switch (heaterMode) {
        case OFF:
            targetPosition = 0; // OFF at 0 degrees
            break;
        case ON:
            targetPosition = 180; // ON at 180 degrees
            break;
        case AUTO:
            if (!isnan(currentTemperature)) { // Ensure temperature is valid for decision
                if (currentTemperature < desiredTemperature - HYSTERESIS) {
                    targetPosition = 180; // Turn ON if below threshold
                } else if (currentTemperature > desiredTemperature + HYSTERESIS) {
                    targetPosition = 0; // Turn OFF if above threshold
                }
            } else {
                Serial.println("Temperature unavailable for AUTO mode decision.");
            }
            break;
    }

    if (targetPosition != currentServoPosition) { // Only move if target is different
        heaterServo.write(targetPosition);
        currentServoPosition = targetPosition;
        Serial.print("Servo moved to: ");
        Serial.println(currentServoPosition);
    }
}

String getSensorDataJson() {
    String json = "{";
    json += "\"temperatureC\": " + String(currentTemperature, 2) + ",";
    json += "\"temperatureF\": " + String(celsiusToFahrenheit(currentTemperature), 2) + ",";
    json += "\"humidity\": " + String(currentHumidity, 2) + ",";
    json += "\"mq2Analog\": " + String(mq2AnalogValue) + ",";
    json += "\"mq2Digital\": " + String(mq2DigitalThreshold ? "true" : "false") + ",";
    json += String("\"heaterMode\": \"") + (heaterMode == OFF ? "OFF" : (heaterMode == ON ? "ON" : "AUTO")) + "\",";
    json += "\"desiredTemperatureC\": " + String(desiredTemperature, 1) + ","; // Changed key name for clarity
    json += "\"desiredTemperatureF\": " + String(celsiusToFahrenheit(desiredTemperature), 1) + ","; // Added Fahrenheit desired temp
    json += "\"servoPosition\": " + String(currentServoPosition);
    json += "}";
    return json;
}

void handleRoot() {
    server.send(200, "text/html; charset=UTF-8", generateHtmlPage()); // <-- Set Content-Type header with UTF-8
}

void handleHeaterControl() {
    if (server.hasArg("mode")) { // Check for 'mode' parameter
        String mode = server.arg("mode");
        if (mode == "off") {
            heaterMode = OFF;
            Serial.println("Heater mode set to OFF");
        } else if (mode == "on") {
            heaterMode = ON;
            Serial.println("Heater mode set to ON");
        } else if (mode == "auto") {
            heaterMode = AUTO;
            Serial.println("Heater mode set to AUTO");
        }
    }
    // Handle temperature setting in Celsius
    if (server.hasArg("tempC")) { // Changed parameter name to indicate Celsius
        float newTempC = server.arg("tempC").toFloat();
        if (newTempC >= 0.0 && newTempC <= 150.0) { // Valid range 0-150C
            desiredTemperature = newTempC;
            Serial.print("Desired temperature set to: ");
            Serial.print(desiredTemperature);
            Serial.println(" °C");
        } else {
            Serial.println("Invalid Celsius temperature value received.");
        }
    }
    // Handle temperature setting in Fahrenheit
    if (server.hasArg("tempF")) { // Added parameter for Fahrenheit
        float newTempF = server.arg("tempF").toFloat();
        // Convert F to C before storing, then validate in C
        float newTempC = fahrenheitToCelsius(newTempF);
        if (newTempC >= 0.0 && newTempC <= 150.0) { // Validate in C equivalent
            desiredTemperature = newTempC;
            Serial.print("Desired temperature set to: ");
            Serial.print(newTempF);
            Serial.print(" °F (");
            Serial.print(desiredTemperature);
            Serial.println(" °C)");
        } else {
            Serial.println("Invalid Fahrenheit temperature value received.");
        }
    }
    // Redirect back to the root page after control action
    server.sendHeader("Location", "/", true); // Send Location header to redirect
    server.send(302, "text/plain", ""); // Send 302 Found status with empty body
}

String generateHtmlPage() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>Home Monitor</title>";
    html += "<meta charset=\"UTF-8\">"; // <-- Add meta charset tag for correct character display
    html += "<meta http-equiv='refresh' content='30'>"; // Auto-refresh every 30 seconds
    html += "<style>body { font-family: Arial, sans-serif; margin: 20px; }";
    html += ".container { max-width: 600px; margin: auto; padding: 20px; border: 1px solid #ddd; border-radius: 8px; box-shadow: 2px 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { text-align: center; color: #333; }";
    html += "p { font-size: 1.1em; line-height: 1.6; }";
    html += ".sensor-data span { font-weight: bold; color: #007bff; }";
    html += ".controls button, .controls input[type='submit'] { background-color: #28a745; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; font-size: 1em; }";
    html += ".controls button:hover, .controls input[type='submit']{ background-color: #218838; }";
    html += ".controls input[type='number'] { padding: 8px; border: 1px solid #ccc; border-radius: 4px; width: 80px; }";
    html += "</style></head><body><div class='container'><h1>Home Monitoring & Heater Control</h1>";

    html += "<p>Current Status:</p><ul class='sensor-data'>";
    html += "<li>Temperature: <span>" + String(currentTemperature, 2) + " °C / " + String(celsiusToFahrenheit(currentTemperature), 2) + " °F</span></li>";
    html += "<li>Humidity: <span>" + String(currentHumidity, 2) + " %</span></li>";
    html += "<li>Gas Sensor (Analog): <span>" + String(mq2AnalogValue) + "</span></li>";
    html += String("<li>Gas Sensor (Digital): <span>") + (mq2DigitalThreshold ? "Detected" : "Clear") + "</span></li>";
    html += String("<li>Heater Mode: <span>") + (heaterMode == OFF ? "OFF" : (heaterMode == ON ? "ON" : "AUTO")) + "</span></li>";
    // Servo position now reflects the Heater Mode
    html += "<li>Heater Position: <span>" + String(currentServoPosition) + "° (" + (heaterMode == OFF ? "OFF" : (heaterMode == ON ? "ON" : "AUTO")) + ")</span></li>";
    html += "</ul>";

    html += "<p>Heater Controls:</p><div class='controls'>";
    html += "<button onclick=\"window.location.href='/control?mode=off'\">OFF</button>";
    html += "<button onclick=\"window.location.href='/control?mode=on'\">ON</button>";
    html += "<button onclick=\"window.location.href='/control?mode=auto'\">AUTO</button><br>";

    // Display Current Set Temperature separately
    html += "<p>Current Desired Temp: <span>" + String(desiredTemperature, 1) + " °C / " + String(celsiusToFahrenheit(desiredTemperature), 1) + " °F</span></p>";

    // Input for setting desired temperature in Celsius
    html += "<form action='/control' method='get'>";
    html += "Set New Temp (C): <input type='number' name='tempC' placeholder='Enter new temp' step='0.1' min='0' max='150'>"; // Removed 'value' attribute
    html += "<input type='submit' value='Set C'>";
    html += "</form>";

    // Input for setting desired temperature in Fahrenheit
    html += "<form action='/control' method='get'>";
    html += "Set New Temp (F): <input type='number' name='tempF' placeholder='Enter new temp' step='0.1' min='32' max='302'>"; // Removed 'value' attribute
    html += "<input type='submit' value='Set F'>";
    html += "</form>";

    html += "</div></div></body></html>";
    return html;
}
