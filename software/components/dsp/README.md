# Herbruikbare RadioBerry DSP

`radioberry::dsp` bevat de signaalverwerking, onafhankelijk van een applicatie,
Pico-I/O, gatewarewoordformaat, netwerkprotocol of browser. Een applicatie
beheert de DSP-state en voedt de functies met samples. Er is geen heap nodig.

| Header onder `include/radioberry/` | Verwerking |
|---|---|
| `dsp.h` | Complexe float-samples en radix-2 FFT/IFFT |
| `tone.h` | Fasecontinue PCM16-testtoon |
| `am.h` | 48 kHz IQ naar 12 kHz AM-audio, filtering, DC-verwijdering en AGC |
| `ssb.h` | 48 kHz IQ naar 12 kHz USB/LSB-audio, decimatie en complexe FIR |
| `agc.h` | Instelbare envelopdetector en gain-regeling |
| `receiver.h` | Gedeelde AM/USB/LSB-ontvanger, AGC-bediening en PCM16-uitgang |
| `spectrum.h` | Hann-venster, 512-punts FFT, vermogensmiddeling en dB-niveaus |

De lagere AM-/SSB-functies blijven beschikbaar voor applicaties die zelf
de uitgangsverwerking willen verzorgen. De gedeelde ontvanger levert één
PCM16-sample per vier invoerparen:

```c
#include <radioberry/receiver.h>

static rb_dsp_receiver receiver;

/* Eenmaal initialiseren, buiten de samplelus. */
rb_dsp_receiver_init(&receiver);
rb_dsp_receiver_agc(&receiver, RB_AGC_NORMAL, -100);

/* Per genormaliseerd IQ-paar op 48 kHz; Q is al correct georiënteerd. */
int16_t pcm;
if (rb_dsp_receiver_pcm16(&receiver, RB_DSP_RX_LSB, i, q, &pcm)) {
    /* Verwerk/buffer pcm op 12 kHz in de applicatie. */
}
```

Roep `rb_dsp_receiver_reset()` aan bij sampleverlies of een moduswissel.
De reset bewaart de AGC-instellingen. Gateware-tags controleren en Q
omkeren gebeuren vóór deze API, in de adapter van de gebruikte hardware.

`rb_dsp_spectrum_push()` neemt eveneens gewone float-IQ-paren aan. De
functie meldt wanneer een complete FFT aan het gemiddelde is toegevoegd.
`rb_dsp_spectrum_level()` geeft per bin het gemiddelde vermogen in dB,
met lage frequenties eerst. De applicatie bepaalt de samplefrequentie voor
de frequentie-as, het publicatiemoment en de codering voor een display.
`rb_dsp_spectrum_reset()` begint een nieuw gemiddelde en wist ook een
gedeeltelijk venster, zonder de cumulatieve tellers te wissen.

In `applications/radioberry_websdr/` blijven de gateware-/DMA-adapters,
samplecontinuïteit, blokqueues, RF-configuratie, HTTP en RBS1/RBA2-framing.
`websdr_spectrum.c` bepaalt bijvoorbeeld de 100 ms publicatiecadans en
codeert de dB-waarden naar displaybytes; de FFT en vermogensberekeningen
staan in `src/spectrum.c`. De ruwe gatewarediagnose telt en meet de
oorspronkelijke ADC-woorden in de hardwareadapter.

De rekenkern gebruikt uitsluitend C en de standaard wiskundebibliotheek.
Bij een Pico-build staan de kritieke SSB-, receiver- en AGC-samplefuncties
in RAM; de hostbuild gebruikt dezelfde code zonder Pico-headers.
