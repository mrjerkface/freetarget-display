// WiFi + TCP link to the freETarget. Finds the target's FET-* network, joins
// it, connects to TCP port 1090 and feeds shots into the model. Reconnects on
// its own forever.
#pragma once

void net_start(void);
