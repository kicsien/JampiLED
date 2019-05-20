/*
original example in library: 
https://github.com/MajicDesigns/MD_MAX72XX 

NTP clock with MD_MX72xx code:
https://github.com/wilyarti/Simple_IOT_Clock/blob/master/simple_iot_clock.ino 

Original project and backend software for spectrum analyzer:
https://www.codeproject.com/Articles/797537/Making-an-Audio-Spectrum-analyzer-with-Bass-dll-Cs
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h> 
#include <WiFiManager.h> 
#include <ArduinoOTA.h>
#include <WebSocketsClient.h>
#include <DNSServer.h>

#include <MD_MAX72xx.h>
#include <SPI.h>

//NTP
#include <WiFiUdp.h>
WiFiUDP UDP;                     // Create an instance of the WiFiUDP class to send and receive

//for NTP
IPAddress timeServerIP;          // time.nist.gov NTP server address
/*
 * to set timeZone modify the following two lines
 * more info at 615. line
 */
const char* NTPServerName = "0.hu.pool.ntp.org";
const uint32_t seventyYears = 2209017600UL; //original value: 2208988800UL; to convert NTP time to a UNIX timestamp
//end
const int NTP_PACKET_SIZE = 48;  // NTP time stamp is in the first 48 bytes of the message
byte NTPBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets

unsigned long intervalNTP = 60000; // Request NTP time every minute
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
uint32_t timeUNIX = 0;
int lastMinute;
int lastHour;
unsigned long prevActualTime = 0;
bool stime = false;   //to clear display on first run of clock routine
bool spectr = false;  //to clear display on first run of spectrum analyzer routine 

//for spectrum analyzer
int counter = 32;
int value = 0;
byte buffer[32] = { 0 };
int lastvalue = 0;

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define MAX_DEVICES 4

#define CLK_PIN   D5 // or SCK GPIO14
#define DATA_PIN  D7 // or MOSI GPIO13
#define CS_PIN    D8 // or SS  GPIO15

// SPI hardware interface
#define HARDWARE_TYPE MD_MAX72XX::ICSTATION_HW  //edit this as per your LED matrix hardware type
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

//server and telnet
WiFiServer server(80);
WiFiServer TelnetServer(23);
WiFiClient Telnet;
WiFiManager wifiManager;

// Global message buffers shared by Wifi and Scrolling functions
const uint8_t MESG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;
const uint8_t SCROLL_DELAY = 75;
const char* capName = "JampiLED";

char curMessage[MESG_SIZE];
char newMessage[MESG_SIZE];
char command[MESG_SIZE];
bool newMessageAvailable = false;

volatile byte displayMode = 2;

//DEBUG
#define  PRINT_CALLBACK  0
#define DEBUG 0
#define LED_HEARTBEAT 0

#if DEBUG
#define PRINT(s, v) { Serial.print(F(s)); Serial.print(v); }
#define PRINTS(s)   { Serial.print(F(s)); }
#else
#define PRINT(s, v)
#define PRINTS(s)
#endif

#if LED_HEARTBEAT
#define HB_LED  D2
#define HB_LED_TIME 500 // in milliseconds
#endif

//Webpage
char WebResponse[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";

char WebPage[] =
  "<!DOCTYPE html>" \
  "<html>" \
  "<head>" \
  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">" \
  "<title>JampiLED</title>" \
  "<style>" \
  "html, body" \
  "{" \
  "margin: 0px;" \
  "border: 0px;" \
  "padding: 5px;" \
  "background-color: white;" \
  "font-family: sans-serif;" \
  "}" \
  "#container " \
  "{" \
  "border: solid 2px #CCC;" \
  "padding: 10px;" \
  "background-color: #ECECEC;" \
  "}" \
  "input[type=text] " \
  "{" \
  "width: 200px;"\
  "}" \
  "#brgtRange {opacity: 0.2;}" \
  "</style>" \
  "<script>" \
  "strLine = \"\";" \
  "function SendText()" \
  "{" \
  "  nocache = \"/&nocache=\" + Math.random() * 1000000;" \
  "  var request = new XMLHttpRequest();" \
  "  if(document.getElementById(\"clock\").checked) {" \
  "     strLine = \"&MSG=stime\"; " \
  "     boxDisplay(\"msgBox\", false); boxDisplay(\"brgtRange\", false); " \
  "  } else if(document.getElementById(\"spectrum\").checked) {" \
  "     strLine = \"&MSG=spectrum\"; "\
  "     boxDisplay(\"msgBox\", false); boxDisplay(\"brgtRange\", false); " \  
  "  } else if(document.getElementById(\"birght\").checked) {" \
  "     strLine = \"&MSG=lum_\" + document.getElementById(\"txt_form\").Brightness.value; "\
  "     boxDisplay(\"msgBox\", false); boxDisplay(\"brgtRange\", true); " \  
  "  } else { " \
  "     strLine = \"&MSG=\" + document.getElementById(\"txt_form\").Message.value; "\
  "     boxDisplay(\"msgBox\", true); boxDisplay(\"brgtRange\", false); }" \
  "  request.open(\"GET\", strLine + nocache, false);" \
  "  request.send(null);" \
  "}" \
  "function boxDisplay(elem, condition) {"\
  "  var box = document.getElementById(elem); "\
  "  if(condition) {"\
  "     box.style.opacity = \"1\"; "\
  "     box.style.userSelect = \"inherit\"; "\
  "     box.style.pointerEvents = \"auto\"; "\
  "  } else { "\
  "     box.style.opacity = \"0.2\"; "\
  "     box.style.userSelect = \"none\"; "\
  "     box.style.pointerEvents = \"none\"; "\
  "  }" \
  "}" \
  "function changeRadio(elem) {" \
  "  if(elem.value == \"text\") {" \
  "     boxDisplay(\"msgBox\", true); boxDisplay(\"brgtRange\", false); " \  
  "  } else if(elem.value == \"brightness\") {" \
  "     boxDisplay(\"msgBox\", false); boxDisplay(\"brgtRange\", true); " \  
  "  } else { " \
  "    boxDisplay(\"msgBox\", false); boxDisplay(\"brgtRange\", false);"\
  "  }" \
  "}" \
  "</script>" \
  "</head>" \
  "<body>" \
  "<div id=\"container\">"\
  "<H1><b>JampiLED</b></H1>" \
  "<!--<p>WiFi MAX7219 LED Matrix Display</p>-->" \
  "<form id=\"txt_form\" name=\"frmText\">" \
  "<div id=\"msgBox\">" \
  "<label>Text message:</label><br>" \
  "<label><input type=\"text\" name=\"Message\" maxlength=\"255\"></label><br><br>" \
  "</div><div id=\"brgtRange\">" \
  "<label>Display brightness:</label><br>" \
  "0 <input type=\"range\" name=\"Brightness\" min=\"0\" max=\"15\"> 15<br><br>" \
  "</div></form><form id=\"checkboxes\" name=\"modes\">" \
  "<input type=\"radio\" id=\"text\" name=\"mode\" value=\"text\" checked=\"checked\" onclick=\"changeRadio(this)\"> <label for=\"text\">Display text</label><br>" \
  "<input type=\"radio\" id=\"birght\" name=\"mode\" value=\"brightness\" onclick=\"changeRadio(this)\"> <label for=\"birght\">Set display brightness</label><br>" \
  "<input type=\"radio\" id=\"clock\" name=\"mode\" value=\"clock\" onclick=\"changeRadio(this)\"> <label for=\"clock\">Display clock</label><br>"\
  "<input type=\"radio\" id=\"spectrum\" name=\"mode\" value=\"spectrum\" onclick=\"changeRadio(this)\"> <label for=\"spectrum\">Display spectrum analyzer</label><br><br>" \
  "</form>" \
  "<input type=\"submit\" value=\"Send\" onclick=\"SendText()\">" \
  "</div>" \
  "</body>" \
  "</html>";

char *err2Str(wl_status_t code)
{
  switch (code)
  {
    case WL_IDLE_STATUS:    return ("IDLE");           break; // WiFi is in process of changing between statuses
    case WL_NO_SSID_AVAIL:  return ("NO_SSID_AVAIL");  break; // case configured SSID cannot be reached
    case WL_CONNECTED:      return ("CONNECTED");      break; // successful connection is established
    case WL_CONNECT_FAILED: return ("CONNECT_FAILED"); break; // password is incorrect
    case WL_DISCONNECTED:   return ("CONNECT_FAILED"); break; // module is not configured in station mode
    default: return ("??");
  }
}

uint8_t htoi(char c)
{
  c = toupper(c);
  if ((c >= '0') && (c <= '9')) return (c - '0');
  if ((c >= 'A') && (c <= 'F')) return (c - 'A' + 0xa);
  return (0);
}

boolean getText(char *szMesg, char *psz, uint8_t len)
{
  boolean isValid = false;  // text received flag
  char *pStart, *pEnd;      // pointer to start and end of text

  // get pointer to the beginning of the text
  pStart = strstr(szMesg, "/&MSG=");

  if (pStart != NULL)
  {
    pStart += 6;  // skip to start of data
    pEnd = strstr(pStart, "/&");

    if (pEnd != NULL)
    {
      while (pStart != pEnd)
      {
        if ((*pStart == '%') && isdigit(*(pStart + 1)))
        {
          // replace %xx hex code with the ASCII character
          char c = 0;
          pStart++;
          c += (htoi(*pStart++) << 4);
          c += htoi(*pStart++);
          *psz++ = c;
        }
        else
          *psz++ = *pStart++;
      }

      *psz = '\0'; // terminate the string
      isValid = true;
    }
  }

  return (isValid);
}

void handleWiFi(void)
{
  static enum { S_IDLE, S_WAIT_CONN, S_READ, S_EXTRACT, S_RESPONSE, S_DISCONN } state = S_IDLE;
  static char szBuf[1024];
  static uint16_t idxBuf = 0;
  static WiFiClient client;
  static uint32_t timeStart;

  switch (state)
  {
    case S_IDLE:   // initialise
      //PRINTS("\nS_IDLE");
      idxBuf = 0;
      state = S_WAIT_CONN;
      break;

    case S_WAIT_CONN:   // waiting for connection
      {
        client = server.available();
        if (!client) break;
        if (!client.connected()) break;

#if DEBUG
        char szTxt[20];
        sprintf(szTxt, "%03d:%03d:%03d:%03d", client.remoteIP()[0], client.remoteIP()[1], client.remoteIP()[2], client.remoteIP()[3]);
        //PRINT("\nNew client @ ", szTxt);
#endif

        timeStart = millis();
        state = S_READ;
      }
      break;

    case S_READ: // get the first line of data
      //PRINTS("\nS_READ");
      while (client.available())
      {
        char c = client.read();
        if ((c == '\r') || (c == '\n'))
        {
          szBuf[idxBuf] = '\0';
          client.flush();
          //PRINT("\nRecv: ", szBuf);
          state = S_EXTRACT;
        }
        else
          szBuf[idxBuf++] = (char)c;
      }
      if (millis() - timeStart > 1000)
      {
        //PRINTS("\nWait timeout");
        state = S_DISCONN;
      }
      break;

    case S_EXTRACT: // extract data
      //PRINTS("\nS_EXTRACT");
      // Extract the string from the message if there is one
      newMessageAvailable = getText(szBuf, newMessage, MESG_SIZE);
      //PRINT("\nNew Msg: ", newMessage); 
      if(strcmp(newMessage, "stime") == 0 ||          //  avoiding display commands
      strcmp(newMessage, "spectrum") == 0 ||
      String(newMessage).indexOf('lum_') != -1) {
        strcpy(command, newMessage);
        newMessageAvailable = false;
      } else 
        strcpy(command, "text");
      state = S_RESPONSE;
      break;

    case S_RESPONSE: // send the response to the client
      //PRINTS("\nS_RESPONSE");
      // Return the response to the client (web page)
      client.print(WebResponse);
      client.print(WebPage);
      state = S_DISCONN;
      break;

    case S_DISCONN: // disconnect client
      //PRINTS("\nS_DISCONN");
      client.flush();
      client.stop();
      state = S_IDLE;
      break;

    default:  state = S_IDLE;
  }
}

void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col)
// Callback function for data that is being scrolled off the display
{
#if PRINT_CALLBACK
  /*Serial.print("\n cb ");
  Serial.print(dev);
  Serial.print(' ');
  Serial.print(t);
  Serial.print(' ');
  Serial.println(col);*/
#endif
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t)
// Callback function for data that is required for scrolling into the display
{
  static enum { S_IDLE, S_NEXT_CHAR, S_SHOW_CHAR, S_SHOW_SPACE } state = S_IDLE;
  static char   *p;
  static uint16_t curLen, showLen;
  static uint8_t  cBuf[8];
  uint8_t colData = 0;

  // finite state machine to control what we do on the callback
  switch (state)
  {
    case S_IDLE: // reset the message pointer and check for new message to load
      //PRINTS("\nS_IDLE");
      p = curMessage;      // reset the pointer to start of message
      if (newMessageAvailable)  // there is a new message waiting
      {
        strcpy(curMessage, newMessage); // copy it in
        newMessageAvailable = false;
      }
      state = S_NEXT_CHAR;
      break;

    case S_NEXT_CHAR: // Load the next character from the font table
      //PRINTS("\nS_NEXT_CHAR");
      if (*p == '\0')
        state = S_IDLE;
      else
      {
        showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state = S_SHOW_CHAR;
      }
      break;

    case S_SHOW_CHAR: // display the next part of the character
      //PRINTS("\nS_SHOW_CHAR");
      colData = cBuf[curLen++];
      if (curLen < showLen)
        break;

      // set up the inter character spacing
      showLen = (*p != '\0' ? CHAR_SPACING : (MAX_DEVICES * COL_SIZE) / 2);
      curLen = 0;
      state = S_SHOW_SPACE;
    // fall through

    case S_SHOW_SPACE:  // display inter-character spacing (blank column)
      //PRINT("\nS_ICSPACE: ", curLen);
      //PRINT("/", showLen);
      curLen++;
      if (curLen == showLen)
        state = S_NEXT_CHAR;
      break;

    default:
      state = S_IDLE;
  }

  return (colData);
}

void scrollText(void)
{
  static uint32_t prevTime = 0;

  // Is it time to scroll the text?
  if (millis() - prevTime >= SCROLL_DELAY)
  {
    mx.transform(MD_MAX72XX::TSL);  // scroll along - the callback will load all the data
    prevTime = millis();      // starting point for next time
  }
}

void setup()
{
  Serial.begin(115200);
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.autoConnect(capName);

#if DEBUG
  //Serial.begin(115200);
  //PRINTS("\n[MD_MAX72XX WiFi Message Display]\nType a message for the scrolling display from your internet browser");
#endif

#if LED_HEARTBEAT
  pinMode(HB_LED, OUTPUT);
  digitalWrite(HB_LED, LOW);
#endif

  // Display initialisation
  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);

  curMessage[0] = newMessage[0] = '\0';

  // Connect to and initialise WiFi network
  //PRINT("\nConnecting to ", ssid);
  //WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    //Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(capName);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    //Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    /*
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    */
  });

  ArduinoOTA.begin();
  //Serial.println("Ready");

  //NTP
  startUDP();
  if (!WiFi.hostByName(NTPServerName, timeServerIP)) { // Get the IP address of the NTP server
    //Serial.println("DNS lookup failed. Rebooting.");
    //Serial.flush();
    ESP.reset();
  }
  //Serial.print("Time server IP:\t");
  //Serial.println(timeServerIP);

  //Serial.println("\r\nSending NTP request ...");
  sendNTPpacket(timeServerIP);
    
  // Start the server
  server.begin();
  //PRINTS("\nServer started");
  //telnet start
  TelnetServer.begin();
  TelnetServer.setNoDelay(true);

  // Set up first message as the IP address
  sprintf(curMessage, "%03d:%03d:%03d:%03d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  //PRINT("\nAssigned IP ", curMessage);
    Serial.println("JampiLED is booting now.");

}

void displayTime(){
    unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > intervalNTP) { // If a minute has passed since last NTP request
    prevNTP = currentMillis;
    //Serial.println("\r\nSending NTP request ...");
    sendNTPpacket(timeServerIP);               // Send an NTP request
  }

  uint32_t time = getTime();                   // Check if an NTP response has arrived and get the (UNIX) time
  if (time) {                                  // If a new timestamp has been received
    timeUNIX = time;
    //Serial.print("NTP response:\t");
    //Serial.println(timeUNIX);
    lastNTPResponse = currentMillis;
  } else if ((currentMillis - lastNTPResponse) > 3600000) {
    spiral();
    //Serial.println("More than 1 hour since last NTP response. Rebooting.");
    //Serial.flush();
    ESP.reset();
  }

  uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse) / 1000;
  // Change 10 below to your time offset. For example I am in AEST (QLD) which is UTC plus 10.
  uint32_t localTime = actualTime + (10 * 60 * 60);
  if (actualTime != prevActualTime && timeUNIX != 0) { // If a second has passed since last print
    prevActualTime = actualTime;
    //Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(actualTime), getMinutes(actualTime), getSeconds(actualTime));
    //Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(localTime), getMinutes(localTime), getSeconds(localTime));

  }
  // Convert number to string, then split the string so each char uses a single display
  // Convert to local time first
  String strhh  = String(getHours(localTime));
  String strmm  = String(getMinutes(localTime));

  // Create an animation on hour change.
  if (lastHour != getHours(localTime)) {
    spiral();
    lastHour = getHours(localTime);
  }

  // Only update if the time changes, otherwise just update ticker.
  if (lastMinute != getMinutes(localTime) || !stime) {
    mx.clear();
    for (int i = 0; i < strhh.length(); i++) {
      if (strhh.length() == 1) {
        mx.setChar((COL_SIZE * (4)) - 2, '0');
        mx.setChar((COL_SIZE * (4 - 1)) - 1, strhh[i]);

      } else {
        mx.setChar((COL_SIZE * (4 - i)) - (2 - i), strhh[i]);
      }
    }
    for (int k = 0; k < strmm.length(); k++) {
      if (strmm.length() == 1) {
        mx.setChar((COL_SIZE * (2)) - 1, '0');
        mx.setChar((COL_SIZE * (2 - 1)) - 2, strmm[k]);
      } else {
        if (k == 0) {
          mx.setChar((COL_SIZE * (2 - k)) - 3, strmm[k]);
        } else if (k == 1 ) {
          mx.setChar((COL_SIZE * (2 - k)) - 2, strmm[k]);
        }
      }
    }
    char *d = ":";
    mx.setChar((COL_SIZE * 2) , *d);
    mx.update();
    lastMinute = getMinutes(localTime);
  }
  mx.setPoint(ROW_SIZE - 1, 31, true);
  for (int j = 0; j < getSeconds(localTime); j++) {
    mx.setPoint(ROW_SIZE - 1, (30 - (j / 2)), true);
  }
}


void startUDP() {
  //Serial.println("Starting UDP");
  UDP.begin(123);                          // Start listening for UDP messages on port 123
  //Serial.print("Local port:\t");
  //Serial.println(UDP.localPort());
  //Serial.println();
}

uint32_t getTime() {
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  // const uint32_t seventyYears = 2209017600UL; //2208988800UL + 28 800
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

void sendNTPpacket(IPAddress& address) {
  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode

  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

inline int getSeconds(uint32_t UNIXTime) {
  return UNIXTime % 60;
}

inline int getMinutes(uint32_t UNIXTime) {
  return UNIXTime / 60 % 60;
}

inline int getHours(uint32_t UNIXTime) {
  return UNIXTime / 3600 % 24;
}
void spiral()
// setPoint() used to draw a spiral across the whole display
{
  int  rmin = 0, rmax = ROW_SIZE - 1;
  int  cmin = 0, cmax = (COL_SIZE * MAX_DEVICES) - 1;

  mx.clear();
  while ((rmax > rmin) && (cmax > cmin))
  {
    // do row
    for (int i = cmin; i <= cmax; i++)
    {
      mx.setPoint(rmin, i, true);
      delay(100 / MAX_DEVICES);
    }
    rmin++;

    // do column
    for (uint8_t i = rmin; i <= rmax; i++)
    {
      mx.setPoint(i, cmax, true);
      delay(100 / MAX_DEVICES);
    }
    cmax--;

    // do row
    for (int i = cmax; i >= cmin; i--)
    {
      mx.setPoint(rmax, i, true);
      delay(100 / MAX_DEVICES);
    }
    rmax--;

    // do column
    for (uint8_t i = rmax; i >= rmin; i--)
    {
      mx.setPoint(i, cmin, true);
      delay(100 / MAX_DEVICES);
    }
    cmin++;
  }
}
void cross()
// Combination of setRow() and setColumn() with user controlled
// display updates to ensure concurrent changes.
{
  mx.clear();
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  // diagonally down the display R to L
  for (uint8_t i = 0; i < ROW_SIZE; i++)
  {
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0xff);
      mx.setRow(j, i, 0xff);
    }
    mx.update();
    delay(100);
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0x00);
      mx.setRow(j, i, 0x00);
    }
  }

  // moving up the display on the R
  for (int8_t i = ROW_SIZE - 1; i >= 0; i--)
  {
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0xff);
      mx.setRow(j, ROW_SIZE - 1, 0xff);
    }
    mx.update();
    delay(100);
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0x00);
      mx.setRow(j, ROW_SIZE - 1, 0x00);
    }
  }

  // diagonally up the display L to R
  for (uint8_t i = 0; i < ROW_SIZE; i++)
  {
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0xff);
      mx.setRow(j, ROW_SIZE - 1 - i, 0xff);
    }
    mx.update();
    delay(100);
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0x00);
      mx.setRow(j, ROW_SIZE - 1 - i, 0x00);
    }
  }
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void Set(int index, int value)
{
  int device = index / 8; //calculate device
  int row = index - (device * 8); //calculate row
  int leds = map(value, 0, 255, 0, 9); //map value to number of leds.
  //display data
  switch (leds)
  {
  case 0:
    mx.setColumn(device,row, 0x00);
    return;
  case 1:
    mx.setColumn(device,row, 0x80);
    return;
  case 2:
    mx.setColumn(device,row, 0xc0);
    return;
  case 3:
    mx.setColumn(device,row, 0xe0);
    return;
  case 4:
    mx.setColumn(device,row, 0xf0);
    return;
  case 5:
    mx.setColumn(device,row, 0xf8);
    return;
  case 6:
    mx.setColumn(device,row, 0xfc);
    return;
  case 7:
    mx.setColumn(device,row, 0xfe);
    return;
  case 8:
    mx.setColumn(device,row, 0xff);
    return;
  }
}

// https://stackoverflow.com/questions/9072320/split-string-into-string-array
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void changeMode() {
  if(strcmp(command, "stime") == 0) {                 //  set display clock
    displayMode = 0;
  }
  else if(strcmp(command, "spectrum") == 0){          //  set display audio visualizer
    displayMode = 1;
  }
  else if(String(command).indexOf('lum_') != -1){     //  set brightness
    int intensity_val = getValue(command,'_',1).toInt();
    mx.control(MD_MAX72XX::INTENSITY, intensity_val);
  }
  else {                                              //  set display text
    displayMode = 2;
  }
}

void loop()
{
#if LED_HEARTBEAT
  static uint32_t timeLast = 0;

  if (millis() - timeLast >= HB_LED_TIME)
  {
    digitalWrite(HB_LED, digitalRead(HB_LED) == LOW ? HIGH : LOW);
    timeLast = millis();
  }
#endif
  ArduinoOTA.handle();
  handleWiFi();
  changeMode();
  if(displayMode == 0){             // clock display
      if(!stime) {  //clear display
        mx.clear();
        strcpy(curMessage, " ");
        stime = true;
      }
      displayTime();
      spectr = false;    
  } else if (displayMode == 1) {    // audio visualizer
      if(!spectr){  //clear display
          mx.clear();
          strcpy(curMessage, " ");
          spectr = true;
      }
      if (TelnetServer.hasClient()){
        if (!Telnet || !Telnet.connected()){
            if(Telnet) Telnet.stop();
            Telnet = TelnetServer.available();
          } else {
            TelnetServer.available().stop();
          }
        }
        if (Telnet && Telnet.connected() && Telnet.available()){
          counter = 32;
          while(Telnet.available()) {
            value = Telnet.read();
            Set(counter, value);
            counter--;
            if (counter < 1) counter = 32;
          }
      }
      stime = false;
  } else {                         // scrolling text display
      scrollText();
      stime = false;
      spectr = false;  
  }
}
