// Whitelist EC3000-IDs

#pragma once

const char* whitelist[] = {
  "DEAD",  // 3D-Drucker
  "BEEF",  // Waschmaschine & Geschirrspüler
  "1234",   // Kühlschrank
  "5678",    // E-Bike
  "90AB",    // Monitor
  "CDEF"    // Total
};

const int whitelistSize = sizeof(whitelist) / sizeof(whitelist[0]);
