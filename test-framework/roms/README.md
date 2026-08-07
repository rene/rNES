# CI ROMs

Drop **license-clean** `.nes` files here for the automated ROM tests that run
in GitHub CI, and list them in [`../roms-ci.txt`](../roms-ci.txt).

> ⚠️ **Do not commit commercial ROMs.** Only add ROMs that are legally
> redistributable, for example:
> - Homebrew games released under a permissive/open license.
> - Public-domain NES test ROMs (e.g. from the NESdev community) whose license
>   allows redistribution — check each one.

For each ROM you add:

1. Copy the `.nes` file into this directory.
2. Add its path (relative to `test-framework/`, e.g. `roms/mygame.nes`) to
   `../roms-ci.txt`.
3. Regenerate the baseline so CI knows the expected result:
   ```sh
   make -C test-framework
   python3 test-framework/run_tests.py \
       --rom-list test-framework/roms-ci.txt \
       --baseline test-framework/baseline.json --update-baseline
   ```
4. Commit the ROM, the updated `roms-ci.txt`, and `baseline.json`.

While this directory is empty, the CI job is a green no-op.

Your **private / commercial** ROM collection is never committed — run those
locally against your own list file:

```sh
python3 test-framework/run_tests.py --rom-list ~/my-roms.txt
```
