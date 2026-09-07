# HashMaps

## Build

```
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --target HashMaps HashMapsProbes -j
```

- `HashMaps` non abilita i contatori ed e il target da usare per misurare i tempi
- `HashMapsProbes` definisce `HASHMAP_COUNT_PROBES=1` e mostra probe e diagnostiche

Entrambi eseguono gli stessi algoritmi e controlli di correttezza. La build strumentata aggiunge scritture ai contatori, quindi i suoi tempi non devono essere confrontati direttamente con quelli della build normale

In CLion basta ricaricare CMake, selezionare uno dei due target e inserire gli argomenti del benchmark nel campo Program arguments


## Modalita

I comandi accettano questi nomi:

| Modalita | Implementazioni eseguite |
| --- | --- |
| `standard` | controllo |
| `elastic` | Elastic |
| `funnel` | Funnel |
| `both` | controllo ed Elastic |
| `funnel-control` | controllo e Funnel |
| `all` | tutte e tre |

## Cosa esegue ogni benchmark

Ogni punto di benchmark costruisce mappe nuove e vuote. Non viene riutilizzata la tabella del punto precedente e non esiste una fase di warm-up che modifica la struttura misurata

Il protocollo di un punto e sempre questo:

1. carica o genera le chiavi da inserire
2. inserisce `capacity - floor(delta * capacity)` chiavi uniche
3. controlla che ogni inserimento sia riuscito
4. cerca una volta tutte le chiavi presenti nello stesso ordine
5. controlla che ogni lookup restituisca il valore associato corretto
6. costruisce una chiave sicuramente assente ed esegue un lookup negativo


Il lookup negativo non e incluso in `lookup time`, `lookup avg` o `lookup max`. Ha tempo e probe separati nell'output

Nella tabella compatta le voci significano:

| Voce | Significato |
| --- | --- |
| `map` | implementazione misurata |
| `c` | costante di Elastic usata da `f(epsilon)`, `-` per controllo e Funnel |
| `insert time` | CPU time totale per inserire tutte le chiavi del punto |
| `insert avg` | probe medi per inserimento |
| `insert max` | massimo numero di probe osservato in un singolo inserimento |
| `lookup time` | CPU time totale per cercare una volta tutte le chiavi presenti |
| `lookup avg` | probe medi per lookup positivo |
| `lookup max` | massimo numero di probe osservato in un singolo lookup positivo |

La diagnostica dettagliata usa le colonne `metric`, `observed` e `theoretical reference`. `observed` e il risultato effettivo della singola esecuzione. `theoretical reference` riporta il requisito di correttezza o la scala asintotica pertinente; non e sempre un valore numerico atteso con cui fare un confronto uno a uno. Un `-` significa che per quella riga non viene proposta una previsione teorica

Lo sweep di `c` aggiunge queste voci:

| Voce | Significato |
| --- | --- |
| `case1 fallback` | fallback del Caso 1 divisi per il numero totale di inserimenti entrati nel Caso 1 |
| `case3 count` | numero di inserimenti entrati nel Caso 3 |
| `case3 avg/max` | probe medi e massimi fra i soli inserimenti del Caso 3 |

La wordlist viene separata sui caratteri di spaziatura riconosciuti da `isspace`, inclusi spazi, tab e newline. Maiuscole, minuscole e punteggiatura non vengono normalizzate, quindi `word`, `Word` e `word,` sono tre chiavi diverse. I duplicati esatti vengono rimossi conservando l'ordine della prima occorrenza

## Quale benchmark usare

| Comando | Dataset | Capacita | Parametri variati | Output |
| --- | --- | --- | --- | --- |
| `<wordlist>` | prefissi della wordlist deduplicata | maggiore power-of-two disponibile fino a 65536 | quattro delta standard, tutte le mappe | tabella compatta |
| `--wordlist-sweep <wordlist> [max] [seed]` | prefissi della wordlist deduplicata | maggiore power-of-two fino a `max` | quattro delta standard, tutte le mappe | tabella compatta |
| `<wordlist> <mode> [delta] [seed]` | tutte le parole uniche | calcolata dal numero di parole e da delta | un solo delta e le mappe indicate | diagnostica dettagliata |
| `--demo <mode>` | chiavi generate deterministiche | 65536 | quattro delta standard | tabella compatta |
| `--demo <mode> <delta> [seed]` | chiavi generate deterministiche | 1024 | un solo delta | diagnostica dettagliata |
| `--c-sweep [delta] [seed]` | stesse chiavi generate a ogni punto | 16384 | `c = 0.25, 0.5, 1, 2, 4, 8, 16, 100` | tabella Elastic compatta |
| `--elastic-lookup-comparison <wordlist> [max] [delta] [seed]` | prefisso della wordlist deduplicata | massima potenza di due fino a `max`, default 4096 | tre lookup sulla stessa tabella Elastic | tabella comparativa |
| `--csv-wordlist-sweep <wordlist> [max] [seed]` | stesso protocollo di `--wordlist-sweep` | stessa selezione automatica | quattro delta standard | CSV |
| `--csv-wordlist <wordlist> <mode> [delta] [seed]` | stesso protocollo del test wordlist singolo | calcolata dalla wordlist | un solo delta | CSV |
| `--csv-load-sweep <mode> [seed]` | stesso protocollo di `--demo` senza delta | 65536 | quattro delta standard | CSV |
| `--csv-c-sweep [delta] [seed]` | stesso protocollo di `--c-sweep` | 16384 | otto valori di c | CSV |
| `--csv-elastic-lookup-comparison <wordlist> [max] [delta] [seed]` | stesso protocollo del confronto lookup | stessa selezione automatica | tre lookup sulla stessa tabella Elastic | CSV |


## Delta sulla stessa wordlist

Il comando da usare per confrontare piu delta sulla stessa wordlist e:

```
./cmake-build-release/HashMapsProbes --wordlist-sweep path/to/wordlist.txt 65536
```

I quattro valori predefiniti sono:

```text
delta       load factor      chiavi inserite
1/8         7/8              n - floor(n/8)
1/32        31/32            n - floor(n/32)
1/128       127/128          n - floor(n/128)
1/256       255/256          n - floor(n/256)
```

La capacita `n`, il seed e l'ordine della wordlist rimangono uguali. Ogni punto usa le prime `n - floor(delta*n)` parole uniche, quindi le chiavi del punto meno carico sono un sottoinsieme di quelle dei punti successivi

I delta dello sweep sono nell'array `LOAD_SWEEP_DELTAS` all'inizio di `src/wordlist_runner.c`. Per cambiare valori o ordine bisogna modificare quell'array e ricompilare; la CLI non accetta una lista arbitraria di delta

## Benchmark leggibili

Per eseguire facilmente tanti test su una wordlist basta passare soltanto il suo path:

```
./cmake-build-release/HashMapsProbes path/to/wordlist.txt
```

Questo esegue controllo, Elastic e Funnel ai quattro load factor standard

```
./cmake-build-release/HashMapsProbes --wordlist-sweep path/to/wordlist.txt 32768
```

Se una partizione Funnel non esiste per la coppia finita di capacita e delta, quel solo punto Funnel viene saltato con un messaggio esplicito. Controllo ed Elastic continuano normalmente. Per ottenere tutte e quattro le righe Funnel con i parametri attuali serve in pratica una wordlist abbastanza grande da usare almeno `n=32768`, cioe circa 32640 chiavi uniche al load massimo

Confronto principale sui load factor `7/8`, `31/32`, `127/128` e `255/256`, con capacita 65536:

```
./cmake-build-release/HashMapsProbes --demo all
```

Demo dettagliata con capacita 1024 e un singolo `delta`:

```
./cmake-build-release/HashMapsProbes --demo all 0.125
```

La demo dettagliata mostra anche lookup negativo, occupazione dei sottoarray, casi Elastic, destinazioni Funnel e riferimenti alle scale teoriche

Sweep della costante Elastic `c` sui valori da 0.25 a 100, con capacita 16384:

```
./cmake-build-release/HashMapsProbes --c-sweep 0.125
```

## Confronto lookup Elastic

Questo benchmark costruisce una sola tabella Elastic e cerca le stesse chiavi con tre sequenze:

- `phi-aware` e il lookup corrente che ricostruisce i draw locali tramite l'inversa di `phi`
- `full-table-siphash` usa `SipHash(key, k) % capacity` per ogni probe e permette ripetizioni
- `modular-double-hash` e il precedente approccio modulare: deriva partenza e passo da SipHash e rende il passo coprimo con la capacita, visitando ogni slot una sola volta

Il benchmark richiede `HashMapsProbes` e usa una capacita massima predefinita di 4096 perche i due lookup ciechi possono fare lavoro quadratico sul totale delle chiavi cercate:

```
./cmake-build-release/HashMapsProbes --elastic-lookup-comparison path/to/wordlist.txt 4096 0.125
```

La riga `insertion probes` descrive il costo della singola costruzione Elastic condivisa dai tre metodi. `positive total`, `positive avg` e `positive max` descrivono i lookup delle chiavi presenti. `before / at / after phi` separa i lookup che trovano la chiave prima, esattamente a oppure oltre l'indice globale `phi(i,j)` ricostruito dalla collocazione. Per `phi-aware`, non avere risultati dopo `phi` controlla l'invariante che collega inserzione e lookup, ma da solo non dimostra i limiti asintotici del paper. Tutti i valori di `k` attraversati contano come probe, inclusi quelli fuori dall'immagine di `phi`. `avg / n` divide la media dei probe positivi per la capacita. `negative probes` misura separatamente una chiave assente

Per controllare la scalabilita rispetto a `n` bisogna ripetere il comando con la stessa wordlist, delta e seed cambiando soltanto la capacita massima, per esempio 512, 1024, 2048 e 4096

## CSV

I comandi CSV stampano soltanto intestazione e dati su standard output, quindi possono essere salvati direttamente:

```
mkdir -p benchmark-results
./cmake-build-release/HashMapsProbes --csv-load-sweep all > benchmark-results/load-sweep.csv
./cmake-build-release/HashMapsProbes --csv-c-sweep 0.125 > benchmark-results/c-sweep.csv
./cmake-build-release/HashMapsProbes --csv-wordlist-sweep path/to/wordlist.txt 65536 > benchmark-results/wordlist-sweep.csv
./cmake-build-release/HashMapsProbes --csv-elastic-lookup-comparison path/to/wordlist.txt 4096 0.125 > benchmark-results/elastic-lookup-comparison.csv
```

Per misurare i tempi usare gli stessi comandi con `HashMaps`. In quella build le colonne dei probe rimangono vuote e `probe_counting` vale zero


## Significato dell'output

| Colonna o gruppo | Significato |
| --- | --- |
| `implementation` | `standard`, `elastic` o `funnel` |
| `dataset` | path della wordlist oppure nome del dataset generato |
| `capacity` | numero totale di slot fisici `n` |
| `keys` | numero di chiavi uniche inserite in quel punto |
| `delta` | frazione libera richiesta |
| `load_factor` | valore osservato `keys / capacity` |
| `c` | costante usata da `f(epsilon)` per Elastic, vuota per le altre mappe |
| `seed` | chiave SipHash di 16 byte scritta come 32 cifre esadecimali |
| `probe_counting` | 1 nel target `HashMapsProbes`, 0 nel target normale |
| `correct` | 1 se inserimenti, lookup positivi e lookup negativo sono corretti |
| `insertion_seconds` | CPU time totale della fase di inserimento |
| `insertion_operations` | numero di inserimenti completati |
| `insertion_probes` | totale degli slot fisici controllati durante gli inserimenti |
| `insertion_probe_average` | `insertion_probes / insertion_operations` |
| `insertion_probe_maximum` | massimo numero di probe usato da un singolo inserimento |
| `positive_lookup_seconds` | CPU time totale per cercare una volta tutte le chiavi presenti |
| `positive_lookup_operations` | numero di lookup positivi completati |
| `positive_lookup_probes` | totale degli slot controllati dai lookup positivi |
| `positive_lookup_probe_average` | media di probe per lookup positivo |
| `positive_lookup_probe_maximum` | massimo osservato fra i lookup positivi |
| `negative_lookup_seconds` | CPU time del singolo lookup della chiave assente |
| `negative_lookup_probes` | probe usati dal singolo lookup negativo |
| `elastic_batch_zero_*` | inserimenti e probe del batch iniziale `B0` |
| `elastic_case_one_*` | frequenza, fallback e probe del Caso 1 |
| `elastic_case_two_*` | frequenza e probe del Caso 2 |
| `elastic_case_three_*` | frequenza, probe totali, media e massimo del Caso 3 costoso |
| `funnel_insertions_in_a` | chiavi collocate nella gerarchia `A'` |
| `funnel_insertions_in_b` | chiavi arrivate e collocate in `B` |
| `funnel_insertions_in_c` | chiavi arrivate e collocate in `C` |
| `funnel_insertion_failures` | inserimenti falliti perche entrambe le scelte in `C` erano piene |
| `theory_log2_inverse_delta` | scala `L = log2(1/delta)` |
| `theory_log2_inverse_delta_squared` | scala `L^2` |
| `theory_log2_log2_capacity` | scala `log2(log2(n))` |
| `theory_funnel_maximum_scale` | scala `L^2 + loglog(n)` del massimo Funnel con alta probabilita |
| `theory_funnel_special_insertion_bound` | valore `delta*n/8` usato per il bound sugli inserimenti speciali Funnel |


Il seed predefinito e:

```
000102030405060708090a0b0c0d0e0f
```

Un seed alternativo e una stringa di 32 cifre esadecimali. Va registrato insieme ai risultati:

```
./cmake-build-release/HashMapsProbes --csv-load-sweep all 101112131415161718191a1b1c1d1e1f
```


## Riprodurre i numeri della relazione

Un solo script ricostruisce tutte le misure della sezione Results:

```
./reproduce_benchmarks.sh [output_dir] [wordlist]
```

Ricompila entrambi i target, stampa ogni comando prima di eseguirlo, scrive i CSV nella
directory indicata (default `report/data`) e produce una tabella riassuntiva confrontabile riga
per riga con le tabelle della relazione.

I grafici della relazione leggono blocchi di dati incorporati in cima al `.tex`, non i file su
disco, cosi il sorgente resta autonomo e compila su Overleaf. Dopo aver rifatto i benchmark
vanno rigenerati:

```
./tools/embed_data.sh
```

Senza questo passaggio i grafici continuano a mostrare i numeri vecchi.

Ogni run e determinato dalla tripla (wordlist, delta, seed SipHash a 128 bit). Il seed compare
in ogni riga di output e vale `000102030405060708090a0b0c0d0e0f` di default, quindi due
esecuzioni con gli stessi argomenti danno gli stessi numeri.


## Test

I test non fanno parte dei due target di benchmark e si compilano singolarmente. Il piu
importante e la suite di conformita, che percorre la specifica del paper clausola per clausola
e la verifica sulle strutture che il codice costruisce davvero:

```
cc -std=c11 -O2 -DHASHMAP_COUNT_PROBES=1 -Iinclude -Isrc -Ithird_party \
   $(find src -name '*.c' ! -name 'main.c') third_party/siphash.c \
   tests/conformance_to_paper.c -o /tmp/conformance && /tmp/conformance
```

Stampa `104 checks, 0 failures`. Senza `-DHASHMAP_COUNT_PROBES=1` i controlli sono 98: sei
dipendono dai contatori. La stessa riga di compilazione funziona per gli altri programmi in
`tests/`, fra cui `compare_lookup_models.c` (confronto fra il lookup phi-aware e la scansione
per livelli), `measure_phi_distribution.c` (media di phi e distribuzione dei piazzamenti) e
`scan_negative_queries.c` (costo delle query negative della scansione per livelli).
