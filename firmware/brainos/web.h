// The page on the home network: http://brain.local (while Wi-Fi is on).
#pragma once
void webBegin();
void webTick();
bool webHasPassword();        // the page has its password (made on the first visit)
void webForgetPassword();     // from the board's About: opens a 2-minute window to make a new one
void webArmSetup();           // About: allow /setup for two minutes
bool webSetupOpen();          // the window is open
