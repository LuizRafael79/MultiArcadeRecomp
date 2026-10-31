# Vendored dependencies

This directory contains upstream source snapshots used by the MameRecomp core.
Keep each dependency in its own directory so its upstream identity and any
existing license, notice, and `UPSTREAM.md` files remain attached to that copy.

When preparing module level attribution, record the original repository URL,
revision, license, local modifications, and the MameRecomp modules that consume
the dependency. Do not merge upstream license texts into a single file or imply
that all vendored code has the same license.

Build outputs and generated game code belong under `build/`, not here.
