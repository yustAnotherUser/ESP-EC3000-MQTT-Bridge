// Whitelist EC3000-IDs

#pragma once

const char* whitelist[] = {
  "7821",  // 3D-Drucker
  "531C",  // Waschmaschine & Geschirrspüler
  "7E3A"   // Kühlschrank
  "770C"    // E-Bike
  "7E65"    // Monitor
  "51D2"    // Total
};

const int whitelistSize = sizeof(whitelist) / sizeof(whitelist[0]);
