# PSXRecomp compiler source

This directory contains the PSXRecomp game compiler sources and only the
dependencies needed to build `mamerecomp_psxrecomp`:

- Upstream: `https://github.com/Unistudio-Inova/psxrecomp`
- Revision: `060ef0ab`
- Main project license: PolyForm Noncommercial 1.0.0 (`LICENSE`)
- Dependency notices: `lib/fmt/LICENSE.rst`, `lib/rabbitizer/LICENSE`, and
  `lib/toml11/LICENSE`
- Five headers used by the translator at compile time are retained under
  `runtime/include/`; the PSXRecomp runtime implementation is not vendored.

The vendor copy intentionally omits the PSXRecomp machine runtime and unrelated
media, UI, modding, and widescreen components. The compiler emits source that
expects PSXRecomp runtime APIs; MameRecomp must provide an explicit adapter
before those generated functions can execute in a MameRecomp board module.
Preserve the upstream license and notices in source distributions. The
project's non-commercial restriction applies to this vendored compiler code.
