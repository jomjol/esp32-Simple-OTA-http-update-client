#ifndef CONNECT_WLAN_H
#define CONNECT_WLAN_H

#include <string>

bool initialise_wifi(std::string _ssid, std::string _passphrase, std::string _hostname, int _try_reconnect = -1);

void LoadWlanFromFile(std::string fn, std::string &_ssid, std::string &_passphrase, std::string &_hostname);

std::string getHostname();
std::string getIPAddress();
std::string getSSID();

#endif