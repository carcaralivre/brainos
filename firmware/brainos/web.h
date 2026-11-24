// The page on the home network: http://brain.local (while Wi-Fi is on).
#pragma once
void webBegin();
void webTick();
bool webHasPassword();        // the page has its password (made on the first visit)
void webForgetPassword();     // from the board's About: the next visit makes a new one
