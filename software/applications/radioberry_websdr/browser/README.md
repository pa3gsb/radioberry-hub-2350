# RadioBerry WebSDR-browserinterface

Open de HTTP-adrespagina van het board voor de live ontvanger. De firmware
levert de interface zelf, met spectrum, waterfall, afstemming, gain, AGC
en AM/USB/LSB-audio. De DSP draait op de RP2350. Chrome is automatisch
gecontroleerd; andere browsers moeten afzonderlijk worden beproefd.

Voor een zelfstandige spectrumdemo voer je eerst `./build.ps1` uit en open
je het gegenereerde `index.html`. Als los bestand (`file://`) genereert de
browser gesimuleerde signalen. Via HTTP gebruikt de pagina de board-API,
tenzij je expliciet `?demo=1` toevoegt. De audiostream vereist het board of
de hosttestfixture. Zie de [firmware-bouwinstructies](../../../README.md).

## Bediening

- Typ een frequentie in **kHz** en druk op Enter of de knop naast het veld.
  `7100,125` en `7100.125` betekenen beide 7.100.125 Hz. Escape herstelt
  de bevestigde waarde. Het proefbereik is 100–30.000 kHz; dit legt het
  uiteindelijke boardbereik niet vast.
- Klik of tik in het spectrum om dat punt naar het midden af te stemmen.
  Gebruik `−`/`+` of de pijltjestoetsen als het spectrum toetsenbordfocus heeft.
- De bandknoppen kiezen een nieuwe gesimuleerde scène. Normaal afstemmen
  verschuift alleen het venster; de bronnen houden hun radiofrequentie.
- Kies 48, 24 of 12 kHz span, kleuren en de ondergrens van de niveauschaal.
  De boarddemo biedt voorlopig alleen 48 kHz.
  Frequentie, span, kleuren of ondergrens wijzigen start de waterfall opnieuw.
- Pauzeren bevriest de simulatie. Afstemmen en weergave-instellingen blijven
  bruikbaar en tonen dan één bijgewerkt beeld. Hervatten laat de tijd doorgaan.

De bron levert 512 relatieve niveaus met een nominale snelheid van tien
beelden per seconde. De waterfall bewaart 200 regels (20 seconden
simulatietijd), nieuwste bovenaan. Een achtergrondtabblad pauzeert de
simulatietijd. De tijden zijn geen gemeten radiotijd of browserprestatie.
In de boarddemo bepalen de boardtijdstempels de waterfalltijd; gemiste
tijdvakken blijven leeg. Pauzeren stopt het ophalen en tekenen, terwijl
het board zijn synthetische bron laat doorlopen.

## Op een telefoon proberen

Voor deze proef kun je de bestanden tijdelijk vanaf je computer aanbieden.
Vanuit deze map, met Python geïnstalleerd:

```powershell
py -3 -m http.server 8080 --bind 0.0.0.0
```

Open op een telefoon op hetzelfde netwerk `http://<LAN-IP-van-je-pc>:8080/?demo=1`.
Stop de proefserver met Ctrl+C. Bereikbaarheid hangt ook af van de firewall
en netwerkisolatie. Dit is alleen een hulpmiddel voor de demo. Bij de
boarddemo opent de telefoon rechtstreeks `http://<IP-van-het-board>/`.
Zonder `?demo=1` probeert een HTTP-pagina altijd verbinding te maken met
de board-API; een ontbrekende API wordt als verbindingsverlies getoond.

Probeer invoer met het telefoontoetsenbord, tikken op een piek, pauzeren,
kleuren en draaien naar landschap. Controleer dat de pagina niet zijdelings
scrollt en dat spectrum en waterfall dezelfde frequentie-as gebruiken.

## Aanpassen en bouwen

Bewerk de bestanden onder `src/` en voer vanuit deze map uit:

```powershell
.\build.ps1
```

Het bouwscript gebruikt alleen PowerShell en werkt offline. `index.html`
is het gegenereerde, direct te openen resultaat; wijzig dat bestand niet
handmatig. Het is bewust ongecomprimeerd en bevat leesbare broncode.
Dit gegenereerde bestand staat buiten Git. De firmware bouwt de ingebedde
pagina rechtstreeks uit `src/` via `../embed_assets.py`.

## Controle herhalen

```powershell
.\verify.ps1
# Eventueel met Edge:
.\verify.ps1 -Browser 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
```

Deze controle bouwt eerst de standalone pagina, start een eigen verborgen
browserprofiel, blokkeert externe
HTTP(S)-verzoeken en controleert de echte pagina via het browserdebugprotocol.
De simulatietimer wordt voor de controle handmatig doorgestapt. De controle
bevat onder meer frequentiekalibratie, afstemmen, ongeldige invoer, pauze,
waterfallvolgorde na bufferomloop, schermbreedtes en nagebootste aanrakingen.
Resultaten en schermafbeeldingen worden in een eigen tijdelijke map gezet.
De testbrowser wordt daarna gesloten. Een echte telefoon is hiermee nog
niet getest.

De HTTP-integratiecontrole gebruikt dezelfde C-service als de firmware,
gecompileerd met GCC en via een tijdelijke localhost-testserver:

```powershell
.\verify-http.ps1 -Python 'C:\pad\naar\python.exe'
```

Deze controle omvat afstembevestiging, oude/duplicaatbeelden, ongeldige
data, verbindingsverlies en boardherstart. Dit bewijst de hostkoppeling;
W5500-prestaties en een echte telefoon moeten op het board worden gemeten.

Voor een controle rechtstreeks tegen het fysieke board:

```powershell
.\verify.ps1 -PageUrl 'http://<board-ip>/' -LiveBoard
```

Die controle leest de pagina en spectrumdata, controleert de weergave op
verschillende schermbreedtes en bewaart schermafbeeldingen. Zij wijzigt
de afstemming niet en gebruikt geen routes van de localhost-testfixture.

De huidige audiocontrole gebruikt `pcm-probe.html` en ondersteunt de
PCM-testtoon, AM en SSB:

```powershell
.\verify-audio.ps1 -PageUrl 'http://<board-ip>/pcm-probe.html' -Seconds 20
.\verify-audio.ps1 -PageUrl 'http://<board-ip>/pcm-probe.html' -AM -Seconds 20
.\verify-audio.ps1 -PageUrl 'http://<board-ip>/pcm-probe.html' -SSB usb -Seconds 20
```

Voor de IQ-controles gebruik je `verify-iq.ps1`; met `-Python` kun je het
werkelijke Python-programma kiezen als `python` niet op PATH staat. De
scripts in `../tests/` bevatten ook hosttests en metingen tegen een expliciet
opgegeven boardadres. De gedeelde RBS1-decoder staat naast die tests.

## Sources and licence

This browser interface includes code from
[madpsy/ka9q_ubersdr](https://github.com/madpsy/ka9q_ubersdr), commit
`a18b58aa8865adafa95fa2fe4110d503fe102f7b` (9 October 2026), extracted on
10 October 2026. The combined browser interface is distributed under the
included [GNU GPL version 3](LICENSE-ubersdr.txt). The public-domain statement
at the repository root does not replace the licence of these components.

| Local file under `src/vendor/ubersdr/` | Upstream file under `static/v2/src/lib/` | Changes |
|---|---|---|
| `palettes.js` | `palettes.js` | Complete module; source attribution added |
| `spectrumTrace.js` | `spectrumTrace.js` | Only `binsToPixels()` and `frequencyTicks()` retained, unchanged |
| `waterfallRing.js` | `waterfallRing.js` | Only `ringSlices()` retained, unchanged |

The selected upstream files had no individual copyright or licence headers;
none were removed. `LICENSE-ubersdr.txt` is the unchanged upstream
`LICENSE.TXT`. The source comments identify the upstream files and commit.

The interface, simulation and board transport are in `src/index.template.html`,
`src/style.css`, `src/app.js` and `src/board.js`. The build combines these
with the helpers, removes module `export` keywords and wraps the script;
source attribution is retained and the result is not minified.

The IQ and PCM pages and their scripts are project code. No other UberSDR
packages are included. Keep this README, the licence text and editable source
when distributing the browser interface. The firmware provides these files
and `build.ps1` in `/browser-source.zip`, generated by `../embed_assets.py`.
