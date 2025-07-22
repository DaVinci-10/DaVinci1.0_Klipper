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
void streamHtmlPage(WiFiClient& client); // Modified to take WiFiClient reference
float celsiusToFahrenheit(float celsius); // Prototype for new helper function
float fahrenheitToCelsius(float fahrenheit); // Prototype for new helper function

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("\nStarting DaVinci Filament Dryer System...");

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
    Serial.print("MQ2 Analog (ppm): ");
    Serial.println(mq2AnalogValue);

    // Read MQ2 Digital Value
    mq2DigitalThreshold = digitalRead(MQ2_DOUT_PIN);
    Serial.print("MQ2 Digital: ");
    Serial.println(mq2DigitalThreshold ? "Smoke or VOC's Detected!" : "No Smoke or VOC's Detected");
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
    // Get the client connection object
    WiFiClient client = server.client();

    // Send HTTP headers
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=UTF-8");
    client.println("Connection: close"); // Indicate that the connection will be closed after this response
    client.println(); // Blank line separates headers from content

    // Stream the HTML content
    streamHtmlPage(client);
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
            Serial.print(" °C)");
            Serial.println();
        } else {
            Serial.println("Invalid Fahrenheit temperature value received.");
        }
    }
    // Send a simple OK response.
    server.send(200, "text/plain", "OK");
}

// Function to stream HTML content directly to the client
void streamHtmlPage(WiFiClient& client) {
    // Start HTML document
    client.println("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>Filament Monitor</title>");
    client.println("<meta charset=\"UTF-8\">");
    client.println("<style>");
    client.println("body { font-family: 'Inter', Arial, sans-serif; margin: 20px; background-color: #f4f7f6; color: #333; }");
    client.println(".container { max-width: 600px; margin: auto; padding: 25px; border: 1px solid #e0e0e0; border-radius: 12px; box-shadow: 0 4px 12px rgba(0,0,0,0.08); background-color: #ffffff; }");
    client.println("h1 { text-align: center; color: #2c3e50; margin-bottom: 25px; }");
    client.println("p { font-size: 1.1em; line-height: 1.6; margin-bottom: 10px; }");
    client.println(".sensor-data ul { list-style: none; padding: 0; margin-top: 15px; }");
    client.println(".sensor-data li { background-color: #e9f5f5; padding: 10px 15px; margin-bottom: 8px; border-radius: 8px; display: flex; justify-content: space-between; align-items: center; }");
    client.println(".sensor-data li strong { color: #34495e; font-weight: 600; }");
    client.println(".sensor-data li span { font-weight: bold; color: #2a9d8f; font-size: 1.1em; }");
    client.println(".controls { text-align: center; margin-top: 30px; padding-top: 20px; border-top: 1px solid #e0e0e0; }");
    client.println(".controls button, .controls input[type='submit'] { ");
    client.println("background-color: #4CAF50; color: white; padding: 12px 20px; border: none; border-radius: 8px; cursor: pointer; margin: 8px; font-size: 1.05em; transition: background-color 0.3s ease, transform 0.2s ease; box-shadow: 0 4px 8px rgba(0,0,0,0.1); ");
    client.println("}");
    client.println(".controls button:hover, .controls input[type='submit']:hover { background-color: #45a049; transform: translateY(-2px); }");
    client.println(".controls button:active, .controls input[type='submit']:active { transform: translateY(0); box-shadow: 0 2px 4px rgba(0,0,0,0.1); }");
    client.println(".controls input[type='number'] { padding: 10px; border: 1px solid #ccc; border-radius: 8px; width: 100px; margin-right: 10px; font-size: 1em; }");
    client.println(".current-temp-display { background-color: #f0f8ff; padding: 15px; border-radius: 8px; margin-top: 20px; text-align: center; font-size: 1.1em; color: #2c3e50; }");
    client.println(".current-temp-display span { font-weight: bold; color: #e76f51; font-size: 1.2em; }");
    client.println(".last-updated { font-size: 0.9em; color: #7f8c8d; text-align: center; margin-top: 20px; }");
    client.println("</style></head><body><div class='container'><h1>DaVinci Filament Dryer - Monitoring & Heater Control</h1>");
    client.println("<p>Current Status:</p><ul class='sensor-data'>");
    client.println("<li><strong>Temperature:</strong> <span id='temperature'>--</span></li>");
    client.println("<li><strong>Humidity:</strong> <span id='humidity'>--</span></li>");
    client.println("<li><strong>Gas Sensor (Analog):</strong> <span id='mq2Analog'>--</span></li>");
    client.println("<li><strong>Gas Sensor (Digital):</strong> <span id='mq2Digital'>--</span></li>");
    client.println("<li><strong>Heater Mode:</strong> <span id='heaterMode'>--</span></li>");
    client.println("<li><strong>Heater Position:</strong> <span id='servoPosition'>--</span></li>");
    client.println("</ul>");

    client.println("<div class='current-temp-display'>");
    client.println("Current Desired Temp: <span id='desiredTemperature'>--</span>");
    client.println("</div>");

    client.println("<p class='last-updated'>Last Updated: <span id='lastUpdated'>--</span></p>");

    client.println("<p>Heater Controls:</p><div class='controls'>");
    client.println("<button onclick=\"sendControl('mode', 'off')\">OFF</button>");
    client.println("<button onclick=\"sendControl('mode', 'on')\">ON</button>");
    client.println("<button onclick=\"sendControl('mode', 'auto')\">AUTO</button><br>");

    client.println("<form onsubmit=\"sendControl('tempC', document.getElementById('tempCInput').value); return false;\">");
    client.println("Set New Temp (C): <input type='number' id='tempCInput' placeholder='Enter new temp' step='0.1' min='0' max='150'>");
    client.println("<input type='submit' value='Set C'>");
    client.println("</form>");

    client.println("<form onsubmit=\"sendControl('tempF', document.getElementById('tempFInput').value); return false;\">");
    client.println("Set New Temp (F): <input type='number' id='tempFInput' placeholder='Enter new temp' step='0.1' min='32' max='302'>");
    client.println("<input type='submit' value='Set F'>");
    client.println("</form>");

    client.println("</div></div>"); // Close controls and container divs

    // --- JavaScript for AJAX updates ---
    client.println("<script>");
    client.println("console.log('Script loaded.');");
    client.println("function fetchSensorData() {");
    client.println("  console.log('Attempting to fetch sensor data...');");
    client.println("  fetch('/sensordata')");
    client.println("    .then(response => {");
    client.println("      console.log('Fetch response received:', response.status, response.statusText);");
    client.println("      if (!response.ok) {");
    client.println("        throw new Error(`HTTP error! status: ${response.status}`);");
    client.println("      }");
    client.println("      return response.json();");
    client.println("    })");
    client.println("    .then(data => {");
    client.println("      console.log('Sensor data received:', data);");
    client.println("      document.getElementById('temperature').innerText = data.temperatureC.toFixed(2) + ' °C / ' + data.temperatureF.toFixed(2) + ' °F';");
    client.println("      document.getElementById('humidity').innerText = data.humidity.toFixed(2) + ' %';");
    client.println("      document.getElementById('mq2Analog').innerText = data.mq2Analog;");
    client.println("      document.getElementById('mq2Digital').innerText = data.mq2Digital === 'true' ? 'Smoke or VOC\\'s Detected!' : 'No Smoke or VOC\\'s Detected';");
    client.println("      document.getElementById('heaterMode').innerText = data.heaterMode;");
    client.println("      document.getElementById('servoPosition').innerText = data.servoPosition + '° (' + data.heaterMode + ')';");
    client.println("      document.getElementById('desiredTemperature').innerText = data.desiredTemperatureC.toFixed(1) + ' °C / ' + data.desiredTemperatureF.toFixed(1) + ' °F';");
    client.println("      document.getElementById('lastUpdated').innerText = new Date().toLocaleTimeString();");
    client.println("    })");
    client.println("    .catch(error => console.error('Error fetching sensor data:', error));");
    client.println("}");

    client.println("function sendControl(param, value) {");
    client.println("  console.log(`Sending control command: ${param}=${value}`);");
    client.println("  fetch(`/control?${param}=${value}`)");
    client.println("    .then(response => {");
    client.println("      if (response.ok) {");
    client.println("        console.log(`Control command sent successfully.`);");
    client.println("        fetchSensorData(); // Refresh data immediately after sending control command");
    client.println("      } else {");
    client.println("        console.error('Failed to send control command. Response status:', response.status);");
    client.println("      }");
    client.println("    })");
    client.println("    .catch(error => console.error('Error sending control command:', error));");
    client.println("}");

    client.println("window.onload = function() {");
    client.println("  console.log('Window loaded. Initial data fetch...');");
    client.println("  fetchSensorData(); // Initial fetch when page loads");
    client.println("  setInterval(fetchSensorData, " + String(READ_SENSOR_INTERVAL) + "); // Fetch data every READ_SENSOR_INTERVAL milliseconds");
    client.println("};");
    client.println("</script>");
    client.println("</body></html>");
    // client.stop() is handled by the server after handleRoot returns
}
