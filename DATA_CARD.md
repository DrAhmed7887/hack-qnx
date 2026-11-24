# Data card: data/demo/

- **Type:** Public open dataset (de-identified)
- **Source:** PhysioNet/CinC Challenge 2015, <https://physionet.org/content/challenge-2015/1.0.0/> (DOI 10.13026/c9fg-a467)
- **License:** ODC-BY 1.0 — "Anyone can access the files, as long as they conform to the terms of the specified license". Text in `data/demo/LICENSE-ODC-BY.txt`; attribution in `NOTICE`.
- **De-identification:** de-identified as stated by PhysioNet; we make no further claim.

## Purpose
Fault demonstration and deterministic QEMU replay only. Headline scores come
strictly from the full-750 local evaluation defined by `eval/split.json`, never
from these six records.

## Contents
Six `train`-pinned records, all in `data/demo/` with sizes and SHA-256 in `MANIFEST.json`:
`a109l`, `a653l`, `v188s`, `t116s`, `t106s`, `f450s`.

`PLAYLIST.json` fixes the replay order for the QEMU shift-review demo:
`a109l, a653l, t116s, t106s, v188s, f450s`.

## Heldout barrier
Zero of these records are in `heldout`. `make demo-verify` (and
`scripts/make_split.py --check`) fail if any demo record is heldout or not in `train`.

## Known limits
- Only 6 records; `train` split only.
- No claim of representativeness of the 750-record set or of any clinical population.
