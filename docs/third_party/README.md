# Vendored dependencies

This directory contains upstream source snapshots used by the MultiArcadeRecomp core.
Keep dependency source snapshots in their own directories. Their upstream
provenance notes are under `docs/third_party/`, and legal texts/notices are
under `docs/license/`; these files remain part of the repository distribution.

When preparing module level attribution, record the original repository URL,
revision, license, local modifications, and the MultiArcadeRecomp modules that consume
the dependency. Do not merge upstream license texts into a single file or imply
that all vendored code has the same license.

Build outputs and generated game code belong under `build/`, not here.

The MAME SH-4 vector/approximation FPU algorithm port lives in
`mame-sh4-fpu/`; its MIT attribution and upstream revision are recorded in
`docs/third_party/mame-sh4-fpu/UPSTREAM.md`.
