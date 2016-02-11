//ARDUINO 1.0+ ONLY
//ARDUINO 1.0+ ONLY
#include <TimeLib.h>
#include <Adafruit_CC3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>

////////////////////////////////////////////////////////////////////////
//CONFIGURE WIFI
////////////////////////////////////////////////////////////////////////
// These are the interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3  // MUST be an interrupt pin!
// These can be any two pins
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10
// Use hardware SPI for the remaining pins
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11
Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT,
                         SPI_CLOCK_DIVIDER); // you can change this clock speed but DI

#define WLAN_SSID       "xxxx"        // cannot be longer than 32 characters!
#define WLAN_PASS       "xxxx"
// Security can be WLAN_SEC_UNSEC, WLAN_SEC_WEP, WLAN_SEC_WPA or WLAN_SEC_WPA2
#define WLAN_SECURITY   WLAN_SEC_WPA2

Adafruit_CC3000_Client client;

// What site to grab!
#define WEBSITE      "xxxx.synology.me"


////////////////////////////////////////////////////////////////////////
//PAROLA
////////////////////////////////////////////////////////////////////////
#define  MAX_DEVICES 12
#define CLK_PIN   24
#define DATA_PIN  23
#define CS_PIN    22
MD_Parola P = MD_Parola(DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

////////////////////////////////////////////////////////////////////////
//Variables /conts etc.
////////////////////////////////////////////////////////////////////////
const int timeZone = 1;     // Central European Time
char inString[2000]; // string for incoming serial data
char *WEBPAGE ;//= "/algemeen";
int stringPos = 0; // string index counter
boolean startRead = false; // is reading?
const unsigned long
connectTimeout  = 15L * 1000L, // Max time to wait for server connection
responseTimeout = 15L * 1000L; // Max time to wait for data from server
int
countdown       = 0;  // loop() iterations until next time server query
unsigned long
lastPolledTime  = 0L, // Last value retrieved from time server
sketchTime      = 0L; // CPU milliseconds since last server query
#define IDLE_TIMEOUT_MS  30000
uint32_t ip = 0;

//clock
unsigned long timeDisplay = 0;
// day display delay
unsigned long lastDateDisplay = 0;
unsigned long DateDelay = 600000;
// RSS reading delay
unsigned long lastRSStime = 0;
unsigned long RssDelay = 600000;
// rss or domo
bool rss = 0;

////////////////////////////////////////////////////////////////////////
//SETUP
////////////////////////////////////////////////////////////////////////
void setup() {
  P.begin();
  Serial.begin(115200);
  Serial.println(F("Hello, CC3000!\n"));

  displayDriverMode();

  Serial.println(F("\nInitialising ..."));
  P.displayText("Initialising ...", CENTER, 1, 2000, PRINT, PRINT );
  while (!P.displayAnimate())
    ;
  if (!cc3000.begin()) {
    Serial.println(F("Unable to initialise the CC3000! Check your wiring?"));
    for (;;);
  }

  uint16_t firmware = checkFirmwareVersion();
  if (firmware < 0x113) {
    Serial.println(F("Wrong firmware version!"));
    for (;;);
  }

  displayMACAddress();

  Serial.println(F("\nDeleting old connection profiles"));
  if (!cc3000.deleteProfiles()) {
    Serial.println(F("Failed!"));
    while (1);
  }

  /* Attempt to connect to an access point */
  char *ssid = WLAN_SSID;             /* Max 32 chars */
  Serial.print(F("\nAttempting to connect to ")); Serial.println(ssid);
  P.displayText("Connecting..." , CENTER, 1, 2000, PRINT, PRINT );
  while (!P.displayAnimate())
    ;

  /* NOTE: Secure connections are not available in 'Tiny' mode! */
  if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    Serial.println(F("Failed!"));
    while (1);
  }

  Serial.println(F("Connected!"));
  P.displayText("Connected", CENTER, 1, 2000, PRINT, PRINT );
  while (!P.displayAnimate())
    ;
  /* Wait for DHCP to complete */
  Serial.println(F("Request DHCP"));
  while (!cc3000.checkDHCP()) {
    delay(100); // ToDo: Insert a DHCP timeout!
  }

  /* Display the IP address DNS, Gateway, etc. */
  while (!displayConnectionDetails()) {
    delay(1000);
  }
  setSyncProvider(getTime);

  // Try looking up the website's IP address
  Serial.print(WEBSITE); Serial.print(F(" -> "));
  while (ip == 0) {
    if (! cc3000.getHostByName(WEBSITE, &ip)) {
      Serial.println(F("Couldn't resolve!"));
    }
  }
  Serial.print(ip);
}

////////////////////////////////////////////////////////////////////////
//LOOP
////////////////////////////////////////////////////////////////////////
void loop() {

  char txtBuf[2000];
  char charBuf[100];
  if ((unsigned long)(millis() - timeDisplay >= 1000 )) {
    sprintf(charBuf, "%02u : %02u", hour(), minute());
    P.displayText(charBuf, CENTER, 0, 0, PRINT, NO_EFFECT );
    while (!P.displayAnimate())
      timeDisplay = millis();
  }
  ;
  if ((unsigned long)(millis() - lastDateDisplay) >= DateDelay) {
    lastDateDisplay = millis();
    String dag = String(dayStr(weekday())) + " " + String(day()) + " " + String(monthShortStr(month()));
    dag.toCharArray(charBuf, 100);
    P.displayText(charBuf, CENTER, 1, 3000, SCROLL_UP, SCROLL_UP );
    while (!P.displayAnimate());
  }
  if ((unsigned long)(millis() - lastRSStime) >= RssDelay && rss == 1 ) {
    lastRSStime = millis();
    String pageValue = connectAndRead("/feed.cgi"); //connect to the server and read the output
    pageValue.toCharArray(txtBuf, 2000);
    P.displayText(txtBuf, CENTER, 3, 0, SCROLL_LEFT, SCROLL_LEFT );
    while (!P.displayAnimate());
    rss = 0;
  }
  if ((unsigned long)(millis() - lastRSStime) >= RssDelay && rss == 0 ) {
    lastRSStime = millis();
    String pageValue = connectAndRead("/domo.cgi"); //connect to the server and read the output
    pageValue.toCharArray(txtBuf, 2000);
    P.displayText(txtBuf, CENTER, 3, 0, SCROLL_LEFT, SCROLL_LEFT );
    while (!P.displayAnimate());
    lastRSStime = millis();
    rss = 1;
  }
}

String connectAndRead( char* WEBPAGE) {
  stringPos = 0;
  memset( &inString, 0, 2000); //clear inString memory

  Adafruit_CC3000_Client www = cc3000.connectTCP(ip, 80);
  if (www.connected()) {
    www.fastrprint(F("GET "));
    www.fastrprint(WEBPAGE);
    www.fastrprint(F(" HTTP/1.1\r\n"));
    www.fastrprint(F("Host: ")); www.fastrprint(WEBSITE); www.fastrprint(F("\r\n"));
    www.fastrprint(F("\r\n"));
    www.println();
    Serial.println(WEBPAGE);
    //Connected - Read the page
    /* Read data until either the connection is closed, or the idle timeout is reached. */
    unsigned long lastRead = millis();
    while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
      while (www.available()) {
        char c = www.read();
        Serial.print(c);
        if (c == '<' ) { //'<' is our begining character
          startRead = true; //Ready to start reading the part
        } else if (startRead) {

          if (c != '>') { //'>' is our ending character
            inString[stringPos] = c;
            stringPos ++;
          } else {
            //got what we need here! We can disconnect now
            startRead = false;
            client.stop();
            client.flush();
            Serial.println("disconnecting.");
            www.close();
            Serial.println("Close www.");
            return inString;
          }
        }
      }
    }
  } else {
    Serial.println(F("Connection failed"));
    return "connection failed";
  }

}


/**************************************************************************/
/*!
    @brief  Displays the driver mode (tiny of normal), and the buffer
            size if tiny mode is not being used

    @note   The buffer size and driver mode are defined in cc3000_common.h
*/
/**************************************************************************/
void displayDriverMode(void)
{
#ifdef CC3000_TINY_DRIVER
  Serial.println(F("CC3000 is configure in 'Tiny' mode"));
#else
  Serial.print(F("RX Buffer : "));
  Serial.print(CC3000_RX_BUFFER_SIZE);
  Serial.println(F(" bytes"));
  Serial.print(F("TX Buffer : "));
  Serial.print(CC3000_TX_BUFFER_SIZE);
  Serial.println(F(" bytes"));
#endif
}

/**************************************************************************/
/*!
    @brief  Tries to read the CC3000's internal firmware patch ID
*/
/**************************************************************************/
uint16_t checkFirmwareVersion(void)
{
  uint8_t major, minor;
  uint16_t version;

#ifndef CC3000_TINY_DRIVER
  if (!cc3000.getFirmwareVersion(&major, &minor))
  {
    Serial.println(F("Unable to retrieve the firmware version!\r\n"));
    version = 0;
  }
  else
  {
    Serial.print(F("Firmware V. : "));
    Serial.print(major); Serial.print(F(".")); Serial.println(minor);
    version = major; version <<= 8; version |= minor;
  }
#endif
  return version;
}

/**************************************************************************/
/*!
    @brief  Tries to read the 6-byte MAC address of the CC3000 module
*/
/**************************************************************************/
void displayMACAddress(void)
{
  uint8_t macAddress[6];

  if (!cc3000.getMacAddress(macAddress))
  {
    Serial.println(F("Unable to retrieve MAC Address!\r\n"));
  }
  else
  {
    Serial.print(F("MAC Address : "));
    cc3000.printHex((byte*)&macAddress, 6);
  }
}


/**************************************************************************/
/*!
    @brief  Tries to read the IP address and other connection details
*/
/**************************************************************************/
bool displayConnectionDetails(void)
{
  uint32_t ipAddress, netmask, gateway, dhcpserv, dnsserv;

  if (!cc3000.getIPAddress(&ipAddress, &netmask, &gateway, &dhcpserv, &dnsserv))
  {
    Serial.println(F("Unable to retrieve the IP Address!\r\n"));
    return false;
  }
  else
  {
    Serial.print(F("\nIP Addr: ")); cc3000.printIPdotsRev(ipAddress);
    Serial.print(F("\nNetmask: ")); cc3000.printIPdotsRev(netmask);
    Serial.print(F("\nGateway: ")); cc3000.printIPdotsRev(gateway);
    Serial.print(F("\nDHCPsrv: ")); cc3000.printIPdotsRev(dhcpserv);
    Serial.print(F("\nDNSserv: ")); cc3000.printIPdotsRev(dnsserv);
    Serial.println();
    return true;
  }
}

// Minimalist time server query; adapted from Adafruit Gutenbird sketch,
// which in turn has roots in Arduino UdpNTPClient tutorial.
unsigned long getTime(void) {

  uint8_t       buf[48];
  unsigned long ip, startTime, t = 0L;

  Serial.print(F("Locating time server..."));

  // Hostname to IP lookup; use NTP pool (rotates through servers)
  if (cc3000.getHostByName("0.nl.pool.ntp.org", &ip)) {
    static const char PROGMEM
    timeReqA[] = { 227,  0,  6, 236 },
                 timeReqB[] = {  49, 78, 49,  52 };

    Serial.println(F("\r\nAttempting connection..."));
    startTime = millis();
    do {
      client = cc3000.connectUDP(ip, 123);
    } while ((!client.connected()) &&
             ((millis() - startTime) < connectTimeout));

    if (client.connected()) {
      Serial.print(F("connected!\r\nIssuing request..."));

      // Assemble and issue request packet
      memset(buf, 0, sizeof(buf));
      memcpy_P( buf    , timeReqA, sizeof(timeReqA));
      memcpy_P(&buf[12], timeReqB, sizeof(timeReqB));
      client.write(buf, sizeof(buf));

      Serial.print(F("\r\nAwaiting response..."));
      memset(buf, 0, sizeof(buf));
      startTime = millis();
      while ((!client.available()) &&
             ((millis() - startTime) < responseTimeout));
      if (client.available()) {
        client.read(buf, sizeof(buf));
        t = (((unsigned long)buf[40] << 24) |
             ((unsigned long)buf[41] << 16) |
             ((unsigned long)buf[42] <<  8) |
             (unsigned long)buf[43]) - 2208988800UL + timeZone * SECS_PER_HOUR;
        Serial.print(F("OK\r\n"));
      }
      client.close();
    }
  }
  if (!t) Serial.println(F("error"));
  return t;
}
