#ifndef debug_h
#define debug_h

// see also https://forum.arduino.cc/index.php?topic=46900.0
#ifdef DEBUG
#define DEBUG_PRINT(x)  Serial.print (x)
#define DEBUG_PRINTLN(x)  Serial.println (x)
#define DEBUG_PRINTF(x, y)  Serial.printf (x, y)
#define DEBUG_PRINTF2(x, y, z)  Serial.printf (x, y, z)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(x, y)
#define DEBUG_PRINTF2(x, y, z)
#endif

#endif