# TI TMS320C31 references

These original Texas Instruments documents are kept here so the C31
interpreter, disassembler, and static translator have stable primary references
alongside the source. They are unmodified; TI copyright and notices remain in
each PDF. The PDFs are reference material, not source code to copy into the
runtime.

| File | TI literature | Use in MultiArcadeRecomp |
| --- | --- | --- |
| `SPRU031F_User_Guide.pdf` | SPRU031F, *TMS320C3x User's Guide* | ISA authority: Appendix A opcode patterns, Chapter 13 instruction formats and effects, pipeline, memory, boot loader. Appendix B includes C31/VC33 boot-loader source. |
| `TMS320C3x_Users_Guide_1994_SPRU031D.pdf` | SPRU031D, October 1994 | Historical comparison copy. Its revision notes highlight repeat/interrupt, serial-port, and DMA documentation updates. |
| `SPRU083_Embedded_Control_Technical_Brief.pdf` | SPRU083, *TMS320C31 Embedded Control Technical Brief* | Device and embedded-system overview. |
| `SPRU194_General_Purpose_Applications_User_Guide.pdf` | SPRU194, *TMS320C3x General-Purpose Applications User's Guide* | Application examples and assembly routines for understanding realistic C3x programs. |
| `SPRU035_Assembly_Language_Tools_User_Guide.pdf` | SPRU035D, *TMS320C3x/C4x Assembly Language Tools User's Guide* | Assembler/linker behavior, directives, object images, and how source assembly becomes program words. |

Canonical TI sources:

- https://www.ti.com/lit/ug/spru031e/spru031e.pdf
- https://ftpmirror.your.org/pub/misc/bitsavers/components/ti/TMS320xx/1994_TMS320C3x_Users_Guide.pdf
- https://www.ti.com/jp/lit/an/spru083/spru083.pdf
- https://www.ti.com/lit/ug/spru194/spru194.pdf
- https://www.ti.com/lit/pdf/spru035

SPRU031F is the normative source for instruction decoding. Appendix A's
major-opcode fields must be combined with the per-instruction encodings in
Chapter 13; entries marked variable do not by themselves fully identify the
instruction form. Parallel forms are compound operations and need one atomic
translation with source snapshots and deferred writes. Until that exists,
their encodings stay on the interpreter/handler fallback.
