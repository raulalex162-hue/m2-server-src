// game/services/ProtoBegin.h + ProtoEnd.h — se pun in jurul oricarui #include "....pb.h".
//
//   #include "../services/ProtoBegin.h"
//   #include "m2/heartbeat.pb.h"
//   #include "../services/ProtoEnd.h"
//
// De ce: headerele vechi m2dev definesc macro-uri cu nume obisnuite (ex. number(from, to)
// din libthecore/utils.h) care strica headerele Protobuf si Abseil. Aici le ascundem
// temporar; ProtoEnd.h le readuce exact cum erau, deci restul jocului nu observa nimic.
//
// Fara #pragma once: fisierul trebuie sa poata fi inclus de mai multe ori.

#pragma push_macro("number")
#undef number
