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