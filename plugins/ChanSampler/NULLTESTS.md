# Null tests

The same notes through FL Studio's Sampler and through Chan Sampler, subtracted. The residual is in dB re FL's
output; lower is better. How the renders are made (in-FL pairs, offline cases, the render queue, FL's 16-bit export,
velocity, tempo and rates) and the scale (NULL, close, diff) are in [MEASUREMENTS.md](MEASUREMENTS.md#setup). The
in-FL null tests of each feature are given beside its law there; the time-stretch modes cannot null (FL's engine is
proprietary) and are compared by behaviour.

## Offline suite

At velocity 100 FL's 16-bit floor sits about 6 dB higher than at velocity 128. The EQ and ring-mod cases, three pogo
cases, the precomputed cut at 512 and the SVF LPx2 filter case, which reach −60 to −66 dB at velocity 128, read
"close" at velocity 100 because of the export alone. The small real differences in the offline suite are the
precomputed cut at its lowest cutoff (−46.5 dB), the envelope with tensions (−46.2) and the Fast LP, LP, BP and LPx2
filter cases (−51 to −59). The in-FL tests in [MEASUREMENTS.md](MEASUREMENTS.md) are the reference.

Offline suite: **82 cases: 65 NULL, 17 close, 0 diff** ([full table](#offline-cases)).

## Offline cases

Offline cases (`python nulltest.py` in `nulltest/`; this page: `python nulltest.py --report`), FL velocity 100.

| Case | Residual (dB) | Result |
|---|---|---|
| off_base | -70.7 | NULL |
| off_boost16 | -69.8 | NULL |
| off_boost64 | -77.7 | NULL |
| off_boost128 | -82.7 | NULL |
| off_boost256 | -88.3 | NULL |
| off_boost64clip | -91.5 | NULL |
| off_eq0 | -62.7 | NULL |
| off_eq64 | -59.9 | close |
| off_eq192 | -59.9 | close |
| off_eq256 | -62.8 | NULL |
| off_fin64 | -69.5 | NULL |
| off_fout64 | -69.8 | NULL |
| off_fin256 | -65.9 | NULL |
| off_fout256 | -66.6 | NULL |
| off_fin1024 | -64.5 | NULL |
| off_fout1024 | -66.6 | NULL |
| off_start25 | -70.5 | NULL |
| off_len50 | -71.0 | NULL |
| off_norm | -76.5 | NULL |
| off_rev | -71.3 | NULL |
| off_inv | -70.7 | NULL |
| off_swap | -70.7 | NULL |
| off_fadest | -67.8 | NULL |
| off_remdc | -76.0 | NULL |
| off_trim16 | -69.5 | NULL |
| off_trim64 | -70.0 | NULL |
| off_trim256 | -70.7 | NULL |
| off_rm256_128 | -56.2 | close |
| off_rm128_64 | -55.1 | close |
| off_rm256_0 | -56.2 | close |
| offL_pccut512_0 | -54.4 | close |
| offL_pccut768_0 | -61.8 | NULL |
| offL_pccut256_0 | -46.5 | close |
| offL_pccut640_320 | -66.6 | NULL |
| offL_pccut640_640 | -73.7 | NULL |
| off_sd0 | -70.8 | NULL |
| off_sd1024 | -70.8 | NULL |
| off_sd3072 | -71.3 | NULL |
| off_sd4096 | -71.3 | NULL |
| off_pogo0 | -58.4 | close |
| off_pogo128 | -58.4 | close |
| off_pogo384 | -58.4 | close |
| off_pogo512 | -58.4 | close |
| off_so0.1 | -68.8 | NULL |
| off_so0.5 | -70.1 | NULL |
| off_dk0_so | -70.5 | NULL |
| off_dk0_cut | -75.2 | NULL |
| off_dk1_so | -70.8 | NULL |
| off_dk1_cut | -70.7 | NULL |
| off_dk2_so | -70.5 | NULL |
| off_dk2_cut | -75.6 | NULL |
| off_dk3_so | -70.6 | NULL |
| off_dk3_cut | -75.6 | NULL |
| off_dk4_so | -70.9 | NULL |
| off_dk4_cut | -75.7 | NULL |
| off_dk5_so | -70.8 | NULL |
| off_dk5_cut | -75.5 | NULL |
| off_env_default | -76.5 | NULL |
| off_env_att10000 | -80.1 | NULL |
| off_env_dec20000 | -80.0 | NULL |
| off_env_rel20000 | -78.9 | NULL |
| off_env_tension | -46.2 | close |
| off_flt0_128_0 | -54.2 | close |
| off_flt0_96_160 | -61.0 | NULL |
| off_flt1_128_0 | -54.0 | close |
| off_flt1_96_160 | -62.8 | NULL |
| off_flt2_128_0 | -69.4 | NULL |
| off_flt2_96_160 | -59.5 | close |
| off_flt3_128_0 | -66.6 | NULL |
| off_flt3_96_160 | -71.5 | NULL |
| off_flt4_128_0 | -66.1 | NULL |
| off_flt4_96_160 | -70.3 | NULL |
| off_flt5_128_0 | -51.2 | close |
| off_flt5_96_160 | -62.3 | NULL |
| off_flt6_128_0 | -60.0 | NULL |
| off_flt6_96_160 | -60.7 | NULL |
| off_flt7_128_0 | -59.8 | close |
| off_flt7_96_160 | -63.3 | NULL |
| off_xf64_noloop | -71.1 | NULL |
| off_xf128_noloop | -71.1 | NULL |
| off_xf256_noloop | -71.0 | NULL |
| off_st25_len50 | -70.8 | NULL |
