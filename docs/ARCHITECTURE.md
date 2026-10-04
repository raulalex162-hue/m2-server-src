# Plan de arhitectură și implementare – Server Metin2 (m2dev)

Oct 3, 2026 · @Raul

## Context și principii

Construim pe **baza m2dev** (server și client deja funcționale pe VM-ul local) și folosim **Rodnia** doar ca referință de funcționalitate. Din Rodnia preluăm idei și formate de configurare, nu cod copiat.

Regulile de arhitectură, valabile pentru tot proiectul:

1. **Un sistem = o responsabilitate.** Fiecare sistem stă în folderul lui și are o interfață publică mică.
2. **Sistemele nu se apelează direct între ele.** Comunică prin **evenimente** (GameEvents) sau prin **serviciile din CORE** (recompense, monede, config). Aceasta e regula care previne dependențele circulare găsite în Rodnia.
3. **Codul de bază (CHARACTER, shop, luptă) nu știe de sisteme.** El doar publică evenimente („a murit un monstru”, „s-a cumpărat un item”), iar sistemele interesate le ascultă.
4. **Serverul decide, clientul afișează.** Orice verificare (preț, cooldown, drept de revendicare) se face pe server.
5. **Valorile stau în config, nu în cod.** Recompense, praguri și timpi se citesc din fișiere JSON, validate la pornire.
6. **Niciun secret în cod sau în Git.** Chei, parole și tokenuri stau doar în config-uri locale, ignorate de Git.
7. **Incremental:** un sistem → implementare → test → validare → abia apoi următorul.

**Decizie de rețea:** sistemele noi comunică prin **mesaje Protobuf**, ca în Rodnia, nu prin structuri binare clasice. Pachetele vechi Ymir rămân neatinse pentru gameplay-ul existent, deci nu rescriem ce merge deja. Protobuf vine deocamdată din pachetele sistemului (`pkg` pe FreeBSD, vcpkg `x64-windows-static` pe Windows), cu un comutator CMake `M2_PROTOBUF_SOURCE` = `system` | `vendor`, ca mai târziu să-l compilăm local din `vendor` fără alte schimbări. Toate mesajele sistemelor noi trec printr-un singur pachet m2dev, `SYSTEM` (0x0C80), cu prefixul `[system:2][type:2]` urmat de mesajul Protobuf.

## Inventarul sistemelor

Referința Rodnia are în jur de 140 de sisteme activate (143 de define-uri în client, 151 pe server). Nu le facem pe toate: le grupăm aici ca să vedem ce infrastructură comună cer, apoi alegem pe rând.

| Categorie | Sisteme (din referință) | Ce cer de la CORE |
| --- | --- | --- |
| Progresie | achievement-uri, challenges, battle pass (normal și progress), biolog, titluri, reborn, heroes level | evenimente de joc, recompense, date per jucător, config |
| Recompense periodice | daily reward, play-time lottery, advent calendar, vote4buff | recompense, timp server, date per cont, anti-abuz |
| Economie și comerț | offline shop (cu istoric și seif), căutare în magazine, cumpărare multiplă, trade nou, special storage, monede noi | monede, tranzacții atomice în DB, log de economie |
| Iteme | costume (armă, mount, aripi), sash, change look, switchbot, shining, skill color, refine cu boost, bonus 6–7, deschidere multiplă de cufere | inventar, recompense, config |
| Companioni | buffi, pet-uri cu incubator, horse rework | date per jucător, timere, config |
| Evenimente | world cup, kingdom defence, ultra world boss, global goal, enchant, fish, xmas, spooky, golden, dojang, minigame-uri | planificator de evenimente, recompense, anunțuri, evenimente de joc |
| PvP și clasamente | combat zone, duel nou, ranking (inclusiv sezonier) | evenimente de joc, agregare de date, cache |
| Calitatea vieții (mai ales client) | filtre de pickup, ascundere efecte, costume și obiecte, FOV, fonturi, info monștri, timer dungeon, minimap nou, loguri chat | setări locale, câteva pachete simple |
| Administrare și securitate | mentenanță, captcha, whisper admin, account guard, patch notes, log de hack | log, permisiuni, config, notificări |

Sisteme de noroc (ruletă, blackjack, loterii) rămân **în afara planului** până decidem cum le tratăm legal pe un server cu donații.

## CORE: componentele comune

CORE-ul conține tot ce folosesc cel puțin două sisteme. Niciun sistem nu își rescrie propria variantă de config, log, recompensă sau acces la DB. Pe server stă în `src/game/core/`.

| Componentă | Ce face | Ce NU face | Folosită de |
| --- | --- | --- | --- |
| SystemRegistry | Înregistrează sistemele, le pornește și oprește în ordine, le activează sau dezactivează din config (feature flags), oferă `/sysreload` | Nu conține logică de joc | toate sistemele |
| SystemConfig | Citește `share/conf/systems/<sistem>.json`, validează câmpurile și refuză pornirea cu mesaj clar dacă lipsește ceva | Nu interpretează valorile | toate sistemele |
| Log (canale) | Un canal spdlog pe sistem (`[ACHIEVEMENT]`, `[DAILY]`), cu niveluri și trace id per acțiune | Nu decide ce e eroare de joc | toate |
| GameEvents | Magistrala de evenimente: codul de bază publică (`OnMobKill`, `OnItemBuy`, `OnLogin`, `OnLevelUp`), sistemele se abonează | Nu știe ce sisteme există | progresie, evenimente, ranking |
| RewardService | Acordă orice recompensă dintr-o descriere comună (item, yang, monedă, titlu, buff), cu log și verificare de spațiu în inventar | Nu decide **când** se dă recompensa | daily, achievement, battle pass, evenimente |
| CurrencyService | Toate monedele (yang, NR coin, monede noi): citire, adăugare, scădere atomică, log de economie | Nu face prețuri | shop, offline shop, reward |
| PlayerSystemData | Stocare per jucător și per sistem (progres, ultima revendicare), încărcată la login, salvată periodic | Nu definește structura datelor fiecărui sistem | progresie, daily, companioni |
| Guard (anti-abuz) | Cooldown și rate limit pe comenzi și pachete, verificări comune (jucător viu, nu în trade, nu în warp) | Nu face anti-cheat de mișcare | toate comenzile noi |
| SystemPackets | Interval rezervat de headere per sistem, folosit ca transport pentru mesaje Protobuf: schema în fișiere .proto, cod generat la build, câmpuri validate automat, versiuni compatibile între client și server | Nu înlocuiește pachetele vechi Ymir, care rămân pentru gameplay-ul existent | sistemele cu interfață |
| Scheduler | Sarcini la oră fixă sau periodice (reset zilnic, sezon, eveniment) pe ora serverului | Nu ține starea evenimentelor | daily, evenimente, ranking |

Pe client, echivalentul e un folder `root/core/`: dispecerul de pachete pe sistem, o fereastră de bază (`SystemWindow`) cu deschidere/închidere și log, și helperii de tooltip pentru recompense.

## Harta dependențelor

Sistemele de bază sunt CORE-ul și cele trei servicii din Faza 1; toate celelalte depind de ele și niciunul nu depinde de alt sistem, cu o singură excepție controlată.

&#91;embedded content: harta dependențelor · 6 niveluri\]

Codul de bază al jocului doar publică evenimente în GameEvents. Singura legătură sistem → sistem e Căutarea, care citește Offline shop printr-o interfață doar de citire.

Ce folosește fiecare și de ce:

- **Achievement, Challenges, Battle pass, Biolog** folosesc **GameEvents** pentru că progresul lor vine din acțiuni de joc (monstru ucis, item cumpărat, nivel), și **RewardService** pentru că dau recompense.
- **Titluri** folosește **RewardService** pentru că un titlu e o recompensă de tip TITLE. Achievement-ul nu apelează Titlurile direct, ci acordă o recompensă de acel tip.
- **Battle pass** folosește **Scheduler** pentru resetul sezonului.
- **Daily reward** folosește **PlayerSystemData** pentru ultima revendicare și **Scheduler** pentru ziua serverului.
- **Offline shop** folosește **CurrencyService** pentru că orice vânzare e o tranzacție de bani și **Guard** pentru limitele de acțiuni.
- **Ranking și evenimentele** folosesc **GameEvents** și **Scheduler**, deci vin la final, când aceste componente sunt deja stabile.

Dependențe circulare posibile: niciuna, atâta timp cât regula săgeții în jos e respectată. Riscul real e un sistem care vrea să „reacționeze” la altul (de exemplu achievement la finalizarea battle pass-ului). Soluția e mereu un eveniment publicat de primul (`OnBattlePassFinished`), nu un apel direct.

## Probleme arhitecturale găsite în referință

În sursa serverului Rodnia, sistemele de progresie sunt apelate direct din codul de bază. Asta e principala sursă de bug-uri greu de urmărit și o evităm din prima zi.

| Problemă | Dovada din sursa Rodnia | Cum o evităm |
| --- | --- | --- |
| Cuplare directă | Achievement-urile sunt apelate din 20 de fișiere `.cpp`, challenges din 19, battle pass e referit în 26 (luptă, shop, pescuit, cube, inventar, quest-uri) | Codul de bază publică un eveniment; sistemul ascultă. Un singur punct de intrare per sistem |
| Dependență circulară | `AchievementSystem.cpp` include `shop_manager.h`, iar `shop.cpp` apelează `CAchievementSystem::OnBuy` | Shop publică `OnItemBuy`; achievement-ul nu include nimic din shop |
| Lanț între sisteme | `battle_pass.cpp` și `ProgressBattlePass.cpp` includ atât `AchievementSystem.h`, cât și `ChallangesSystem.h` | Battle pass publică `OnBattlePassFinished`; cine are nevoie ascultă |
| Obiect-zeu | Toate sistemele includ `char.h`, iar `char.cpp` le apelează pe toate | CHARACTER rămâne neatins; datele sistemelor stau în PlayerSystemData |
| Scurtături de developer | `test_server` dă GM tuturor; un nume de caracter scris în cod (`MrZorls`) primea drepturi de admin | Permisiunile vin doar din config și din `gmlist`; nicio verificare pe nume |
| Secrete în cod | Un token de bot Telegram în `main.cpp`; chei și parole în config-urile din arhivă | Secretele stau doar în config local, în `.gitignore` |
| Comenzi fără limită | `/test_dungeon` creează un dungeon nou la fiecare apel, fără cooldown | Orice comandă trece prin Guard (cooldown) |
| Parole slabe | `SHA1(SHA1(parolă))` fără salt, calculat în query SQL | Hash Argon2id cu libsodium, în C++ |

Regula care rezultă: **o săgeată de dependență merge doar în jos** (sistem → CORE), niciodată sistem → sistem și niciodată CORE → sistem.

## Ordinea de implementare

Ordinea urmează dependențele, nu interesul: nicio fază nu începe până când poarta celei anterioare nu e trecută.

&#91;embedded content: ordinea de implementare · 6 faze, 5 porți\]

De ce în ordinea asta:

- **CORE întâi**, pentru că toate sistemele îl folosesc. Un bug în CORE descoperit târziu ar afecta tot.
- **Serviciile înaintea sistemelor**, pentru că recompensele și monedele sunt cel mai reutilizat cod din listă.
- **Daily reward primul sistem real**, pentru că e mic, dar trece prin toate straturile. Dacă merge, lanțul e dovedit.
- **Progresia înaintea economiei**, pentru că GameEvents se maturizează pe sisteme cu risc mic. Offline shop-ul mută bani și iteme între jucători, deci un bug acolo înseamnă dupe.
- **Evenimentele și clasamentele la final**, pentru că agregă date din toate celelalte.

## Structura proiectului

Păstrăm cele patru repository-uri m2dev, fiecare ca fork propriu în Git, și adăugăm foldere noi în loc să amestecăm cod în fișierele existente. CMake-ul din `src/game` folosește `GLOB_RECURSE`, deci folderele noi intră automat în build după `cmake ..`.

**Ajustare (Faza 0, pasul 2):** CORE-ul fără dependențe de joc stă într-o librărie separată, `src/core` (ținta CMake `m2core`), nu în `src/game/core`. Așa poate fi testat singur, iar compilatorul împiedică orice include accidental din codul jocului. Serviciile care au nevoie de CHARACTER sau inventar (Reward, Currency, PlayerSystemData) vor sta în `src/game/services/`. Evenimentele din GameEvents transportă identificatori (pid, vnum), nu pointeri la CHARACTER.

```
m2dev-server-src/
  src/game/
    core/                    # CORE, fără logică de joc
      registry/              # SystemRegistry, ciclul de viață, feature flags
      config/                # SystemConfig: citire și validare JSON
      log/                   # canale de log, trace id
      events/                # GameEvents: tipurile de evenimente și magistrala
      reward/                # RewardService
      currency/              # CurrencyService
      storage/               # PlayerSystemData
      guard/                 # cooldown, rate limit, verificări comune
      packets/               # transport Protobuf, dispecer de mesaje pe sistem
      scheduler/             # sarcini periodice pe ora serverului
    proto/                   # fișierele .proto, câte unul per sistem (sursa comună client/server)
    systems/
      daily_reward/          # un folder per sistem
        DailyReward.h        # interfața publică (singura inclusă din afară)
        DailyReward.cpp      # logica
        DailyRewardConfig.h  # structura config-ului și validarea
        DailyRewardPackets.h # pachetele sistemului
        DailyRewardDB.cpp    # citire și scriere în DB
      achievement/ …
  include/                   # librării header-only (JSON, doctest)
  tests/
    core/                    # teste unitare pentru CORE
    systems/<sistem>/        # teste unitare pe logica fiecărui sistem

m2dev-server/
  share/conf/systems/        # <sistem>.json, câte unul per sistem
  sql/migrations/            # 001_core.sql, 002_daily_reward.sql … aplicate în ordine
  share/locale/english/quest/systems/  # quest-uri doar dacă un sistem chiar are nevoie

m2dev-client-src/
  src/UserInterface/systems/<sistem>/  # modulul Python expus din C++, dacă e nevoie

m2dev-client/assets/
  root/core/                 # dispecer de pachete, SystemWindow, helperi
  root/systems/<sistem>/     # logica de interfață a sistemului
  uiscript/<sistem>/         # layout-ul ferestrelor

docs/                        # documentația (acest plan, fișele sistemelor, registrul de modificări)
```

Regula de includere: din afara folderului unui sistem se include **doar** `<Sistem>.h`. Restul fișierelor sunt private sistemului.

## Debugging și logging

Fiecare acțiune a unui jucător primește un **trace id**, iar fiecare sistem scrie pe canalul lui. Așa poți urmări aceeași acțiune prin toate sistemele cu un singur `grep`.

Formatul unei linii de log:

```
2026-10-03 14:02:11 [DAILY]    DEBUG t=8f3a pid=1 recv  claim day=3
2026-10-03 14:02:11 [DAILY]    DEBUG t=8f3a pid=1 check last_claim=2026-10-02 streak=2 -> OK
2026-10-03 14:02:11 [REWARD]   INFO  t=8f3a pid=1 give  item=50852 x2 source=daily
2026-10-03 14:02:11 [EVENTS]   DEBUG t=8f3a pid=1 emit  OnRewardClaimed source=daily
2026-10-03 14:02:11 [ACHIEV]   DEBUG t=8f3a pid=1 recv  OnRewardClaimed -> progress 3/7
2026-10-03 14:02:11 [DAILY]    INFO  t=8f3a pid=1 done  claim day=3 result=OK
```

Ce răspunde fiecare linie: **unde** (canalul), **ce a intrat** (`recv`), **ce s-a verificat** (`check`), **ce a produs** (`give`, `emit`), **cine a primit** (canalul următor cu același `t=`).

Unelte de debugging:

- **Nivel per sistem din config**: `"log_level": "debug"` în `<sistem>.json`, fără recompilare.
- **Comanda GM `/sysdebug <sistem> on|off`**: pornește debug pe un sistem în timpul rulării.
- **Comanda GM `/sysinfo <sistem> [jucător]`**: afișează starea sistemului și datele jucătorului.
- **Fișier de log separat per sistem**, opțional, pe lângă `syslog` și `syserr`.
- **Pe client**: fiecare modul Python scrie cu prefixul sistemului prin `dbg.TraceError`, iar dispecerul de pachete poate loga pachetele primite per sistem.

## Testare și integrare

Fiecare sistem trece prin trei niveluri de test înainte să construim ceva peste el. Un sistem care pică un nivel nu se integrează.

| Nivel | Ce testează | Cum | Când |
| --- | --- | --- | --- |
| Unitar | Logica pură (de exemplu: „ziua 8 revine la ziua 1”, „a doua revendicare în aceeași zi e refuzată”) | doctest în `tests/`, fără DB și fără rețea, rulat la fiecare build | la fiecare modificare |
| Integrare locală | Sistemul pe serverul din VM: pachete, DB, log-uri | plan de test scris în fișa sistemului, comenzi GM de test (`/sysinfo`, setarea datei de test) | după implementare |
| Regresie | Că sistemele vechi merg la fel după integrarea celui nou | lista de verificare a sistemelor deja validate, refăcută la fiecare integrare | înainte de merge |

Integrarea progresivă:

1. Snapshot în VirtualBox și branch nou în Git (`system/<nume>`).
2. Implementare și teste unitare pe branch.
3. Integrare pe VM: CORE + sistemele validate + sistemul nou.
4. Verificarea log-urilor după planul de test, apoi lista de regresie.
5. Merge în `main` doar dacă totul trece. Altfel, problema e aproape sigur în sistemul nou sau în interacțiunea lui cu cele existente.
6. Actualizarea fișei sistemului și a registrului de modificări.

## Reguli de lucru și registrul de modificări

O problemă se repară doar după ce îi știm cauza și impactul. Pașii, în ordine:

1. Identificăm problema și sistemul (log-uri, trace id).
2. Găsim sursa și explicăm de ce apare.
3. Stabilim impactul și ce alte componente pot fi atinse.
4. Propunem soluția și o discutăm.
5. Implementăm pe un branch, testăm, apoi completăm registrul.

Fiecare etapă de lucru urmează același ciclu: explic ce facem și de ce, arăt structura, implementăm, testăm, verificăm, documentăm, apoi trecem mai departe. Fără cod masiv pentru mai multe sisteme deodată.

Șablonul pentru registrul de modificări (`docs/CHANGELOG.md`):

```markdown
### MODIFICARE #001
- Sistem:
- Fișier(e):
- Problemă:
- Cauză:
- Soluție:
- Impact:
- Sisteme afectate:
- Test efectuat:
- Rezultat:
- Commit:
```

## Fișa de sistem și statusul

Fiecare sistem are o fișă în `docs/systems/<sistem>.md`, completată înainte de implementare și actualizată după validare.

```markdown
# <Sistem>
- Scop:
- Responsabilitate (ce face):
- Ce NU face:
- Date primite (pachete, evenimente):
- Date produse (pachete, evenimente, înregistrări în DB):
- Interfața publică (<Sistem>.h):
- Folosește din CORE:
- Evenimente ascultate / publicate:
- Tabele DB și migrare:
- Config (<sistem>.json):
- Poate funcționa independent: da / nu
- Plan de test (unitar, integrare):
- Probleme cunoscute:
```

| Sistem | Faza | Depinde de | Status |
| --- | --- | --- | --- |
| CORE minimal (registry, config, log, events, guard) | 0 | — | Planificat |
| RewardService, CurrencyService, PlayerSystemData | 1 | CORE minimal | Planificat |
| Daily reward | 2 | Reward, PlayerSystemData, Scheduler | Planificat |
| Switchbot | 2 | CORE, inventar | Planificat |
| Achievement | 3 | GameEvents, Reward, PlayerSystemData | Planificat |
| Challenges | 3 | GameEvents, Reward, PlayerSystemData | Planificat |
| Battle pass | 3 | GameEvents, Reward, PlayerSystemData, Scheduler | Planificat |
| Titluri | 3 | Reward (tip TITLE), PlayerSystemData | Planificat |
| Biolog | 3 | GameEvents, Reward, PlayerSystemData | Planificat |
| Offline shop | 4 | Currency, PlayerSystemData, Guard, log de economie | Planificat |
| Căutare în magazine | 4 | Offline shop (prin interfață de citire) | Planificat |
| Ranking și evenimente (world boss, global goal) | 5 | GameEvents, Scheduler, Reward | Planificat |

## Primul lucru de implementat: scheletul CORE (Faza 0)

Începem cu **CORE minimal plus un sistem-martor, `heartbeat`**, care nu face nimic în joc. Rolul lui e să dovedească faptul că lanțul complet funcționează: înregistrare → config → log → eveniment → test. Orice sistem real de mai târziu va folosi exact același lanț.

Pașii, în ordine, fiecare cu propria validare:

1. **Organizarea lucrului.** Fork-uri proprii în Git pentru cele patru repository-uri, branch-ul `core/skeleton`, `.gitignore` pentru config-urile cu secrete, folderul `docs/` cu acest plan.
2. **Log pe canale** (`core/log`). Folosim spdlog, care e deja în `vendor`. Un canal per sistem, nivelul citit din config, un generator de trace id.
3. **Config** (`core/config`). Adăugăm o librărie JSON header-only în `include/`. Loader cu validare: câmp lipsă sau greșit → mesaj clar în `syserr` și sistemul nu pornește.
4. **Registry** (`core/registry`). O interfață comună pentru sisteme (nume, pornire cu config, oprire, reîncărcare), lista de sisteme și flag-ul `enabled` din config.
5. **GameEvents minimal** (`core/events`). Trei evenimente publicate din punctele existente din m2dev: `OnLogin`, `OnLogout`, `OnMobKill`. Punctele exacte de publicare le identificăm împreună în sursă, înainte de a modifica ceva.
6. **Teste** (`tests/`). doctest header-only, o țintă CMake `tests` care rulează pe FreeBSD, cu primele teste pentru config și pentru evenimente.
7. **Sistemul `heartbeat`** (`systems/heartbeat`). Se înregistrează, își citește `heartbeat.json`, ascultă `OnLogin` și scrie în log: `[HEARTBEAT] t=… pid=… recv OnLogin`.
8. **Comenzile GM `/sysinfo` și `/sysreload`**, testate pe `heartbeat`.

**Transportul Protobuf face parte din Faza 0**, între pașii 5 și 6: Protobuf adăugat în `vendor` pentru server și client, generarea codului din `proto/` integrată în CMake, un header comun care transportă mesajele și un dispecer care le trimite sistemului destinatar. Sistemul `heartbeat` primește un mesaj de test (`HeartbeatPing` → `HeartbeatPong`), ca să validăm drumul complet client → server → client înainte de orice sistem real.

Faza 0 e validată când:

- [ ] `game` și `db` compilează pe VM fără avertismente noi
- [ ] `tests` rulează și toate testele trec
- [ ] un `heartbeat.json` invalid oprește sistemul cu mesaj clar, iar serverul pornește în rest normal
- [ ] la login apare linia `[HEARTBEAT] … recv OnLogin` cu trace id
- [ ] `enabled: false` în config dezactivează sistemul fără recompilare
- [ ] `/sysinfo heartbeat` și `/sysreload heartbeat` funcționează
  - [ ] mesajul Protobuf `HeartbeatPing` ajunge de la client la server, iar `HeartbeatPong` revine și apare în log-ul clientului
- [ ] jocul se comportă exact ca înainte (login, luptă, inventar)

După validare urmează **Faza 1** (RewardService, CurrencyService, PlayerSystemData), apoi **daily reward** ca primul sistem real.
