# UltraCombo ADSP-2105 backend

Source repository: https://github.com/thefixinhixon/UltraCombo

The interpreter and translator in this directory are taken from UltraCombo's
`src/adsp.c`, `src/adsp.h`, `src/adsp_core.h`, `src/adsprec.c`, and
`src/adsprec.h`. They are used by the KI1 DCS runtime; the MK1/MK2 shared
MultiArcadeRecomp ADSP implementation remains separate for now.

The upstream `rec_abi.h` was reduced to the DCS image and binding types needed
by the generated ADSP code. Generated sound translations are produced in the
build directory from the selected KI ROMs and are not checked into source.

License: MIT, see [`LICENSE`](../../license/ultracombo-adsp/LICENSE).
