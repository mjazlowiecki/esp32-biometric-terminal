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