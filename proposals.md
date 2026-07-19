Cose da discutere

1. La hashmap con elastic hashing ha un'api che permette fare tutte le inserzioni in
una volta, seguendo esattamente la descrizione del paper => non è fatta con una funzione insert(), che poi quando
finisce lo spazio amplia la hash table

TODO aggiungi anche alle altre hashmap la possibilità di fare tutto in una volta per avere dei benchmark fair

2. Se ha senso tenere traccia delle proble complexity: aggiunge complessità ma sembra interessante
vedere effettivamente se elastic/funnel hashing ci mettono cosi pochi probe in ricerca


Fai anche un benchmark di ricerca di cose che non sono nella table per vedere se va male come ci si aspetterebbe
Gli autori hanno sorvolato sulla perdormance per query negative, ma è interessante vedere _quanto_ vada male