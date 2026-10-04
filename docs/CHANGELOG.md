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

## Probleme cunoscute

| Problemă | Când se rezolvă | Până atunci |
| --- | --- | --- |
| `/sysreload` acționează doar pe core-ul curent | Faza 5, sau mai devreme dacă devine incomod (cere un pachet P2P) | reload pe fiecare core sau restart |
| Clientul are un singur sistem în `Dispatch` | începutul Fazei 2, odată cu daily reward | nimic; cu un singur sistem e corect |
| `LNK4098` (`LIBCMT`) în build-ul Debug al clientului | doar dacă apar crash-uri numai în Debug | se verifică dacă apare și în Release |
