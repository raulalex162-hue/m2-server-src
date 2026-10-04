# Registrul de modificări

Fiecare modificare importantă are o intrare, în ordinea în care a fost înregistrată.
Numerele nu se refolosesc și nu se renumerotează. Modificările de cod au commit-ul
alături; cele de proces (build, scripturi) explică ce s-a schimbat în felul de lucru.

Repository-uri: `m2-server-src` (sursa serverului), `m2-server` (fișierele serverului),
`m2-client-src` (sursa clientului), `m2-client` (clientul).

---

## Faza 0 – CORE

### MODIFICARE #001
- Sistem: organizare proiect
- Fișier(e): `.gitignore` (m2-server), `docs/`
- Problemă: codul nu era versionat în repository-uri proprii
- Cauză: clone directe din proiectul original
- Soluție: patru repository-uri private; proiectul original păstrat ca `upstream`; branch `core/skeleton`; config-urile cu secrete (`share/conf/secrets/`, `*.local.json`) excluse din Git; dezvoltarea ca utilizatorul `Raul`, nu ca root
- Impact: niciunul asupra jocului
- Sisteme afectate: niciunul
- Test efectuat: push pe toate cele 4 repository-uri, server pornit ca `Raul`
- Rezultat: OK

### MODIFICARE #002
- Sistem: CORE / log
- Fișier(e): `src/core/log/Log.h`, `Log.cpp`, `src/core/CMakeLists.txt`, `tests/`, `include/doctest.h`, `CMakeLists.txt`, `src/CMakeLists.txt`, `src/game/CMakeLists.txt`
- Problemă: nu exista log separat per sistem și nici un mod de a urmări o acțiune prin mai multe sisteme
- Cauză: m2dev are doar `syslog` și `syserr`
- Soluție: librăria `m2core` (separată de `game`, deci testabilă singură), canale de log în `systems.log`, trace id per acțiune, erorile copiate în `syserr`; testele unitare cu doctest (ținta `core_tests`)
- Impact: niciunul asupra jocului; modulul încă nu era folosit
- Sisteme afectate: niciunul
- Test efectuat: `core_tests` (6 teste), login în joc după recompilare
- Rezultat: OK
- Commit: `3dfc986`

### MODIFICARE #003
- Sistem: CORE / config
- Fișier(e): `src/core/config/Config.h`, `Config.cpp`, `tests/core/test_config.cpp`, `include/nlohmann/json.hpp`
- Problemă: sistemele nu aveau un mod comun de a-și citi și valida configurația
- Cauză: m2dev nu are config-uri per sistem
- Soluție: citire JSON (cu comentarii, tolerant la BOM), validare cu raport complet al tuturor problemelor și calea exactă a câmpului (`rewards[2].count: ...`), câmpurile comune `enabled` și `log_level`
- Impact: niciunul asupra jocului
- Sisteme afectate: niciunul
- Test efectuat: `core_tests` (16 teste)
- Rezultat: OK
- Commit: `2ae8990`

### MODIFICARE #004
- Sistem: CORE / registry (+ corecție în testele de log)
- Fișier(e): `src/core/registry/Registry.h`, `Registry.cpp`, `tests/core/test_registry.cpp`, `tests/core/test_log.cpp`, `src/core/log/Log.h`
- Problemă: sistemele nu aveau un ciclu de viață comun; în plus, testele de registry cădeau cu segmentation fault
- Cauză: (registry) inexistent; (fault) testul de log punea o destinație temporară și nu o scotea la final, iar canalul `REGISTRY`, creat de un test ulterior, scria în memorie deja eliberată
- Soluție: registry cu încărcare în două faze (config-ul se aplică doar dacă e valid), refuzul pornirii la config invalid, reload fără efect la config invalid, oprire în ordine inversă; testul de log revine la `systems.log` la final
- Impact: (fault) doar în teste; destinația de test nu se folosește în joc
- Sisteme afectate: niciunul
- Test efectuat: `core_tests` (28 de teste)
- Rezultat: OK
- Commit: `728a347`

### MODIFICARE #005
- Sistem: CORE / events
- Fișier(e): `src/core/events/Bus.h`, `Bus.cpp`, `GameEvents.h`, `src/game/services/EventPublish.*`, `src/game/input_db.cpp`, `src/game/char.cpp`, `src/game/char_battle.cpp`, `src/core/registry/Registry.cpp`, `tests/core/test_events.cpp`
- Problemă: sistemele ar fi trebuit apelate direct din codul de bază (ca în Rodnia: achievement-uri din 20 de fișiere), ceea ce duce la dependențe circulare
- Cauză: m2dev nu are un mecanism de evenimente
- Soluție: magistrală de evenimente; `EnterGame`, `LeaveGame`, `MobKill` publicate din aceleași locuri în care m2dev anunță quest-urile; un abonat cu excepție nu oprește jocul; registry-ul dezabonează automat un sistem oprit
- Impact: 2 linii în fiecare din cele 3 fișiere de bază
- Sisteme afectate: niciunul (nimeni nu asculta încă)
- Test efectuat: `core_tests` (37 de teste, inclusiv sub AddressSanitizer local), joc: login, monștri, warp, logout
- Rezultat: OK
- Commit: `183b273`

### MODIFICARE #006
- Sistem: proces de lucru (build)
- Fișier(e): —
- Problemă: testul „registry-ul dezaboneaza automat un sistem oprit” pica (1 în loc de 0)
- Cauză: `unzip` a păstrat data veche din arhivă pe `Registry.cpp`; `gmake` a considerat `Registry.o` la zi și nu l-a recompilat
- Soluție: `touch` pe fișier și recompilare; de acum, după fiecare arhivă: `unzip -o arhiva.zip && unzip -Z1 arhiva.zip | xargs touch`
- Impact: niciunul asupra codului
- Sisteme afectate: niciunul
- Test efectuat: `core_tests`, 37/37
- Rezultat: OK

### MODIFICARE #007
- Sistem: game / Protobuf
- Fișier(e): `src/game/services/ProtoBegin.h`, `ProtoEnd.h`, `src/game/systems/heartbeat/Heartbeat.cpp`
- Problemă: `game` nu se compila la primul include al unui `.pb.h`
- Cauză: macro-ul `number(from, to)` din `libthecore/utils.h` înlocuia și metoda `number()` din headerele Protobuf
- Soluție: `ProtoBegin.h` / `ProtoEnd.h` ascund temporar macro-ul în jurul include-urilor Protobuf și îl readuc apoi neschimbat
- Impact: niciunul; `number()` din m2dev funcționează ca înainte
- Sisteme afectate: heartbeat
- Regulă nouă: niciun `.pb.h` inclus în codul jocului fără perechea ProtoBegin/ProtoEnd
- Test efectuat: reproducere locală cu și fără soluție, build pe VM
- Rezultat: OK

### MODIFICARE #008
- Sistem: proces de lucru (build)
- Fișier(e): —
- Problemă: `/sysinfo` răspundea „This command does not exist”
- Cauză: fără `cmake ..`, `GLOB_RECURSE` din `src/game` nu a văzut `SystemCommands.cpp` (fișier nou); legarea `game` a eșuat, iar în `share/bin` a rămas binary-ul vechi
- Soluție: `cmake ..` înainte de `gmake` după orice fișier nou; verificare `ls -la bin/game` înainte de copiere
- Impact: niciunul asupra codului
- Sisteme afectate: niciunul
- Test efectuat: `strings bin/game | grep -c sysreload`, comenzile în joc
- Rezultat: OK

### MODIFICARE #009
- Sistem: proces de lucru (scripturi de aplicare)
- Fișier(e): `apply_pas6c_client.py`
- Problemă: scriptul nu a găsit linia `PackLib` în `src/UserInterface/CMakeLists.txt`
- Cauză: căutarea nu ținea cont de finalurile de linie CRLF puse de Git pe Windows; pe copia de test (LF) mersese
- Soluție: căutare corectată, testată pe un fișier CRLF
- Impact: niciunul asupra codului
- Sisteme afectate: niciunul
- Regulă nouă: scripturile pentru client se testează și pe copii cu finaluri de linie Windows
- Rezultat: OK

### MODIFICARE #010
- Sistem: CORE / net (transport, server)
- Fișier(e): `src/core/net/Router.h`, `Router.cpp`, `src/game/services/NetTransport.*`, `src/common/packet_headers.h`, `src/game/input.h`, `src/game/input_main.cpp`, `src/game/packet_info.cpp`, `tests/core/test_net.cpp`
- Problemă: sistemele noi nu aveau un canal de rețea propriu
- Cauză: fiecare sistem ar fi avut nevoie de pachete noi în codul de rețea m2dev
- Soluție: un singur pachet `SYSTEM` (0x0C80), prefix `[system:2][type:2]`, router cu limită de mărime per mesaj; codul de rețea m2dev atins o singură dată
- Impact: 5 linii în codul de bază; serverul nu trimite încă nimic, deci clientul vechi nu e afectat
- Test efectuat: `core_tests` (46 de teste), joc
- Rezultat: OK
- Commit: `641ca58`

### MODIFICARE #011
- Sistem: proto / Protobuf (server)
- Fișier(e): `proto/`, `src/game/services/NetMessages.h`, `tests/proto/`, `tests/CMakeLists.txt`, `CMakeLists.txt`, `src/game/CMakeLists.txt`
- Problemă: mesajele aveau nevoie de un format tipizat și versionabil
- Soluție: Protobuf din pachetele sistemului (comutator `M2_PROTOBUF_SOURCE` = `system` | `vendor`), cod generat la build, hash de protocol calculat din fișierele `.proto` (`0fef88099e8df740`)
- Impact: `game` depinde acum de `libprotobuf` din `/usr/local/lib` (pkg)
- Test efectuat: `core_tests` (51 de teste), `ldd`, server pornit
- Rezultat: OK
- Commit: `d682b7a`

### MODIFICARE #012
- Sistem: systems / heartbeat (server)
- Fișier(e): `src/game/systems/`, `src/game/main.cpp`, `share/conf/systems/heartbeat.json` (m2-server)
- Problemă: lanțul CORE nu fusese dovedit în joc
- Soluție: lista sistemelor (`Systems.cpp`), pornire înainte de bucla principală, oprire după deconectarea jucătorilor; sistemul-martor `heartbeat`
- Impact: 3 linii în `main.cpp`; pe core-ul de auth sistemele nu pornesc
- Regulă nouă: config-ul fiecărui sistem se numește `<Sistem>Config`
- Test efectuat: `systems.log` cu `start heartbeat`, `recv EnterGame/MobKill/LeaveGame` cu trace id
- Rezultat: OK
- Commit: `92a7303`

### MODIFICARE #013
- Sistem: game / comenzi GM
- Fișier(e): `src/game/services/SystemCommands.cpp`, `src/game/cmd.cpp`, `src/core/registry/*`, `src/core/net/*`, `src/game/systems/heartbeat/*`, teste
- Problemă: sistemele nu puteau fi inspectate sau reîncărcate fără restart
- Soluție: `/sysinfo`, `/sysreload`, `/sysdebug`; registry-ul dă sistemelor contextul de pornire și oprire (`LifecycleCtx`) și primește `Describe` și `SetLogLevel`
- Impact: 6 linii în `cmd.cpp`
- Test efectuat: `core_tests` (54 de teste), testele 1–6 în joc (config invalid refuzat, disabled/running fără restart, debug temporar)
- Rezultat: OK
- Commit: `c1f636c`

### MODIFICARE #014
- Sistem: m2net (client)
- Fișier(e): `src/UserInterface/m2net/`, `proto/`, `src/UserInterface/Packet.h`, `PythonNetworkStream.h`, `PythonNetworkStream.cpp`, `PythonNetworkStreamCommand.cpp`, `PythonNetworkStreamPhaseGame.cpp`, `CMakeLists.txt`, `src/UserInterface/CMakeLists.txt` (m2-client-src)
- Problemă: clientul nu cunoștea pachetul `SYSTEM`
- Soluție: primire și trimitere `SYSTEM` (doar în faza de joc), Protobuf din vcpkg (`x64-windows-static`), `Ping` automat la intrarea în joc, comanda locală `/ping`, `ProtoBegin`/`ProtoEnd` pentru macro-urile din `windows.h`
- Impact: scripturile Python ale clientului nu s-au schimbat; client și server trebuie actualizate împreună
- Test efectuat: hash de protocol identic pe client și server, `Pong` 33–52 ms, limita de frecvență observată în joc
- Rezultat: OK

---

## Faza 1 – servicii comune

### MODIFICARE #015
- Sistem: CORE / storage (PlayerSystemData, logica)
- Fișier(e): `src/core/storage/SystemDataCache.*`, `PlayerDataStore.*`, `proto/server/system_data.proto`, `proto/CMakeLists.txt`, `tests/core/test_storage.cpp`; `sql/migrations/001_player_system_data.sql` (m2-server)
- Problemă: sistemele nu aveau unde să-și țină datele per jucător; o scriere directă din game în MariaDB ar fi permis la warp citirea unor date vechi (ex. o zi de daily reward revendicată de două ori)
- Soluție: db e singurul proprietar al datelor, cu un cache mereu cel mai nou; game nu poate scrie înainte să primească datele; un blob Protobuf per (pid, sistem); mesajele game ↔ db în `proto/server/`, în afara hash-ului protocolului
- Impact: niciunul asupra jocului (logică pură)
- Test efectuat: `core_tests` (65), inclusiv warp cu DB lentă și salvare în timpul citirii
- Rezultat: OK
- Commit: `1e76683`

### MODIFICARE #016
- Sistem: db / systemdata
- Fișier(e): `src/db/systemdata/*`, `src/db/ClientManager.cpp`, `ClientManager.h`, `QID.h`, `CMakeLists.txt`, `src/common/packet_headers.h`, `src/common/ProtoBegin.h`, `ProtoEnd.h`
- Problemă: db nu știa de datele sistemelor; `GetPeer` era privată; ProtoBegin/End erau doar în game
- Soluție: `GD::SYSTEM_DATA` (0x90C0) / `DG::SYSTEM_DATA` (0x91C0), citire asincronă, scriere imediată în hex (fără escapare), răspuns doar dacă core-ul mai e conectat, curățarea cache-ului după 30 de minute de inactivitate; `FindPeer()` public; ProtoBegin/End mutate în `common`
- Impact: 11 linii în `ClientManager.cpp`, câte una în `ClientManager.h`, `QID.h`, 2 în `packet_headers.h`, 2 în CMake
- Test efectuat: build, `ldd`, server pornit
- Rezultat: OK
- Commit: `9095ce1`

### MODIFICARE #017
- Sistem: game / playerdata
- Fișier(e): `src/game/services/PlayerData.*`, `EventPublish.cpp`, `src/game/systems/Systems.cpp`, `src/core/events/GameEvents.h`, `src/game/input.h`, `src/game/input_db.cpp`; `share/conf/systems/player_data.json`
- Problemă: game trebuia să ceară și să trimită datele în ordinea corectă față de sisteme
- Soluție: cerere la intrare (înainte de `EnterGame`), salvare la ieșire (după `LeaveGame`, deci sistemele mai pot scrie), salvare periodică la 60 s și la oprire; evenimentul nou `SystemDataReady`; `Load`/`Store` tipizate cu Protobuf
- Impact: 2 linii în codul de bază
- Test efectuat: `/sysinfo player_data`, cereri și răspunsuri la relog
- Rezultat: OK
- Commit: `8926b76`

### MODIFICARE #018
- Sistem: heartbeat (test pentru PlayerSystemData)
- Fișier(e): `proto/server/heartbeat_state.proto`, `src/game/systems/heartbeat/*`
- Problemă: PlayerSystemData nu fusese dovedit în joc
- Soluție: heartbeat salvează per jucător `total_pings`, `last_ping_ms`, `entries`
- Test efectuat: relog (`entries=2`), warp pe 3 core-uri (`total_pings` 6 → 7 → 8, `entries` 10 → 11 → 12), rândul în MariaDB (11 octeți), salvare periodică în timpul sesiunii
- Rezultat: OK
- Commit: `1c390a8`

### MODIFICARE #019
- Sistem: CORE / reward (logica)
- Fișier(e): `src/core/reward/Reward.*`, `tests/core/test_reward.cpp`
- Problemă: m2dev pierde recompense în tăcere: cu inventarul plin itemul cade pe jos și dispare după 60 s; yang-ul peste 2 miliarde e refuzat doar în `syserr`
- Soluție: pachet de recompensă, livrare „cât încape” cu restul păstrat, cutie de recompense cu limită, citire din config cu validator de vnum
- Impact: niciunul asupra jocului (logică pură)
- Test efectuat: `core_tests` (73)
- Rezultat: OK

### MODIFICARE #020
- Sistem: game / RewardService
- Fișier(e): `src/game/services/Reward.*`, `RewardCommands.cpp`, `proto/server/reward_state.proto`, `proto/server/storage_ids.proto`, `src/game/systems/Systems.cpp`, `src/game/cmd.cpp`; `share/conf/systems/reward.json`
- Problemă: sistemele aveau nevoie de un singur drum, sigur, pentru recompense
- Soluție: itemele se dau doar după ce există o celulă liberă (nu mai ajung pe jos); restul intră în cutia salvată imediat prin PlayerSystemData; livrare automată la intrare; `/reward` (jucători, pauză 3 s) și `/reward_test` (GM); vnum-urile din config verificate în `item_proto`; refuz înainte de a da ceva dacă restul nu poate fi păstrat
- Regulă nouă: identificatorii de date per jucător sub 1000 = sistemele din protocolul cu clientul; de la 1000 = doar server (cutia = 1001)
- Impact: 5 linii în `cmd.cpp` (`reward` înaintea lui `reward_test`, pentru că m2dev potrivește comenzile după prefix)
- Test efectuat: livrare directă, vnum inexistent refuzat, yang peste limită în cutie, cutie păstrată la relog și la restart, rândul 1001 în MariaDB
- Rezultat: OK

### MODIFICARE #021
- Sistem: CORE / reward + game / RewardService (la cererea lui Raul)
- Fișier(e): `src/core/reward/Reward.*`, `src/game/services/Reward.cpp`, `tests/core/test_reward.cpp`
- Problemă: yang-ul din cutie se dădea doar întreg; cu 1,5 miliarde și 1 miliard în cutie nu se dădea nimic
- Soluție: yang livrat parțial, până la limită; în cutie rămâne exact diferența; `IReceiver::CanReceiveGold` înlocuit cu `GoldCapacity`
- Test efectuat: `core_tests` (75); în joc: după restart, livrare automată de 11 teancuri și 644.809.615 yang, apoi încă 864.000 yang după cheltuială
- Rezultat: OK

### MODIFICARE #022
- Sistem: CORE / time + scheduler
- Fișier(e): `src/core/time/Calendar.*`, `src/core/scheduler/Scheduler.*`, `src/core/registry/Registry.cpp`, `tests/core/test_time.cpp`
- Problemă: sistemele zilnice aveau nevoie de o „zi de joc” sigură și de sarcini programate
- Soluție: `DayKey` / `WeekKey` / `NextReset` în ora locală (cu ora de vară); planificator `Every` / `DailyAt` fără rulări recuperate în avalanșă; registry-ul anulează sarcinile unui sistem oprit
- Regulă: ce nu are voie să rateze o zi folosește `DayKey`, nu un eveniment de reset
- Test efectuat: `core_tests` (86), inclusiv ziua de 23 de ore din 29 martie 2026
- Rezultat: OK

### MODIFICARE #023
- Sistem: game / game_time
- Fișier(e): `src/game/services/GameTime.*`, `src/game/systems/Systems.cpp`, `src/game/systems/heartbeat/*`; `share/conf/systems/game_time.json`
- Problemă: planificatorul trebuia „bătut” în joc, iar ora de reset trebuia să fie comună tuturor sistemelor
- Soluție: sistemul `game_time` (primul din listă) bate planificatorul o dată pe secundă și ține `day_reset`; heartbeat are o sarcină pe minut și una zilnică, de test
- Test efectuat: `/sysinfo game_time` cu ora EEST; reset de test la 18:27 → `zi noua de joc` la 18:27:00 pe toate cele 4 core-uri; la un start după ora de reset nu s-a declanșat o zi falsă
- Rezultat: OK

---

## Probleme cunoscute

| Problemă | Când se rezolvă | Până atunci |
| --- | --- | --- |
| `/sysreload` acționează doar pe core-ul curent | Faza 5, sau mai devreme dacă devine incomod (cere un pachet P2P) | reload pe fiecare core sau restart |
| Clientul are un singur sistem în `Dispatch` | începutul Fazei 2, odată cu daily reward | nimic; cu un singur sistem e corect |
| `LNK4098` (`LIBCMT`) în build-ul Debug al clientului | doar dacă apar crash-uri numai în Debug | se verifică dacă apare și în Release |
| Textele pentru jucători (`[reward] ...`) sunt scrise în cod | Faza 2, odată cu primele ferestre din client | rămân în română, fără diacritice |
| Pauza de 3 s de la `/reward` e implementată local în comandă | când construim `Guard` (anti-abuz comun) | funcționează, dar nu e refolosibilă |
| Schimbarea `day_reset` cu `/sysreload` nu mută sarcinile `DailyAt` deja programate | doar dacă devine necesar | restart după schimbarea orei |
| `CurrencyService` amânat | la primul sistem care are nevoie de o monedă nouă | yang-ul îl tratează RewardService |
| Inventarul special (materiale, pietre, cufere) | primul sistem al Fazei 4 | inventarul normal |
