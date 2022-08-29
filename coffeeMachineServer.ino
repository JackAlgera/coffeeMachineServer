#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <arduino-timer.h>
#include "time.h"
#include <TimeLib.h>
#include <HTTPClient.h>

// Replace with your network credentials
const char* ssid = "Livebox-GahlouAndFlokkie";
const char* password = "G@hlouAndFlokki3";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Global variables
String request;
String currentState = "";
Timer<20, millis, int> timer;

// GPIO pins to control relays
const int makeCoffeeRelayPin  = 27;
const int turnOnRelayPin      = 26;
bool isPinOnHigh              = false;

String dateToMakeCoffee = "";

// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time, trying again in 1s");
    delay(1000);
  }
  time(&now);
  return now + 7200;
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Coffee machine controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    form { display: inline; }
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    spaced { margin: 20px }
    .button { background-color: #4CAF50; border: none; color: white; padding: 8px 16px; text-decoration: none; font-size: 20px; margin: 20px; cursor: pointer; }
  </style>
</head>
<body>
  <h1>Coffee controller</h1>
  <p><h3>Currently: %STATEPLACEHOLDER%</h3>
  <form action="/makeCoffee">
    <div class="spaced">In how many days: <input type="number" name="inDays"></div>
    <div class="spaced">Hours (HH): <input type="number" name="hours"></div>
    <div class="spaced">Minutes (MM): <input type="number" name="minutes"></div>
    <input type="submit" class="button" value="Submit">
  </form><br>
  <form action="/cancel">
    <input type="submit" class="button" value="Cancel">
  </form><br>
  <p><h3>Will make coffee on</h3></p>
  <p><h3>%DATETOMAKECOFFEEPLACEHOLDER%</h3></p>
</body>
</html>
)rawliteral";

String processor(const String& var) {
  //Serial.println(var);
  if (var == "STATEPLACEHOLDER") {
    return currentState;
  }
  if (var == "DATETOMAKECOFFEEPLACEHOLDER") {
    return dateToMakeCoffee;
  }
  return String();
}

void setup() {
  Serial.begin(115200);
  // Initialize the output variables as outputs
  pinMode(turnOnRelayPin, OUTPUT);
  pinMode(makeCoffeeRelayPin, OUTPUT);
  // Set outputs to LOW
  digitalWrite(turnOnRelayPin, LOW);
  digitalWrite(makeCoffeeRelayPin, LOW);

  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Send a GET request to <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
  server.on("/makeCoffee", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int inDays = request->getParam("inDays")->value().toInt();
    int hours = request->getParam("hours")->value().toInt();
    int minutes = request->getParam("minutes")->value().toInt();
    
    makeCoffee(inDays, hours, minutes);
    
    request->redirect("/");
  });

  server.on("/cancel", HTTP_GET, [] (AsyncWebServerRequest *request) {
    printMessage(0);
    timer.cancel();
     
    request->redirect("/");
  });
  
  server.begin();
  printMessage(0);

  // Init and get the time
  const char* ntpServer = "pool.ntp.org";
  configTime(0, 0, ntpServer);
  setTime(getTime());
}

void loop() {
  timer.tick();
}

bool printMessage(int val) {
  if (val == 0) {
    currentState = "Doing nothing...";
    dateToMakeCoffee = "";
  }
  if (val == 1) {
    currentState = "Turning on machine...";
  }
  if (val == 2) {
    currentState = "Making coffee...";
  }
  if (val == 3) {
    currentState = "Waiting to make coffee...";
  }
  return false;
}

void makeCoffee(int inDays, int hours, int minutes) {
  timer.cancel();
  
  long unsigned delayToMakeCoffee = getEpochDateFor(inDays, hours, minutes) - now();
  dateToMakeCoffee = generateDatetimeFromTimestamp(now() + delayToMakeCoffee);
  
  printMessage(3);
  triggerClick(turnOnRelayPin, delayToMakeCoffee, 1);
  triggerClick(makeCoffeeRelayPin, delayToMakeCoffee + 35, 2);
  timer.in((delayToMakeCoffee + 70) * 1000, printMessage, 0);
}

void triggerClick(int pin, int delayInSeconds, int messageVal) {
  timer.in(delayInSeconds * 1000, printMessage, messageVal);
  timer.in(delayInSeconds * 1000, switchPin, pin);
  timer.in(delayInSeconds * 1000 + 500, switchPin, pin);
}

bool switchPin(int pin) {
  if (!isPinOnHigh) {
    digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, LOW);
  }
  isPinOnHigh = !isPinOnHigh;
  return false;
}

// ------ Time ------

String timeServerUrl = "https://showcase.api.linx.twenty57.net/UnixTime/tounix";

long getEpochDateFor(int days, int hours, int minutes) {
  HTTPClient http;
  long unsigned payload = -1;

  String dateUri = timeServerUrl + "?date=" + generateDatetime(days, hours, minutes);
  Serial.println(dateUri);
  
  // Your Domain name with URL path or IP address with path
  http.begin(dateUri.c_str());

  int httpResponseCode = http.GET();

  if (httpResponseCode>0) {
    String response = http.getString();
    response.replace("\"", "");
    payload = atol(response.c_str());
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  
  // Free resources
  http.end();

  return payload;
}

String generateDatetimeFromTimestamp(long timestampDelay) {
  String date = "";

  date.concat(year(timestampDelay));
  date.concat("/");

  if (month(timestampDelay) < 10) {
    date += "0";
  }
  date.concat(month(timestampDelay));
  date.concat("/");

  if (day(timestampDelay) < 10) {
    date += "0";
  }
  date.concat(day(timestampDelay));
  date.concat(" ");

  if (hour(timestampDelay) < 10) {
    date += "0";
  }
  date.concat(hour(timestampDelay));
  date.concat(":");

  if (minute(timestampDelay) < 10) {
    date += "0";
  }
  date.concat(minute(timestampDelay));
  date.concat(":");

  if (second(timestampDelay) < 10) {
    date += "0";
  }
  date.concat(second(timestampDelay));

  return date;
}

String generateDatetime(int days, int hours, int minutes) {
  String date = "";
  long currentTimestamp = now() + 60 * 60 * 24 * days;

  date.concat(year(currentTimestamp));
  date.concat("/");

  if (month(currentTimestamp) < 10) {
    date += "0";
  }
  date.concat(month(currentTimestamp));
  date.concat("/");

  if (day(currentTimestamp) < 10) {
    date += "0";
  }
  date.concat(day(currentTimestamp));
  date.concat("%20");

  if (hours < 10) {
    date += "0";
  }
  date.concat(hours);
  date.concat(":");

  if (minutes < 10) {
    date += "0";
  }
  date.concat(minutes);
  date.concat(":");

  if (second(currentTimestamp) < 10) {
    date += "0";
  }
  date.concat(second(currentTimestamp));

  return date;
}
