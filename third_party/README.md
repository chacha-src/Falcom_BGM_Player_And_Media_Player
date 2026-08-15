# Voice changer core

Male→female / female→male processing is implemented in `VcVocalTract` using:

- [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (MIT)
- [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) (MIT)

Vendored under `third_party/`. See `third_party/NOTICE.txt` for attribution.

Pitch and formant are set independently (`setTransposeFactor` + `setFormantFactor` with pitch compensation).
