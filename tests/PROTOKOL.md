# Protokół dla testów synchronizacji

Środowisko: Moduł ESP-WROOM-32 DevKitC-v1 na płytce stykowej, Broker MQTT Mosquitto 2.1.4 i serwer Python na komputerze PC,
sieć: hotspot telefonu. 
Każdy test od czystego stanu, procedura niżej - chyba że opisano inaczej.

## Procedura czystego stanu
1. Karta microSD: EVENTS.BIN -> przemianowanie, w celu archiwizacji poprzedniego stanu.
2. `idf.py erase-flash`, `idf.py flash monitor`, bez karty - zerowanie NVS: next_seq, acked_seq.
3. Włożenie karty do modułu na płytce, wciśnięcie przycisku `EN` na płytce modułu ESP32 (ponieważ monitor resetuje prłytke przy połączeniu)
4. Usunięcie pliku: `server/events.db`.

## T1a — utrata potwierdzeń (26.11.2026)
- Cel: serwer zapisuje zdarzenie, ale potwierdzenie (ACK) nie dociera.
- Metoda: `DROP_ACK=0.3` - potwierdzenie pomijane po COMMIT dla ok. 30% przypadków.
- Wynik: 15 zdarzeń; 4 ACK pominięte; 4 duplikaty rozpoznane; baza (15, 1, 15).
- Wniosek: 0 utraconych, 0 zdublowanych.
- Logi: ESP32: `logs/T1a_2611_esp.txt` SERWER: `logs/T1a_2611_server.txt`

## T1b — utrata potwierdzeń, 100 zdarzeń (27.11.2026)
- Cel: taki jak dla T1a, na próbie wekszej próbce - do oceny statystycznej.
- Metoda: generator testów (`CONFIG_TERMINAL_TEST_EVENTS = 100`, użytkownik o ID: 999,
  START/STOP na przemian), serwer z `DROP_ACK=0.3`.
- Wynik:

| Wielkość | Wartość |
|---|---|
| zapis 100 zdarzeń na kartę | 1174 ms (≈ 11,7 ms / zdarzenie) |
| zdarzenia nowe w bazie | 100 |
| ACK pominięte (wszystkie próby) | 53 z 153 (34,6 %) |
| duplikaty rozpoznane | 53 |
| baza `(count, min, max)` | (100, 1, 100) |
| czas dosłania | 567,7 s |

- Analiza: oczekiwana liczba pominięć to: 100 · 0,3 / 0,7 ≈ 43 (odch. std. ≈ 8).
  Czas ≈ 53 × 10,2 s (oczekiwanie na ponowienie) + 100 × 0,26 s ≈ 567 s — zgodny z pomiarem.
  Pojedyncze ACK opóźnione do ~1,5 s (#35, #55) — prawdopodobnie są to skoki opóźnienia hotspotu.
- Wniosek: 0 utraconych, 0 zdublowanych; każde pominięte ACK ma dokładnie jedno ponowienie.
- Logi: ESP32: `logs/T1b_2711_esp.txt`, SERWER: `logs/T1b_2711_server.txt`

## Obserwacje dodatkowe
- 27.11 — reset ESP (przez `idf.py monitor`) w trakcie generowania zdarzeń: na karcie 2 pełne rekordy,
  `next_seq = 3`, brak uciętego rekordu (plik `EVENTS_przerwany.BIN`).
- Część linii logu ESP zaczyna się od ok. 64 znaków „�” — dotyczy wyłącznie konsoli,
  nie wpływa na dane, prawdopodobnie jest to wina któregoś z luźnych przewodów na płytce prototypowej.

## T2 — restarty ESP podczas zapisu i dosyłania (27.11.2026)
- Cel: zanik zasilania w trakcie zapisu dziennika oraz w trakcie oczekiwania na ACK.
- Metoda: generator 1000 zdarzeń; serwer bez awarii (`DROP_ACK=0`);
  3 × przycisk EN (POWERON_RESET): podczas generowania, przy #84 w locie, przy #173 w locie.
- Wynik:

| Wielkość | Wartość |
|---|---|
| zdarzeń na karcie (generowanie przerwane) | 556, `next_seq = 557`, brak uciętego rekordu |
| zakładka po restartach | 83, potem 172 — wysyłka wznawiana od zakładki + 1 |
| zdarzenia nowe w bazie | 556 |
| duplikaty rozpoznane | 2 (#84, #173 — po jednym na restart z zdarzeniem w locie) |
| baza `(count, min, max)` | (556, 1, 556) |
| przepustowość dosyłania | 384 zdarzenia / 103,7 s ≈ 0,27 s na zdarzenie |
| odbudowa sesji przy 556 rekordach | 40 ms (≈ 0,07 ms / rekord) |
| szukanie zakładki przy starcie | ≈ 2,5 ms / niepotwierdzony rekord (fopen na rekord — do optymalizacji) |

- Wniosek: 0 utraconych, 0 zdublowanych; restart w dowolnym momencie nie narusza dziennika.
- Logi: ESP32: `logs/T2_2711_esp.txt`, SERWER: `logs/T2_2711_server.txt`

## T3 + T4 — utrata brokera i utrata sieci, zdarzenia offline (27.11.2026)
- Metoda: generator 500 zdarzeń, serwer bez awarii; ok. #100 broker zatrzymany na ~35 s (T3);
  ok. #300 hotspot wyłączony na ~70 s, w trakcie 3 prawdziwe zdarzenia (palec + wybór aktywności) (T4).
- Wynik:

| Wielkość | Wartość |
|---|---|
| zapis 500 zdarzeń | 6668 ms |
| zdarzenia w bazie | 503 (500 + 3 offline #501–#503) |
| duplikaty | 1 (#442 — retransmisja QoS 1 przez bibliotekę MQTT, bez ponowienia przez terminal) |
| baza `(count, min, max)` | (503, 1, 503) |
| T3: powrót po starcie brokera | ESP ~4 s, serwer ~3 s; wysyłka od #102 |
| T4: wykrycie utraty sieci | reason 200 (beacon timeout), potem 201 |
| T4: Wi-Fi -> MQTT po powrocie sieci | 29 s* |
| T4: przerwa w dostawie zdarzeń | 104 s (#301 -> #302) |

*czas zawyżony przez środowisko testowe: po wyłączeniu hotspotu laptop przełączył się automatycznie na sieć domową

- Wniosek: 0 utraconych, 0 zdublowanych; zdarzenia z czasu braku sieci dosłane w kolejności.
  Duplikat #442 powstał bez awarii (chwilowe opóźnienie sieci 2,5 s) — deduplikacja jest konieczna
  również w normalnej pracy.
- Logi: ESP32: `logs/T3_2711_esp.txt`, SERWER: `logs/T3_2711_server.txt`

## W1 — weryfikacja karta <-> baza (27.11.2026)
- Narzędzie: `server/verify.py` — porównuje każdy rekord z EVENTS.BIN, w tym CRC - 
  z wierszem w bazie: brakujące, nadmiarowe, różne pola oraz uszkodzone i powtórzone numery na karcie.
- Dane T3+T4: karta 503 rekordy, baza 503 wiersze -> 0 brakujących, 0 nadmiarowych, 0 różnic —
  **zgodne co do bitu**.
- Test narzędzia: kopia bazy z usuniętym #250 i zmienionym polem `user` w #300 →
  wykryto brak [250] i różnicę [300], wynik NIEZGODNOSCI (kod wyjścia 1), co dowodzi, że narzędzie działa prawidłowo.