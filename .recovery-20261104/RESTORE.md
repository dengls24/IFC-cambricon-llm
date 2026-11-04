# Recovery snapshot — 2026-11-04

Source layout: `$HOME/2-nand/kvnand/IFC-cambricon-llm`. Repository visibility: public.
This snapshot preserves the selected current source bytes, executable modes, configuration, local changes and useful small results. It does not include model parameters or installed environments. No research experiment was executed for this backup.

## Restore

1. Clone this repository on Linux. Check out the backup commit/branch listed in the delivery report. Windows users should set `git config --global core.autocrlf false` before cloning if exact byte verification is required; this command is guidance only and was not executed by the backup.
2. Run `python3 .recovery-20261104/verify_files.py` from the cloned repository. This checks every uploaded file except the manifest itself.
3. Keep the original relative directory layout above when scripts use sibling projects. Review the model/environment reference list for absolute paths that need adapting to the new machine.
4. Follow original project README/build instructions and requirements files. Recreate the environment; then download model IDs/revisions listed in the original configs/scripts. Training outputs and custom parameters must be restored separately from the full disk backup.
5. `source-manifest.json` records every included source hash, mode, original Git HEAD/status/refs, excluded file and pruned environment/cache directory. `patches/` preserves staged and unstaged changes relative to the original HEAD. Current checked-out files already include these changes: do not apply patches again to this snapshot.

## Environment reconstruction

Mapped environment names found in project sources: No exact environment mapping verified.
The environment metadata was freshly read on 2026-11-04 without copying installed package files. For a mapped environment, `conda create -n NAME-restored --file .recovery-20261104/environments/NAME-conda-explicit.txt` reproduces the recorded conda package builds when upstream packages remain available. Python distribution lists are installed-version inventories, including conda-provided packages; use the project requirements and inspect direct-URL/editable records before restoring extra pip packages. A clean environment reinstall and scientific reproducibility run were not performed.
Original dependency/build files: Makefile.
