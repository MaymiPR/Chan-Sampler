# Binary analysis notes (behaviour only)

Chan Sampler is built from FL Studio's output: renders, screen captures and logged
calls ([EVIDENCE.md](EVIDENCE.md)). On 2026-10-28 FL's own binaries were also
read with REA (Ghidra 12.1.4), for **behaviour analysis only**: what FL does, to
guide what to measure next. No decompiled code and no constants were copied into
Chan Sampler; anything here that changes the plugin is first measured against
FL's renders, as before.

Binaries (FL Studio 26.1.7):

| File | SHA-256 |
|---|---|
| `FLEngine_x64.dll` (the engine, Delphi) | `6d9c5ab09c1d5594012ac2c31cf9ed08e73f42164ed0964cccae10a95d9b8e02` |
| `Shared\elastique_x64.dll` (zplane elastique, loaded by FL for time stretching) | `8f98e9e5038d6eebf333100e182c9819254b7537e4fec1741b547a5fa6fd9c96` |

Each finding cites the REA Evidence IDs behind it. The Evidence bundles are kept
outside the repository because they hold decompiled text. **Observed** means
read in the binary; **inferred** means reasoned from it; **unverified** means
not backed by Evidence.

## Summary

| Question | Verdict |
|---|---|
| Declicking / end-of-sample fade | Confirms the measured modes; gives Crossfade's curve as a closed form |
| SMP LENGTH trim and its tail filter | Changes: short trims are lengthened, not clamped; length uses the unrounded start |
| Reversed short trims | Order of operations confirmed; cause of the residual unresolved |
| Arpeggiator TIME / GATE / slide | Changes: TIME is an exact formula; GATE ends on whole ticks; slide glides per tick |
| Channel filters and LPx2 compensation | Confirms the measured target; compensation is a closed form riding the voice gain ramp |
| e3 onsets and band split | Changes: onsets are per spectral peak inside the vocoder; the split is inside the FFT |

## Declicking and the end of the sample

- **Observed:** FL builds one fixed table of declick presets, each an in-fade and an out-fade (method, time and shape), and rebuilds it when the rate changes. Fade lengths are fixed times, so their length in samples follows the host rate. The voice takes its preset straight from the channel's declicking mode. Evidence: ev_3da0449927a30a8b416cfbbe2ec96a2e6f4b34ab59fe8d89d0bfc9bc387c4173, ev_046bc9712d6be0888895acf3d09e87652d7b209d455777b5ac6a1a41a7bca823, ev_6b9c48a2bb37fdf45023d9cfd6fde5b82efe2694198b8a4147c1db292fa893c4, ev_4f051e0245d8c8abefb737f1b9206538c3c206230234b5eef4f688ca0994faf4.
- **Confirms** our measured times for every mode (Out only, the transient modes, Generic, Smooth, Crossfade).
- **Inferred:** the mode-name to preset mapping, matched by fade times rather than by strings.
- **Observed:** the out-fade is a raised cosine whose first step is already below full level and whose last step is not quite zero; the in-fade is the same table reversed. Evidence: ev_22d684db60bb01848dbb96d322735c53342c46601db5ec2d642399078041e504.
- **Crossfade:** the cosine is bent by an exponential shaping curve, a closed form that can replace our polynomial fits once measured against them. Evidence: ev_e4064146960ec2d9cd55efe76f7b25f8a4194ba226b304d92f993754dd563e79, ev_732295acb6426e13db58d0096286ecc74045f0dbed7dce2865dd815860d2840f.
- **Transient in-fade:** subtracts the first sample's value with a falling cosine. **Confirms** our x − x0·(1 − S). Evidence: ev_22d684db60bb01848dbb96d322735c53342c46601db5ec2d642399078041e504.
- **Transient (no bleeding) out:** a one-pole high-pass whose coefficient sweeps up exponentially, then a short closing cosine. **Confirms** the measured structure. One scaling value is set at run time and wasn't read (**unverified**). Evidence: ev_046bc9712d6be0888895acf3d09e87652d7b209d455777b5ac6a1a41a7bca823.
- **Cut groups:** a cut voice switches to a very short preset when a global option is on, probably "Fast declick for cut groups" (link **unverified**). Evidence: ev_02ecd1f2c400cb6e4f1dbd25863987e7489a7f74690c23c433fa59482b05f92d.
- **End of an untrimmed sample:** nothing is precomputed there; the fade is the voice's out-fade. **Inferred:** the remaining ~−57 dB comes from the cosine table's first/last-step convention and the out-fade's sample-accurate start. Next: measure that convention in an end-of-sample render. What triggers the release at the sample end is **unverified**.

## SMP LENGTH trim

- **Observed order of precomputing:** trim, then the stereo effect, then Reverse, then the other precomputed effects (fades, EQ, boost, clip, DC, crossfade). Evidence: ev_4abf3ef05388b4d774eaa07b8baf53f4ddd6d3b557491f8c94e31e942c7a7488, ev_b2e42695fcef7d9982cc75edb4793052816c12a805f45ec7132375f20fbe6852.
- **Observed trim:** the start rounds as we do, half to even. The length is taken from the *unrounded* start fraction, where we round the start first. Evidence: ev_89603cd5c92d6a7379fcb7f9a0e2754b622091e4c93890270da2298ebfc4a5cc.
- **Changes:** a trim shorter than a fixed minimum is lengthened *by* that amount (or its start moved back near the end of the file), not clamped *to* it. A trim a little shorter than the minimum therefore plays nearly twice the minimum. Evidence: ev_89603cd5c92d6a7379fcb7f9a0e2754b622091e4c93890270da2298ebfc4a5cc. Next: measure a few very short LENGTH values in FL.
- **Observed:** the crop keeps both its first and last frame. Evidence: ev_409748bb683d94236e2d2307e31426abc39a17ae012509851f959c05a746a121.
- **Observed:** the start filter runs only when SMP START > 0, the end filter only when LENGTH < 100%. Evidence: ev_89603cd5c92d6a7379fcb7f9a0e2754b622091e4c93890270da2298ebfc4a5cc.
- **Observed filters:** they are the same swept high-pass as the transient declicker, over a short window at the start and a longer one at the end. The window is sized from a rate. **Confirms** our 22-frame start. Evidence: ev_b4c08eafdf57a7ee2fae64df952547947f0f221c615cb7eb3985fb75e97c5e89, ev_7ecea4bc903e54bf51ff39737cc2cec040933ef785c3602b54424c90126b2313, ev_bd58d405f853cb75469621c48d0b1b7ee0bd15bd6fd47fccd7de51392d837214.
- **Content dependence, observed:** each channel first skips exact zeros at the edge, the window is clipped to what remains, and each channel is filtered on its own. **Confirms** "trailing silence is left out". Evidence: ev_acb7c2714a6d7fdf3aa7361dbbd7b9570b89de871d7a4730f32be04fa1d05576.
- **Inferred:** the rate is the sample's own rate.

## Reversed short trims

- **Observed:** start filter, end filter, then a whole-buffer Reverse; the fade-in/out knobs and other effects come after Reverse. Evidence: ev_9ed3a3bf22132e36dfbfe45340aacd0f4ca6822be889c9a428129a8142ebfd68, ev_b2e42695fcef7d9982cc75edb4793052816c12a805f45ec7132375f20fbe6852.
- **Inferred:** the "one frame more with Reverse" is the crop keeping its last frame, which the end filter silences forward but which is audible once reversed.
- **Unresolved:** the −46…−54 dB residual. Next: model the end filter as the swept high-pass on short segments, with the lengthening rule, and re-run the reversed trim cases.

## Arpeggiator

- **Observed TIME:** the step length is an exponential in the raw TIME value, rounded and converted to whole song ticks; Hold is the top raw value. Evidence: ev_53ad5f0fa4c290919b9eeb1f4a3aad447fa98c8770a40bd035151c116fdd867d.
- **Checked offline:** this formula and our fitted one both reproduce all 42 measured step lengths (`nulltest/data/arp_ticks_dense.txt`). They differ at 15 TIME values we never measured (raw 501–503, 562–564, 614, 657, 696, 966, 1021, 1219, 1232, 1432, 1445). Next: render those in FL and correct our fit.
- **Observed scheduling:** steps fire on song ticks at whole multiples of the step from the arp start. The gate ends a whole number of ticks after the step (at least one), truncated. Ours uses a fractional gate. Evidence: ev_158b85d260054aa53f6034021c53b1a59d40f4dca4b7e77e5b492cec9d951321 and the scheduler records listed below.
- **Observed slide:** with a gate under 100%, the pitch glides from the gate end to the next step in per-tick rounded increments. At 100% it jumps, which **confirms** our tested case.
- **Confirms:** a note held at the same pitch with Auto (sustain) isn't retriggered.
- **Unverified:** the tick-unit value used by the conversion (inferred from the data fit).
- Further Evidence: ev_4071276c5049473eeb21227180de1d59a84934aa019ac595bd4e4ed8c116807e, ev_516849ecd5fad94bfce002d58360eb7fbef03b449aea4902f15ddd84ef6de5dd, ev_06740b5166cdfe6ba16e328f0a91c155a6189b31f0e1d930bdecfb137e45e771, ev_d864b4d435dd9fbf355930bb5ea7b65ce33724058323621646a77dff60009030.

## Channel filters and LPx2

- **Observed:** Mod X / Mod Y (envelope and LFO) are worked out once per song tick, and the filter coefficients are set once per tick from knob plus modulation. Only Fast LP ramps its cutoff per sample. Evidence: ev_31eb675f6a8c54fab57909bc82c91325066416316a7d2553e1bbe79cb5c2d3f4, ev_659cd962fc0d5deb8f7c1213a001c070ebdce4988161e66616bc87ce65d35599.
- **Observed types:** Fast LP is a one-pole with exponential cutoff and resonance maps. LP / BP / HP / BS are standard biquads with exponential frequency and Q maps. The SVF types are a Chamberlin SVF. "x2" is a flag adding a second identical stage, not a separate filter type. Evidence: ev_a282da397f1a5acc4a40bac96218d1acb2404f525fa123fb73768f9444a8f404, ev_4dbfece1a1c8793b96564056a16f5cdead32a1ee82dedb583c5a2d01c121a98f, ev_63cb6557caea1f45c2f60d1312a04ca3b334c5620946c79428cde6be085a9236.
- **Confirms:** our SVF LPx2 second-stage damping.
- **Observed LPx2 compensation:** a simple closed form in the stage's Q and frequency, applied to the voice's gain targets. **Confirms** our measured target at maximum resonance. It can replace our resonance × cutoff table. Evidence: ev_70aacfb13f46667719adc8924ee25b2878c1899a0891bf7269b63455e37b3430, ev_5557b8a674d93a3ea912fbe85dc7a86d9dec01815cc6f66f696dcbcbd03d8af2.
- **Observed ramp:** the whole voice gain (envelope × velocity × pan × compensation) moves toward its target once per tick with a capped per-sample slope. Evidence: ev_0365bbd61cf61f0a147bd18dadb9ae0ee0c35d338425d1ec06487cfb4543e0f6.
- **Inferred:** this **confirms** "linear within the tick, capped at the envelope slew", but the cap applies to the product, which only differs from our model when the envelope or pan move at the same time.
- **Unverified:** the voice-filter type to "x2" mapping, the rate scale and slope-cap values, and the gain at the note's first tick.
- **Side finding (observed):** `TFilterLine` is the precomputed reverb's delay line, consistent with our reverb row. Evidence: ev_74c25b04198c1acc73dea70ce97d2376b9018e35874f59497b2b89cf0abb99e9.

## e3 (elastique v3): onsets and the band split

- **Observed:** `ElProV3_CreateInstance` builds, by mode, a Pro core holding a phase vocoder, a PSOLA pair and an FFT band split. Evidence: ev_8bfc7e64647f2b4e02c0da804cd439cb9d9d62c9394d072ccad9a163b9ca812a, ev_310d78807304194e380f176e9d241064f7bbe66cd41f3d411a03603cb652a61e.
- **Unverified:** that FL's e3 generic uses the Pro core's default mode.
- **Observed split:** done inside a short FFT, not by a crossover filter. Bins below a fixed split frequency are removed, with a short linear taper; the high band goes to PSOLA and the full band to the vocoder. The split frequency is fixed for moderate pitch ratios, rises steeply for large ones, and is capped at Nyquist. Evidence: ev_fd056fc87379bbdcbee21adeef17f87e9b166bd7df25425834fa268da19e7e94.
- **Observed:** inside the vocoder, bins above the split take the PSOLA stream unless they are in an onset state. Next: compare the split with our e3 band split.
- **Observed onset detection:** in the phase vocoder, once per hop, for each spectral peak's region, from two measures:
  - a relative spectral flux (change against the energy a few hops back), which is why detection is level-free;
  - an energy time-centroid within the frame (a second FFT with a time ramp).

  A peak whose energy sits late in the frame is marked "onset coming". It restarts when its centroid reaches the frame centre while its flux is still high enough, or at once on a very large flux. Whole-frame rules spread or postpone restarts by energy share. Auftakt (zplane's beat tracker, also in the DLL) isn't used. Evidence: the e3 records listed below.
- **At a restart, observed:**
  - only the peaks in the restart state restart; the others carry on, which **confirms** "the low band restarts on its own onsets";
  - the restart position is the loudest sample of the windowed, full-band frame within a hop either side of its centre, not of the low band. This **changes** our model;
  - the phase turn is a time shift proportional to (1 − ratio) × position, which **confirms** our −ω(s − 1)·P;
  - restarting bins get a capped window-ratio gain.
- **Inferred (the open swell problem):** a steady swell keeps its energy late in the frame and waits in "onset coming". It restarts once its centroid drifts to the centre while the flux is still high enough. So the delay after a swell's start depends on how fast it rises, and very slow swells never restart. This fits our measured "fixed time per rate". Next: rebuild our e3 onset trigger as this per-peak flux + centroid state machine, then measure it against the swell renders.
- **Unverified:** the stricter threshold set used by some cores, the analysis window shape, which channel feeds detection, who calls the vocoder's external onset setter, and the swell consequences above.
- e3 Evidence: ev_0235cd2136c2e6186a8ddd3feedde3e08e8cb962be47aa265bbec646f32f41a0, ev_6a94a62589b247c1e302c9b031121b87e99056529d8fadff02277feec3b4a4e3, ev_2473d9ac92f3e6120a9a5289d12cd88ba6d542a9c7700c73f0d50f89fba8380a, ev_c3bcb9cdd4e5a6ba1bef555577afdc1c724cf264738bcf203e51ec3bc15f2ac3, ev_2bb4d70eb08eccb0b448520a3d6d8f267175f0402097c5d6241b43382a9e78b5, ev_c9bc42874e2d086df388b742e6638739e7561780f3911268a24fcf6fbbb4239f, ev_a7a5449c40cdf17a24db0b5e24af37ac96927283457a01db49dd1e99237a5235, ev_90ddce62f4b8d76a5dfa13332d0a6b8263b4c11d18ae3f5305eed3f93e67ab63, ev_922386c2c80bff6016310a290c88596708ad222d8ed6c3a49c98f916f3b9c059, ev_d9ca595c97f300d64f32e1d263d1f7ef3c98b752b6989aba3b289f39bc6d18d6, ev_a7656eec32609053e3eb6785deae404175087b437065ef7f3236316f26841b1d, ev_c6e8cb132a5b10fc2316df17540effd7a92ee89421e2d9da826ec8ade85d201e, ev_e745a6010e1a0eef98c992ba7091da6bf8ae8070d52b1d86d3b0072dd1ee07e1, ev_95352cbd62a01fb6e3ed9ad2ad12bbb566e8f65f4c2577c183b59388177a0e67, ev_99ba2a016701ad77a07c55b39ed5e73c030b16430841e9ffb3055a34b109e883, ev_06bc5d1bddeb66c70e782ab900d87333a764f7fe76b697a69996634f7bf32230, ev_e27b34192a6a059a3b40edc322b4367c66527a725bd32a1ab816da44199d7278, ev_618612e68c506bf3ce69c2dc2a0b39567299d60552b152f960cda1da3ed6c872, ev_237214afb4b537efccb9f29ff4840a280f30231cf20a9c99121d863b2c9d6843, ev_8b20dbd82a5140a1a6a210e7b7e2b60edc0a4c0a4ce1d094bda91ca8b18580d8, ev_83b2121e5fa974c20864238f0ece4e3741d7fe4c83f836d0a2ebd198af550e24, ev_961d19a29c48b5c2d7dce53ae4813073ce3a0f7da8c92e41c4337858d1e71a71, ev_977dd8922fecf20fe385f77a68e05cfe5a27fbec6298429b0320b1155d631c27, ev_0540f5aee57149798356c1214aab3f5601ce58d8f3b1cb613803d86f796117d9, ev_e5cf69ac7a3e94d6172e3b0aa605730e3d044c466e6c59008131b719902b0dfb, ev_7adbb2e380437d3d16f990eb5edf705cc7db9f5b2a1bdaa047b8b51d7491186a, ev_26f71e919c974f0df5df8d9841b2e38aea626502657381a5d1944d88b1fed8d7.

## Arpeggiator scheduler Evidence

ev_50f0d4e61aabae7765bc9bf7542bd1876fa30e582e56d2d5c6ea60836b5a1aa9, ev_4d70cdc2c4fce6927335400e39cb3cb6b424cd308a235de1aa6bc97a201a0b72, ev_c115137901e23a5fb919ec1aad78ccf10e3b87445be0ac91630dc4ab7f486c9a, ev_b53cb7bb230449dd6fa6cf7eb04a3d39a3c6da22d23abd4925259fefc8042923, ev_fc78ceec9301b366469fbb7fd3857c0ee6299609281bacd526bd91e79021e9ac, ev_1ebd8dc7188b88dad934c89007c87294172ecac20cbfb4fc1feeffbc27728319, ev_8eb573e39d6fe6501814fa33ed36348323f4dda1fcc7b9b437ecd9c18843e8f8, ev_5662521dd5e1b00e8e71907d8c54fcc1004188525d9a7484be82485e7a5c9a19.
