# Shared video post-processing

`shaders/scanlines.frag` is a board-independent RGB post-process. Feed it the
already composed RGB framebuffer and source dimensions; set `uStrength` to 0
to leave the picture unchanged or 1 for alternating rows at 72% brightness.
The effect is deliberately modest and avoids curvature, masks, and resampling
so that it works with both low-resolution 2D frames and rendered 3D frames.

The optional V-Unit OpenGL path currently exposes the same strength through
`--scanlines=0..1`. Its indexed palette-to-RGB shader applies the row modulation
after the palette lookup. Other board presenters can use the shared RGB shader
once they opt into the OpenGL post-process path; their normal CPU/SDL output is
unchanged.

The shader is original MameRecomp code. The local UltraCombo repository was
consulted for reference: its `crt-lottes-fast.glsl` is marked public domain, and
`crt-hyllian-fast.glsl` carries an MIT license. No source from those shaders is
copied here.
