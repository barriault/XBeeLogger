#include <SoftwareSerial.h>
#include <Phant.h>
#include <Wire.h>
#include <Adafruit_AM2315.h>

// Time in ms, where we stop waiting for serial data to come in
// 2s is usually pretty good. Don't go under 1000ms (entering
// command mode takes about 1s).
#define COMMAND_TIMEOUT 2000 // ms
#define DEBUG

// Phant Stuff
String destIP = "54.86.132.254"; // data.sparkfun.com's IP address
String publicKey = "";
String privateKey = "";
Phant phant("data.sparkfun.com", publicKey, privateKey);
const String tempField = "temp";
const String humidityField = "humidity";

// XBee Stuff
const byte XB_RX = 2; // XBee's RX (Din) pin
const byte XB_TX = 3; // XBee's TX (Dout) pin
const int XBEE_BAUD = 9600; // Your XBee's baud (9600 is default)
SoftwareSerial xB(XB_RX, XB_TX); 

// Temp/Humidity Sensor
Adafruit_AM2315 am2315;
float tempVal, humidityVal;

// Phant limits you to 10 seconds between posts. Use this variable
// to limit the update rate (in milliseconds):
const unsigned long UPDATE_RATE = 300000; // 300000ms = 5 minutes
unsigned long lastUpdate = 0; // Keep track of last update time

// LED to show status, fast blinking indicates problem with sensor,
// slow blinking indicates WiFi configuring or problem, on indicates
// WiFi connected.
int led = 13;
bool lastLED = LOW;

void setup()
{
  // We have a 5 second delay to give the shield and XBee module some 
  // time to settle down. Without this things weren't starting up
  // properly unless the reset button was hit after power up.
  delay(5000); 
  
  pinMode(led, OUTPUT);
  digitalWrite(led, lastLED);
  
  // Set up serial ports:
  Serial.begin(9600);
  xB.begin(XBEE_BAUD);
  
  // Set up temp/humidity sensor, blink LED rapidly if sensor problem
  if (! am2315.begin()) {
     while (1) {
       digitalWrite(led, HIGH);
       delay(100);
       digitalWrite(led, LOW);
       delay(100);
     }
  }

  // Set up WiFi network
  Serial.println("Testing network");
  connectWiFi();
  Serial.println("Connected!");
  Serial.print("IP Address: "); printIP(); Serial.println();

  // setupHTTP() will set up the destination address, port, and
  // make sure we're in TCP mode:
  setupHTTP(destIP);
}

void loop()
{
  // If current time is UPDATE_RATE milliseconds greater than
  // the last update rate, send new data.
  if (millis() > (lastUpdate + UPDATE_RATE))
  {
    Serial.print("Sending update...");
    if (sendData())
      Serial.println("SUCCESS!");
    else
      Serial.println("Failed :(");
    lastUpdate = millis();
  }
  
  // In the meanwhile, we'll print data to the serial monitor,
  // just to let the world know our Arduino is still operational:
  readSensors();
  Serial.print("Hum: "); Serial.println(humidityVal);
  Serial.print("Tmp: "); Serial.println(tempVal);
  
  delay(2000);
}

// sendData() makes use of the PHANT LIBRARY to send data to the
// data.sparkfun.com server. We'll use phant.add() to add specific
// parameter and their values to the param list. Then use
// phant.post() to send that data up to the server.
int sendData()
{
  xB.flush(); // Flush data so we get fresh stuff in
  readSensors(); // Get updated values from sensors.
  phant.add(tempField, tempVal);
  phant.add(humidityField, humidityVal);
  // After our PHANT.ADD's we need to PHANT.POST(). The post needs
  // to be sent out the XBee. A simple "print" of that post will
  // take care of it.
  xB.print(phant.post());

  // Check the response to make sure we receive a "200 OK". If 
  // we were good little programmers we'd check the content of
  // the OK response. If we were good little programmers...
  char response[12];
  if (waitForAvailable(12) > 0)
  {
    for (int i=0; i<12; i++)
    {
      response[i] = xB.read();
    }
    if (memcmp(response, "HTTP/1.1 200", 12) == 0)
      return 1;
    else
    {
      Serial.println(response);
      return 0; // Non-200 response
    }
  }
  else // Otherwise timeout, no response from server
    return -1;
}

void readSensors()
{
  humidityVal = am2315.readHumidity();
  tempVal = am2315.readTemperature() * 1.8 + 32;
}

///////////////////////////
// XBee WiFi Setup Stuff //
///////////////////////////
// setupHTTP() sets three important parameters on the XBee:
// 1. Destination IP -- This is the IP address of the server
//    we want to send data to.
// 2. Destination Port -- We'll be sending data over port 80.
//    The standard HTTP port a server listens to.
// 3. IP protocol -- We'll be using TCP (instead of default UDP).
void setupHTTP(String address)
{
  // Enter command mode, wait till we get there.
  while (!commandMode(1))
    ;

  // Set IP (1 - TCP)
  command("ATIP1", 2); // RESP: OK
  // Set DL (destination IP address)
  command("ATDL" + address, 2); // RESP: OK
  // Set DE (0x50 - port 80)
  command("ATDE50", 2); // RESP: OK

  commandMode(0); // Exit command mode when done
}

///////////////
// printIP() //
///////////////
// Simple function that enters command mode, reads the IP and
// prints it to a serial terminal. Then exits command mode.
void printIP()
{
  // Wait till we get into command Mode.
  while (!commandMode(1))
    ;
  // Get rid of any data that may have already been in the
  // serial receive buffer:
  xB.flush();
  // Send the ATMY command. Should at least respond with
  // "0.0.0.0\r" (7 characters):
  command("ATMY", 7);
  // While there are characters to be read, read them and throw
  // them out to the serial monitor.
  while (xB.available() > 0)
  {
    Serial.write(xB.read());
  }
  // Exit command mode:
  commandMode(0);
}

// Assumes you've already configured your XBee module's WiFi settings using X-CTU
// and simply blinks the LED while connecting or if there is a problem. We also
// turn on the LED once connected.
void connectWiFi()
{
  const String CMD_SSID = "ATID";
  const String CMD_ENC = "ATEE";
  const String CMD_PSK = "ATPK";
  // Check if we're connected. If so, sweet! We're done.
  // Otherwise, time to configure some settings, and print
  // some status messages:
  int status;
  while ((status = checkConnect()) != 0)
  {
    // Print a status message. If `status` isn't 0 (indicating
    // "connected"), then it'll be one of these 
    //  (from XBee WiFI user's manual):
    // 0x01 - WiFi transceiver initialization in progress. 
    // 0x02 - WiFi transceiver initialized, but not yet scanning 
    //        for access point. 
    // 0x13 - Disconnecting from access point. 
    // 0x23 – SSID not configured. 
    // 0x24 - Encryption key invalid (either NULL or invalid 
    //        length for WEP) 
    // 0x27 – SSID was found, but join failed. 0x40- Waiting for 
    //        WPA or WPA2 Authentication 
    // 0x41 – Module joined a network and is waiting for IP 
    //        configuration to complete, which usually means it is
    //        waiting for a DHCP provided address. 
    // 0x42 – Module is joined, IP is configured, and listening 
    //        sockets are being set up. 
    // 0xFF– Module is currently scanning for the configured SSID.
    //
    // We added 0xFE to indicate connected but SSID doesn't match
    // the provided id.
    Serial.print("Waiting to connect: ");
    Serial.println(status, HEX);
    digitalWrite(led, (lastLED = ! lastLED));
    delay(1000);
  }
  digitalWrite(led, HIGH);
}

// Check if the XBee is connected to a WiFi network.
// This function will send the ATAI command to the XBee.
// That command will return with either a 0 (meaning connected)
// or various values indicating different levels of no-connect.
byte checkConnect()
{
  byte i=0xFF;
  char temp[2];
  commandMode(0);
  while (!commandMode(1))
    ;
  command("ATAI", 2);
  temp[0] = hexToInt(xB.read());
  temp[1] = hexToInt(xB.read());
  xB.flush();
#ifdef DEBUG
  Serial.print("temp[0] = ");
  Serial.println(temp[0], HEX);
  Serial.print("temp[1] = ");
  Serial.println(temp[1], HEX);
#endif
  
  if (temp[0] == 0) {
    return 0;
  }
  else {
    return (temp[0]<<4) | temp[1];
  }
}

/////////////////////////////////////
// Low-level, ugly, XBee Functions //
/////////////////////////////////////
void command(String atcmd, int rsplen)
{
  xB.flush();
#ifdef DEBUG
  Serial.print("Entering command: ");
#endif
  xB.print(atcmd);
#ifdef DEBUG
  Serial.println(atcmd);
#endif  
  xB.print("\r");
  waitForAvailable(rsplen);
}

int commandMode(boolean enter)
{
  xB.flush();
  
  if (enter)
  {
#ifdef DEBUG
  Serial.println("Entering Command Mode");
#endif
    char c;
    xB.print("+++");   // Send CMD mode string
    waitForAvailable(1);
    if (xB.available() > 0)
    {
      c = xB.read();
#ifdef DEBUG
  Serial.print("Command Mode Response: ");
  Serial.println(c);
#endif
      if (c == 'O') // That's the letter 'O', assume 'K' is next
        return 1; // IF we see "OK" return success
    }
    return 0; // If no (or incorrect) receive, return fail
  }
  else
  {
#ifdef DEBUG
  Serial.println("Exiting Command Mode");
#endif
    command("ATCN", 2);
    return 1;
  }
}

int waitForAvailable(int qty)
{
  int timeout = COMMAND_TIMEOUT;

  while ((timeout-- > 0) && (xB.available() < qty))
    delay(1);

  return timeout;
}

byte hexToInt(char c)
{
  if (c >= 0x41) // If it's A-F
    return c - 0x37;
  else
    return c - 0x30;
}

