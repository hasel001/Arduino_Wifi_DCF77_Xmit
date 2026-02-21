/* * DCF77 Emulator - Arduino Uno WiFi Rev 2
 * Syncs to WiFi NTP and broadcasts 77.5 kHz on Pin 6
 
 Designed by Lawrence Haselmaier
 Written by Gemini
 Debugged and revised by Lawrence Haselmaier
 
 Do not use in any location where it is illegal to transmit
 on 77.5 kHz. Use at your own rist and with due sense and discretion.
 
 Version 0.9 - 20 Feb 2026*/

#include <SPI.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <TimeLib.h> // Install "Time" by Michael Margolis

// --- WiFi Credentials ---
char ssid[] = "ssid";
char pass[] = "password";

// --- DCF77 Configuration ---
const int carrierPin = 6;
unsigned long lastTick = 0;
bool dcfBits[60];

// set localTimeZone according to UTC offset
// e.g., if in Central Standard Time, which is UTC -6, set it to -6
const float localTimeZone = -6;

// NTP Settings
WiFiUDP Udp;
IPAddress timeServer(162, 159, 200, 1); // pool.ntp.org
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

void setup() {
  Serial.begin(9600);
  pinMode(carrierPin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  // 1. Setup Timer B (TCB0) for 77.5 kHz
  TCB0.CTRLB = TCB_CNTMODE_PWM8_gc;
  TCB0.CCMPL = 205; 
  TCB0.CCMPH = 103; 
  TCB0.CTRLA = TCB_ENABLE_bm;

  // 2. Connect to WiFi
  Serial.print("Connecting to WiFi...");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  // 3. Sync Time
  Udp.begin(2390);
  setSyncProvider(getNtpTime);
  setSyncInterval(3600); // Re-sync every hour

  Serial.println("Waiting for NTP Sync...");
  while(timeStatus() == timeNotSet); 
  
  Serial.println("Time Synced!");
  generateFrame();
}

void loop() {
  // We trigger the bit transmission at the start of every second
  if (second() != lastTick) {
    lastTick = second();
    
    // If it's the start of a new minute, generate a fresh frame
    if (lastTick == 0) {
      generateFrame();
      printCurrentTime();
    }

    if (lastTick < 59) {
      transmitBit(dcfBits[lastTick]);
      Serial.print(dcfBits[lastTick] ? "1" : "0");
    } else {
      Serial.println(" [SYNC]");
    }
  }
}

void transmitBit(bool bit) {
  int duration = bit ? 200 : 100;
  TCB0.CTRLB &= ~TCB_CCMPEN_bm; // Drop carrier
  digitalWrite(LED_BUILTIN, HIGH);
  delay(duration);
  TCB0.CTRLB |= TCB_CCMPEN_bm; // Restore carrier
  digitalWrite(LED_BUILTIN, LOW);
}

void generateFrame() {
  // Logic based on timeLib.h values
  int m = minute();
  int h = hour();
  int d = day();
  int dw = weekday(); // 1=Sun, 7=Sat
  int mo = month();
  int yr = year() % 100;

  for (int i = 0; i < 60; i++) dcfBits[i] = 0;
  
  dcfBits[17] = 0; // CEST/CET bits (Adjust for your timezone)
  dcfBits[18] = 1; 
  dcfBits[20] = 1; 

  fillBits(m, 21, 7);
  dcfBits[28] = calcParity(21, 27);
  fillBits(h, 29, 6);
  dcfBits[35] = calcParity(29, 34);
  fillBits(d, 36, 6);
  fillBits(dw - 1, 42, 3); // DCF uses 1=Mon, 7=Sun
  fillBits(mo, 45, 5);
  fillBits(yr, 50, 8);
  dcfBits[58] = calcParity(36, 57);
}

void fillBits(int val, int start, int len) {
  int units = val % 10;
  int tens = val / 10;
  for (int i = 0; i < 4; i++) dcfBits[start + i] = (units >> i) & 1;
  for (int i = 4; i < len; i++) dcfBits[start + i] = (tens >> (i - 4)) & 1;
}

bool calcParity(int start, int end) {
  int count = 0;
  for (int i = start; i <= end; i++) if (dcfBits[i]) count++;
  return (count % 2 != 0);
}

void printCurrentTime() {
  Serial.print("\nBroadcasting: ");
  Serial.print(hour()); Serial.print(":");
  Serial.print(minute()); Serial.print(" Date: ");
  Serial.println(day());
}

/* NTP Code Helpers */
unsigned long getNtpTime() {
  while (Udp.parsePacket() > 0); // discard old packets
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long secsSince1900;
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      //accounting for time zone here
      secsSince1900+= (localTimeZone * 3600);
      return secsSince1900 - 2208988800UL;
    }
  }
  return 0; 
}

void sendNTPpacket(IPAddress &address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011; packetBuffer[1] = 0; packetBuffer[2] = 6; packetBuffer[3] = 0xEC;
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}